/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include "gltfAnisotropyOpenPBR.h"
#include "debugCodes.h"
#include "gltf.h"
#include "gltfImport.h"
#include <cmath>
#include <iomanip>
#include <sstream>

#include <fileformatutils/common.h>

using namespace PXR_NS;

namespace adobe::usd {

float
clamp01(float value)
{
    return std::min(1.0f, std::max(0.0f, value));
}

float
calculateOpenPBRImageRotation(float redChannelValue, float greenChannelValue, float rotation)
{
    // Convert channel values from [0, 1] to [-1, 1]
    float x = redChannelValue * 2.0f - 1.0f;
    float y = greenChannelValue * 2.0f - 1.0f;

    // Calculate the angle in radians and apply rotation
    float angle = std::atan2(y, x) + rotation;
    angle = std::fmod(angle, 2.0f * PI);
    if (angle < 0.0f)
        angle += 2.0f * PI;
    return angle;
}

// Calculates the OpenPBR roughness based on roughness and anisotropy strength.
std::pair<float, float>
convertGltfRoughnessAnisotropyToOpenPBR(float roughness, float strength)
{
    if (strength <= 1e-7f) {
        // If strength is near zero, we can skip calculations
        return { roughness, 0.0f };
    }
    // Step 1: Compute glTF alpha-roughness values
    //   glTF defines alpha = roughness^2
    float alpha = roughness * roughness;
    float s2 = strength * strength;
    float alpha_t = alpha * (1.0f - s2) + s2; // mix(alpha, 1.0, s^2)
    float alpha_b = alpha;

    // Step 2: Derive OpenPBR specular_roughness_anisotropy (a)
    //   From the OpenPBR formulation: alpha_b / alpha_t = (1 - a)
    //   Guard against division by zero when alpha_t is near zero
    float a_openpbr = 0.0f;
    if (alpha_t > 1e-7f) {
        a_openpbr = clamp01(1.0f - (alpha_b / alpha_t));
    }

    // Step 3: Derive OpenPBR specular_roughness (r_openpbr)
    //   OpenPBR's invariant: alpha_t^2 + alpha_b^2 = 2 * (r^2)^2
    //   So: r = ( (alpha_t^2 + alpha_b^2) / 2 )^(1/4)
    float alpha_t2 = alpha_t * alpha_t;
    float alpha_b2 = alpha_b * alpha_b;
    float r_openpbr = std::pow((alpha_t2 + alpha_b2) / 2.0f, 0.25f);
    r_openpbr = clamp01(r_openpbr);

    return { r_openpbr, a_openpbr };
}

std::pair<float, float>
convertOpenPBRRoughnessAnisotropyToGltf(float spRoughness, float spAnisotropy)
{
    float om = 1.0f - spAnisotropy;
    float factor = std::sqrt(2.0f / (1.0f + om * om));
    float alpha_t = spRoughness * spRoughness * factor;
    float alpha_b = alpha_t * om;

    float roughness = std::sqrt(clamp01(alpha_b));

    float strength = 0.0f;
    float d = 1.0f - alpha_b;
    // If roughness is near zero, we can skip calculations
    if (d > 1e-7f) {
        // If roughness is near zero, we can skip calculations
        strength = std::sqrt(clamp01((alpha_t - alpha_b) / d));
    }

    return { roughness, strength };
}

// Encode the arguments into the image name to generate a unique name for the OpenPBR roughness and
// anisotropyimages.
std::string
generateOpenPBRAnisotropyImageName(const std::string& prefix,
                                   int roughnessIndex,
                                   float roughness,
                                   int anisotropyIndex,
                                   float rotation)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << prefix << "_";
    if (roughnessIndex >= 0)
        ss << roughnessIndex;
    else
        ss << "x";

    ss << "_";
    if (roughness >= 0.0f)
        ss << roughness;
    else
        ss << "x";

    ss << "_";
    if (anisotropyIndex >= 0)
        ss << anisotropyIndex;
    else
        ss << "x";

    ss << "_";
    if (rotation != 0.0f)
        ss << rotation;
    else
        ss << "x";

    std::string result = ss.str();
    std::replace(result.begin(), result.end(), '.', '_');
    return result;
}

std::string
generateOpenPBRAnisotropyTangentImageName(const std::string& prefix,
                                          int anisotropyIndex,
                                          float rotation)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << prefix << "_";
    if (anisotropyIndex >= 0)
        ss << anisotropyIndex;
    else
        ss << "x";

    ss << "_";
    if (rotation != 0.0f)
        ss << rotation;
    else
        ss << "x";

    std::string result = ss.str();
    std::replace(result.begin(), result.end(), '.', '_');
    return result;
}

// Extracts the roughness value from a roughness image at a given UV coordinate.
float
extractRoughness(const Image& roughnessImage,
                 int roughnessChannel,
                 bool /*bilinear*/,
                 float u,
                 float v)
{
    // NOTE: we ignore bilinear sampling for now and just use nearest neighbor sampling

    // We expect/assume u and v to be in the range [0, 1)

    // Perform nearest-neighbor sampling to retrieve roughness
    size_t linearX = static_cast<size_t>(u * roughnessImage.width);
    size_t linearY = static_cast<size_t>(v * roughnessImage.height);
    size_t linearIndex =
      (linearY * roughnessImage.width + linearX) * roughnessImage.channels + roughnessChannel;

    // Guard against out-of-bounds access.
    if (linearIndex >= roughnessImage.pixels.size()) {
        TF_WARN("Linear index out of bounds in roughness image.");
        return 0.0f;
    }

    float roughness = roughnessImage.pixels[linearIndex];

    return roughness;
}

// Processes anisotropy pixels and populates anisotropy level and angle images.
void
processAnisotropyPixelsOpenPBR(const Image& gltfAnisotropyImage,
                               float roughness,
                               const Image* roughnessImage,
                               int roughnessChannel,
                               bool bilinearRoughnessSampling,
                               const AnisotropyData& anisotropyData,
                               Image* anisoRoughnessImage,
                               Image* anisoAnisotropyImage,
                               Image* anisoTangentImage)
{
    const size_t width = gltfAnisotropyImage.width;
    const size_t height = gltfAnisotropyImage.height;
    const size_t channels = gltfAnisotropyImage.channels;

    const float* srcPixels = gltfAnisotropyImage.pixels.data();
    float* roughnessPixels = nullptr;
    float* anisotropyPixels = nullptr;
    float* tangentPixels = nullptr;

    if (anisoRoughnessImage && anisoAnisotropyImage) {
        anisoRoughnessImage->allocate(width, height, 1);
        roughnessPixels = anisoRoughnessImage->pixels.data();

        anisoAnisotropyImage->allocate(width, height, 1);
        anisotropyPixels = anisoAnisotropyImage->pixels.data();
    }

    if (anisoTangentImage) {
        anisoTangentImage->allocate(width, height, 3);
        tangentPixels = anisoTangentImage->pixels.data();
    }

    const float strengthFactor = static_cast<float>(anisotropyData.strength);
    const float rotation = static_cast<float>(anisotropyData.rotation);
    bool applyRotation = rotation != 0.0f;
    float cosRotation = std::cos(rotation);
    float sinRotation = std::sin(rotation);

    for (size_t y = 0; y < height; ++y) {
        const float ncy = static_cast<float>(y) / height;
        size_t index = y * width;
        size_t srcIndex = index * channels;
        size_t tanIndex = index * 3;
        for (size_t x = 0; x < width; ++x) {
            if (tangentPixels) {
                float redChannelValue = srcPixels[srcIndex];
                float greenChannelValue = srcPixels[srcIndex + 1];
                if (applyRotation) {
                    // map from [0, 1] to [-1, 1]
                    redChannelValue = redChannelValue * 2.0f - 1.0f;
                    greenChannelValue = greenChannelValue * 2.0f - 1.0f;
                    float rotatedX =
                      redChannelValue * cosRotation - greenChannelValue * sinRotation;
                    float rotatedY =
                      redChannelValue * sinRotation + greenChannelValue * cosRotation;
                    // map back from [-1, 1] to [0, 1]
                    redChannelValue = (rotatedX + 1.0f) * 0.5f;
                    greenChannelValue = (rotatedY + 1.0f) * 0.5f;
                }
                tangentPixels[tanIndex] = redChannelValue;
                tangentPixels[tanIndex + 1] = greenChannelValue;
                // 0.5 results from mapping 0 in the range [-1, 1] to 0.5 in the range [0, 1]
                tangentPixels[tanIndex + 2] = 0.5f;
                tanIndex += 3;
            }
            if (roughnessPixels) {
                float strength = srcPixels[srcIndex + 2] * strengthFactor;

                if (roughnessImage) {
                    const float ncx = static_cast<float>(x) / width;
                    roughness = extractRoughness(
                      *roughnessImage, roughnessChannel, bilinearRoughnessSampling, ncx, ncy);
                }

                // convert glTF anisotropy and roughness to OpenPBR anisotropy and roughness
                auto [specular_roughness, specular_roughness_anisotropy] =
                  convertGltfRoughnessAnisotropyToOpenPBR(roughness, strength);

                roughnessPixels[index] = specular_roughness;
                anisotropyPixels[index] = specular_roughness_anisotropy;
            }
            ++index;
            srcIndex += channels;
        }
    }
}

// Processes anisotropy pixels with roughness, populating the anisotropy level
void
processAnisotropyPixelsFromRoughnessOpenPBR(const AnisotropyData& anisotropyData,
                                            const Image* roughnessImage,
                                            int roughnessImageChannel,
                                            Image& anisoRoughnessImage,
                                            Image& anisoAnisotropyImage)
{
    if (!roughnessImage) {
        return;
    }

    const size_t width = roughnessImage->width;
    const size_t height = roughnessImage->height;
    const size_t channels = roughnessImage->channels;

    anisoRoughnessImage.allocate(width, height, 1);
    anisoAnisotropyImage.allocate(width, height, 1);
    float* roughnessPixels = anisoRoughnessImage.pixels.data();
    float* anisotropyPixels = anisoAnisotropyImage.pixels.data();

    float strength = static_cast<float>(anisotropyData.strength);

    for (size_t y = 0; y < height; ++y) {
        size_t index = y * width;
        size_t srcIndex = index * channels;

        for (size_t x = 0; x < width; ++x) {
            float roughness = roughnessImage->pixels[srcIndex + roughnessImageChannel];

            // convert glTF anisotropy and roughness to OpenPBR anisotropy and roughness
            auto [specular_roughness, specular_roughness_anisotropy] =
              convertGltfRoughnessAnisotropyToOpenPBR(roughness, strength);

            roughnessPixels[index] = specular_roughness;
            anisotropyPixels[index] = specular_roughness_anisotropy;

            srcIndex += channels;
            index++;
        }
    }
}

GfVec3f
convertRotationToTangentOpenPBR(float rotation)
{
    // The tangent is a 3D vector where the x and y components encode the anisotropy rotation and
    // the z component is set to 0.5 to represent a neutral value in the range [0, 1]. The
    // rotation is mapped from [0, 2PI] to [0, 1] by taking the cosine and sine of the rotation
    // angle, which gives us values in the range [-1, 1], and then remapping those values to the
    // range [0, 1].
    float x = (std::cos(rotation) + 1.0f) / 2.0f;
    float y = (std::sin(rotation) + 1.0f) / 2.0f;
    return GfVec3f(x, y, 0.5f);
}

void
importAnisotropyTextureOpenPBR(ImportGltfContext& ctx,
                               const tinygltf::Material& gm,
                               OpenPbrMaterial& m,
                               float roughness,
                               int roughnessTextureIndex,
                               const Image* roughnessImage,
                               int roughnessImageChannel,
                               const AnisotropyData& anisotropyData,
                               int anisotropyTextureIndex,
                               const Image& anisotropySrcImage,
                               std::unordered_map<std::string, int>& cache)
{
    TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                 "importAnisotropyTexture (OpenPBR) for material '%s'\n",
                 m.displayName.c_str());

    bool hasAnisotropySrcImage =
      anisotropySrcImage.width > 0 && anisotropySrcImage.height > 0 && anisotropyTextureIndex >= 0;
    bool hasRoughnessImage =
      roughnessImage != nullptr && roughnessImage->width > 0 && roughnessImage->height > 0;

    // if there is no anisotropy source image and no roughness image, then there is no anisotropy
    if (!hasAnisotropySrcImage && !hasRoughnessImage) {
        return;
    }

    std::string anisoRoughnessCacheKey =
      generateOpenPBRAnisotropyImageName("specularRoughness",
                                         roughnessTextureIndex,
                                         roughness,
                                         anisotropyTextureIndex,
                                         anisotropyData.rotation);

    std::string anisoAnisotropyCacheKey =
      generateOpenPBRAnisotropyImageName("specularRoughnessAnisotropy",
                                         roughnessTextureIndex,
                                         roughness,
                                         anisotropyTextureIndex,
                                         anisotropyData.rotation);

    // The tangent cache key is encoded differently than roughness and anisotropy because the
    // tangent image is only dependent on the anisotropy texture and rotation.
    std::string anisoTangentCacheKey = generateOpenPBRAnisotropyTangentImageName(
      "geometryTangent", anisotropyTextureIndex, anisotropyData.rotation);

    // Check if the conversion has already been done based on the cache keys.
    int usdAnisoRoughnessImageIndex = lookupTexture(cache, anisoRoughnessCacheKey);
    int usdAnisoAnisotropyImageIndex = lookupTexture(cache, anisoAnisotropyCacheKey);
    int usdAnisoTangentImageIndex = lookupTexture(cache, anisoTangentCacheKey);

    // We expect either both roughness and anisotropy images to be present or both to be absent
    // since they are derived from the same source image and parameters. If one is present without
    // the other, it indicates an error in the caching logic or some other coding error.
    if ((usdAnisoRoughnessImageIndex < 0 || usdAnisoAnisotropyImageIndex < 0) &&
        usdAnisoRoughnessImageIndex != usdAnisoAnisotropyImageIndex) {
        TF_WARN("Error: Internal error with anisotropy image caching logic");
        return;
    }

    Image anisoRoughnessImage;
    Image anisoAnisotropyImage;
    Image anisoTangentImage;

    // Get pointers to the images that need to be generated based on whether they are already in the
    // cache.
    Image* anisoRoughnessImagePtr =
      usdAnisoRoughnessImageIndex < 0 ? &anisoRoughnessImage : nullptr;
    Image* anisoAnisotropyImagePtr =
      usdAnisoAnisotropyImageIndex < 0 ? &anisoAnisotropyImage : nullptr;
    Image* anisoTangentImagePtr = usdAnisoTangentImageIndex < 0 ? &anisoTangentImage : nullptr;

    // Reserve space for new images in USD if needed to prevent vector from resizing during image
    // addition
    int numImagesToAdd = 0;
    if (anisoRoughnessImagePtr)
        ++numImagesToAdd;
    if (anisoAnisotropyImagePtr)
        ++numImagesToAdd;
    if (anisoTangentImagePtr)
        ++numImagesToAdd;
    ctx.usd->reserveImages(numImagesToAdd);

    if (hasAnisotropySrcImage) {
        if ((usdAnisoRoughnessImageIndex < 0 && usdAnisoAnisotropyImageIndex < 0) ||
            (usdAnisoTangentImageIndex < 0)) {
            bool bilinearRoughnessSampling =
              (hasRoughnessImage && (anisotropySrcImage.width != roughnessImage->width ||
                                     anisotropySrcImage.height != roughnessImage->height));

            processAnisotropyPixelsOpenPBR(anisotropySrcImage,
                                           roughness,
                                           roughnessImage,
                                           roughnessImageChannel,
                                           bilinearRoughnessSampling,
                                           anisotropyData,
                                           anisoRoughnessImagePtr,
                                           anisoAnisotropyImagePtr,
                                           anisoTangentImagePtr);
        }
    } else if (hasRoughnessImage) {
        if (usdAnisoRoughnessImageIndex < 0 && usdAnisoAnisotropyImageIndex < 0) {
            processAnisotropyPixelsFromRoughnessOpenPBR(anisotropyData,
                                                        roughnessImage,
                                                        roughnessImageChannel,
                                                        anisoRoughnessImage,
                                                        anisoAnisotropyImage);
        }
    }

    if (anisoRoughnessImagePtr && usdAnisoRoughnessImageIndex < 0) {
        usdAnisoRoughnessImageIndex =
          cacheAndWriteImage(ctx, cache, anisoRoughnessCacheKey, anisoRoughnessImage);
    }
    if (anisoAnisotropyImagePtr && usdAnisoAnisotropyImageIndex < 0) {
        usdAnisoAnisotropyImageIndex =
          cacheAndWriteImage(ctx, cache, anisoAnisotropyCacheKey, anisoAnisotropyImage);
    }

    // The tangent image is extracted from the anisotropy image's red and green channels and
    // is independent of roughness and anisotropy level, so it only needs to be generated if
    // it doesn't already exist in the cache. This allows us to avoid generating and caching
    // a tangent image if it's not needed by the material.
    if (anisoTangentImagePtr && usdAnisoTangentImageIndex < 0) {
        usdAnisoTangentImageIndex =
          cacheAndWriteImage(ctx, cache, anisoTangentCacheKey, anisoTangentImage);
    }
    if (usdAnisoRoughnessImageIndex >= 0 && usdAnisoAnisotropyImageIndex >= 0) {
        setInputImage(m.specular_roughness,
                      usdAnisoRoughnessImageIndex,
                      anisotropyData.texture.texCoord,
                      AdobeTokens->r,
                      AdobeTokens->raw);
        setInputImage(m.specular_roughness_anisotropy,
                      usdAnisoAnisotropyImageIndex,
                      anisotropyData.texture.texCoord,
                      AdobeTokens->r,
                      AdobeTokens->raw);
    }
    if (usdAnisoTangentImageIndex >= 0) {
        setInputImage(m.geometry_tangent,
                      usdAnisoTangentImageIndex,
                      anisotropyData.texture.texCoord,
                      AdobeTokens->rgb,
                      AdobeTokens->raw);
    }
}

// OpenPbrMaterial overload for importAnisotropyData
void
importAnisotropyDataOpenPBR(ImportGltfContext& ctx,
                            const tinygltf::Material& gm,
                            const tinygltf::Value& anisoExt,
                            OpenPbrMaterial& m,
                            std::unordered_map<std::string, int>& anisotropyTextureCache)
{
    TF_DEBUG_MSG(
      FILE_FORMAT_GLTF, "importAnisotropyData (OpenPBR) for material '%s'\n", m.name.c_str());

    AnisotropyData anisotropyData;

    // Read anisotropy strength value which defaults to 0.0
    readDoubleValue(anisoExt.Get("anisotropyStrength"), anisotropyData.strength);
    float strengthFactor = static_cast<float>(anisotropyData.strength);

    // If anisotropy strength is zero, ignore all anisotropy data since it will have no effect
    // on the material.
    if (strengthFactor <= 0.0f) {
        return;
    }

    readDoubleValue(anisoExt.Get("anisotropyRotation"), anisotropyData.rotation);

    // extract the roughness value or texture
    float roughness = 0.0f;
    Image roughnessSrcImage;
    int roughnessImageChannel = -1;
    int roughnessImageIndex = m.specular_roughness.image;
    if (roughnessImageIndex >= 0) {
        const ImageAsset& roughnessImageAsset = ctx.usd->images[m.specular_roughness.image];
        if (roughnessSrcImage.read(roughnessImageAsset)) {
            roughnessImageChannel = token2Channel(m.specular_roughness.channel);
            if (roughnessImageChannel >= roughnessSrcImage.channels) {
                roughnessImageChannel = -1;
                TF_WARN("Invalid roughness image channel for material '%s'", m.name.c_str());
            }
        } else {
            TF_WARN("Failed to read roughness image for material '%s'", m.name.c_str());
            return;
        }
    } else if (m.specular_roughness.value.IsHolding<float>()) {
        roughness = m.specular_roughness.value.UncheckedGet<float>();
    }

    bool hasRoughnessImage = roughnessSrcImage.width > 0 && roughnessSrcImage.height > 0 &&
                             roughnessImageChannel >= 0 && roughnessImageIndex >= 0;

    // get the gltf anisotropy texture info (if it exists) and read the image
    readTextureInfo(anisoExt.Get("anisotropyTexture"), anisotropyData.texture);

    Image anisotropySrcImage;
    Input anisotropyInput;
    int anisotropyTextureIndex = anisotropyData.texture.index;
    bool hasTexture = false;
    if (anisotropyTextureIndex > -1) {
        int imageIndex = importImage(ctx, anisotropyTextureIndex, m.name, "anisotropy");
        importTexture(ctx.gltf,
                      imageIndex,
                      anisotropyTextureIndex,
                      anisotropyData.texture.texCoord,
                      anisotropyInput,
                      AdobeTokens->rgb,
                      AdobeTokens->raw);
        importTextureTransform(gm.extensions, anisotropyInput);
        const ImageAsset& anisotropyImageAsset = ctx.usd->images[anisotropyInput.image];
        anisotropySrcImage.read(anisotropyImageAsset, anisotropySrcImage.channels);

        if (isSingleValueImage(anisotropySrcImage)) {
            anisotropyData.strength = anisotropySrcImage.pixels[2] * strengthFactor;

            anisotropyData.rotation = calculateOpenPBRImageRotation(
              anisotropySrcImage.pixels[0], anisotropySrcImage.pixels[1], anisotropyData.rotation);
        } else {
            hasTexture = true;
        }
    }

    // if there is no anisotropy texture and no roughness texture, we can convert the roughness and
    // strength values to specular roughness and anisotropy values directly without needing to
    // construct and sample from an anisotropy texture.
    if (!hasTexture && !hasRoughnessImage) {
        float strength = static_cast<float>(anisotropyData.strength);
        auto [specular_roughness, specular_roughness_anisotropy] =
          convertGltfRoughnessAnisotropyToOpenPBR(roughness, strength);

        importValue1(m.specular_roughness, specular_roughness);
        importValue1(m.specular_roughness_anisotropy, specular_roughness_anisotropy);

        if (anisotropyData.rotation != 0.0f) {
            float angle = static_cast<float>(anisotropyData.rotation);
            m.geometry_tangent.value = convertRotationToTangentOpenPBR(angle);
        }

        return;
    }

    // If there is an anisotropy texture or roughness texture, we need to process them to generate
    // OpenPBR roughness and anisotropy values
    Image* roughnessImage = hasRoughnessImage ? &roughnessSrcImage : nullptr;
    importAnisotropyTextureOpenPBR(ctx,
                                   gm,
                                   m,
                                   roughness,
                                   roughnessImageIndex,
                                   roughnessImage,
                                   roughnessImageChannel,
                                   anisotropyData,
                                   anisotropyTextureIndex,
                                   anisotropySrcImage,
                                   anisotropyTextureCache);
}

bool
convertOpenPBRAnisotropyTexturesToGltf(ExportGltfContext& ctx,
                                       tinygltf::Material& gm,
                                       Image* specularRoughnessImage,
                                       int specularRoughnessImageChannel,
                                       Image* specularRoughnessAnisotropyImage,
                                       int specularRoughnessAnisotropyImageChannel,
                                       Image* geometryTangentImage,
                                       float specularRoughnessValue,
                                       float specularRoughnessAnisotropyValue,
                                       Image& gltfAnisotropyImage,
                                       Image& newRoughnessImage,
                                       float& newRoughnessValue)
{
    if (gltfAnisotropyImage.width <= 0 || gltfAnisotropyImage.height <= 0) {
        TF_WARN("Gltf Anisotropy image has invalid dimensions (%d, %d).",
                gltfAnisotropyImage.width,
                gltfAnisotropyImage.height);
        return false;
    }

    const size_t anisotropyWidth = static_cast<size_t>(gltfAnisotropyImage.width);
    const size_t anisotropyHeight = static_cast<size_t>(gltfAnisotropyImage.height);

    if (geometryTangentImage &&
        (static_cast<size_t>(geometryTangentImage->width) != anisotropyWidth ||
         static_cast<size_t>(geometryTangentImage->height) != anisotropyHeight)) {
        TF_WARN("Coding error: Geometry tangent image size does not match anisotropy image size. "
                "Expected (%zu, "
                "%zu) but got (%d, %d). The geometry tangent image will be ignored.",
                anisotropyWidth,
                anisotropyHeight,
                geometryTangentImage->width,
                geometryTangentImage->height);
        return false;
    }

    const bool hasSameSizedTextures =
      (!specularRoughnessImage ||
       (static_cast<size_t>(specularRoughnessImage->width) == anisotropyWidth &&
        static_cast<size_t>(specularRoughnessImage->height) == anisotropyHeight)) &&
      (!specularRoughnessAnisotropyImage ||
       (static_cast<size_t>(specularRoughnessAnisotropyImage->width) == anisotropyWidth &&
        static_cast<size_t>(specularRoughnessAnisotropyImage->height) == anisotropyHeight));

    float* srcRoughnessPixels =
      specularRoughnessImage ? specularRoughnessImage->pixels.data() : nullptr;
    int srcRoughnessChannels = specularRoughnessImage ? specularRoughnessImage->channels : 0;
    float* srcAnisotropyPixels =
      specularRoughnessAnisotropyImage ? specularRoughnessAnisotropyImage->pixels.data() : nullptr;
    int srcAnisotropyChannels =
      specularRoughnessAnisotropyImage ? specularRoughnessAnisotropyImage->channels : 0;
    float* srcTangentPixels = geometryTangentImage ? geometryTangentImage->pixels.data() : nullptr;

    float* dstAnisoPixels = gltfAnisotropyImage.pixels.data();

    float specularRoughness = specularRoughnessValue;
    float specularRoughnessAnisotropy = specularRoughnessAnisotropyValue;
    float strength = 1.0f;

    float minRoughnessValue = 1.0f;
    float maxRoughnessValue = 0.0f;

    float* dstRoughnessPixels = nullptr;
    bool hasRoughnessOrAnisotropyTexture = srcRoughnessPixels || srcAnisotropyPixels;
    if (hasRoughnessOrAnisotropyTexture) {
        newRoughnessImage.allocate(anisotropyWidth, anisotropyHeight, 1);
        dstRoughnessPixels = newRoughnessImage.pixels.data();
    } else {
        // if there is no roughness or anisotropy texture, we can convert the roughness and
        // anisotropy values to a single roughness value without needing to construct and sample
        // from a roughness texture.
        auto [gltfRoughness, gltfStrength] =
          convertOpenPBRRoughnessAnisotropyToGltf(specularRoughness, specularRoughnessAnisotropy);

        // we set the min and max roughness values to the same value to indicate that the roughness
        // is constant across the image
        minRoughnessValue = maxRoughnessValue = gltfRoughness;
        strength = gltfStrength;
    }

    auto sampleDecodedFloatImage =
      [](const Image& image, size_t channel, float u, float v) -> float {
        size_t srcX =
          std::min(static_cast<size_t>(u * image.width), static_cast<size_t>(image.width) - 1);
        size_t srcY =
          std::min(static_cast<size_t>(v * image.height), static_cast<size_t>(image.height) - 1);
        return image.pixels[(srcY * image.width + srcX) * image.channels + channel];
    };

    for (size_t y = 0; y < anisotropyHeight; ++y) {
        size_t pixelIndex = y * anisotropyWidth;
        size_t dstIndex = pixelIndex * 3;
        float ncy = static_cast<float>(y) / anisotropyHeight;
        for (size_t x = 0; x < anisotropyWidth; ++x) {
            if (srcTangentPixels) {
                dstAnisoPixels[dstIndex] = srcTangentPixels[dstIndex];
                dstAnisoPixels[dstIndex + 1] = srcTangentPixels[dstIndex + 1];
            } else {
                dstAnisoPixels[dstIndex] = 1.0f;
                dstAnisoPixels[dstIndex + 1] = 0.5f;
            }

            if (hasRoughnessOrAnisotropyTexture) {
                if (hasSameSizedTextures) {
                    if (specularRoughnessImage) {
                        specularRoughness = srcRoughnessPixels[pixelIndex * srcRoughnessChannels +
                                                               specularRoughnessImageChannel];
                    }
                    if (specularRoughnessAnisotropyImage) {
                        specularRoughnessAnisotropy =
                          srcAnisotropyPixels[pixelIndex * srcAnisotropyChannels +
                                              specularRoughnessAnisotropyImageChannel];
                    }
                } else {
                    float ncx = static_cast<float>(x) / anisotropyWidth;
                    if (specularRoughnessImage) {
                        specularRoughness = sampleDecodedFloatImage(
                          *specularRoughnessImage, specularRoughnessImageChannel, ncx, ncy);
                    }

                    if (specularRoughnessAnisotropyImage) {
                        specularRoughnessAnisotropy =
                          sampleDecodedFloatImage(*specularRoughnessAnisotropyImage,
                                                  specularRoughnessAnisotropyImageChannel,
                                                  ncx,
                                                  ncy);
                    }
                }

                auto [gltfRoughness, gltfStrength] = convertOpenPBRRoughnessAnisotropyToGltf(
                  specularRoughness, specularRoughnessAnisotropy);
                strength = gltfStrength;
                dstRoughnessPixels[pixelIndex] = gltfRoughness;

                minRoughnessValue = std::min(minRoughnessValue, gltfRoughness);
                maxRoughnessValue = std::max(maxRoughnessValue, gltfRoughness);
            }

            dstAnisoPixels[dstIndex + 2] = strength;
            ++pixelIndex;
            dstIndex += 3;
        }
    }

    // if there is less that 1/255 difference in roughness values across the image, we can treat it
    // as a constant roughness value and avoid writing out a roughness texture
    const float roughnessEpsilon = 1.0f / 255.0f;
    bool hasConstantRoughness = maxRoughnessValue - minRoughnessValue <= roughnessEpsilon;
    if (hasConstantRoughness) {
        newRoughnessValue = (minRoughnessValue + maxRoughnessValue) * 0.5f;
        newRoughnessImage = Image();
    } else {
        // set newRoughnessValue to an invalid value to indicate that the roughness is not constant
        // across the image
        newRoughnessValue = -1.0f;
    }

    return true;
}

// OpenPbrMaterial overload for exportAnisotropyExtension
void
exportAnisotropyExtensionOpenPBR(
  ExportGltfContext& ctx,
  InputTranslator& inputTranslator,
  const OpenPbrMaterial& m,
  tinygltf::Material& gm,
  std::unordered_map<std::string, ExportTextureCacheItem>& constructedAnisotropyTextureCache,
  Input& newRoughnessInput)
{
    if (ctx.usd == nullptr)
        return;

    // If there is no specular roughness anisotropy and no geometry tangent, then there is no
    // anisotropy data to export, so we can return early to avoid writing out an empty
    // extension. If there is a specular roughness texture/value, it can be encoded in the
    // roughness value/texture of the glTF material
    if (m.specular_roughness_anisotropy.value.IsEmpty() &&
        m.specular_roughness_anisotropy.image < 0 && m.geometry_tangent.value.IsEmpty() &&
        m.geometry_tangent.image < 0) {
        return;
    }

    tinygltf::ExtensionMap ext;

    // capture what textures are present
    const bool hasSpecularRoughnessTexture = m.specular_roughness.image >= 0;
    const bool hasSpecularRoughnessAnisotropyTexture = m.specular_roughness_anisotropy.image >= 0;
    const bool hasGeometryTangentTexture = m.geometry_tangent.image >= 0;

    Image specularRoughnessImage;
    int specularRoughnessImageChannel = -1;
    if (hasSpecularRoughnessTexture) {
        specularRoughnessImage = inputTranslator.getDecodedImage(m.specular_roughness.image).second;
        if (specularRoughnessImage.width <= 0 || specularRoughnessImage.height <= 0) {
            TF_WARN("Error obtaining specular roughness image.");
            return;
        }
        specularRoughnessImageChannel = token2Channel(m.specular_roughness.channel);
        if (specularRoughnessImageChannel < 0 ||
            specularRoughnessImageChannel >= specularRoughnessImage.channels) {
            TF_WARN("Invalid specular roughness image channel for material '%s'", m.name.c_str());
            return;
        }
    }

    Image specularRoughnessAnisotropyImage;
    int specularRoughnessAnisotropyImageChannel = -1;
    if (hasSpecularRoughnessAnisotropyTexture) {
        specularRoughnessAnisotropyImage =
          inputTranslator.getDecodedImage(m.specular_roughness_anisotropy.image).second;
        if (specularRoughnessAnisotropyImage.width <= 0 ||
            specularRoughnessAnisotropyImage.height <= 0) {
            TF_WARN("Error obtaining specular roughness anisotropy image.");
            return;
        }
        specularRoughnessAnisotropyImageChannel =
          token2Channel(m.specular_roughness_anisotropy.channel);
        if (specularRoughnessAnisotropyImageChannel < 0 ||
            specularRoughnessAnisotropyImageChannel >= specularRoughnessAnisotropyImage.channels) {
            TF_WARN("Invalid specular roughness anisotropy image channel for material '%s'",
                    m.name.c_str());
            return;
        }
    }

    Image geometryTangentImage;
    if (hasGeometryTangentTexture) {
        geometryTangentImage = inputTranslator.getDecodedImage(m.geometry_tangent.image).second;
        if (geometryTangentImage.width <= 0 || geometryTangentImage.height <= 0) {
            TF_WARN("Error obtaining geometry tangent image.");
            return;
        }
        if (geometryTangentImage.channels < 3) {
            TF_WARN("Geometry tangent image must have at least 3 channels for material '%s'",
                    m.name.c_str());
            return;
        }
    }

    // extract the fixed specular roughness and specular roughness anisotropy values from the
    // material (if present)
    const float specularRoughnessValue = m.specular_roughness.value.IsHolding<float>()
                                           ? m.specular_roughness.value.UncheckedGet<float>()
                                           : 0.0f;
    const float specularRoughnessAnisotropyValue =
      m.specular_roughness_anisotropy.value.IsHolding<float>()
        ? m.specular_roughness_anisotropy.value.UncheckedGet<float>()
        : 0.0f;

    // extract the anisotropy rotation from the geometry tangent if it exists
    float gltfAnisotropyRotation = 0.0f;
    if (!hasGeometryTangentTexture && m.geometry_tangent.value.IsHolding<GfVec3f>()) {
        GfVec3f tangentValue = m.geometry_tangent.value.UncheckedGet<GfVec3f>();
        gltfAnisotropyRotation =
          calculateOpenPBRImageRotation(tangentValue[0], tangentValue[1], 0.0f);
    }

    if (gltfAnisotropyRotation != 0.0f) {
        addRotationToAnisotropyExt(ext, gltfAnisotropyRotation);
    }

    // handle simple case where specular_roughness, specular_roughness_anisotropy and
    // geometry_tangent are values (no textures)
    if (!hasSpecularRoughnessTexture && !hasSpecularRoughnessAnisotropyTexture &&
        !hasGeometryTangentTexture) {
        auto [gltfRoughness, gltfStrength] = convertOpenPBRRoughnessAnisotropyToGltf(
          specularRoughnessValue, specularRoughnessAnisotropyValue);
        gm.pbrMetallicRoughness.roughnessFactor = gltfRoughness;
        if (gltfStrength != 0.0f) {
            addStrengthToAnisotropyExt(ext, gltfStrength);
        }

        addMaterialExt(ctx, gm, "KHR_materials_anisotropy", ext);
        return;
    }

    // handle the complex case where one or more of specular_roughness,
    // specular_roughness_anisotropy and geometry_tangent are textures.

    // Prefer the geometry tangent texture's resolution for the anisotropy texture since it
    // encodes the anisotropy rotation which is the most visually impactful aspect of
    // anisotropy. Another reason is that it's more complicated to sample the geometry tangent
    // texture. If there is no geometry tangent texture, use the maximum resolution between the
    // roughness and anisotropy textures to preserve as much detail as possible in both maps.
    int anisotropyWidth =
      hasGeometryTangentTexture
        ? geometryTangentImage.width
        : std::max(specularRoughnessImage.width, specularRoughnessAnisotropyImage.width);
    int anisotropyHeight =
      hasGeometryTangentTexture
        ? geometryTangentImage.height
        : std::max(specularRoughnessImage.height, specularRoughnessAnisotropyImage.height);
    if (anisotropyWidth <= 0 || anisotropyHeight <= 0) {
        TF_WARN("Error obtaining anisotropy images.");
        return;
    }

    // generate a cache key for the constructed anisotropy texture based on the parameters that
    // affect its construction. This is used to avoid redundant construction and export of the same
    // anisotropy texture for materials that share the same anisotropy parameters and source
    // textures.
    std::stringstream textureKeyStream;
    textureKeyStream << std::fixed << std::setprecision(6) << m.specular_roughness.image << "_"
                     << m.specular_roughness_anisotropy.image << "_" << m.geometry_tangent.image
                     << "_" << specularRoughnessValue << "_" << specularRoughnessAnisotropyValue
                     << "_" << gltfAnisotropyRotation;
    std::string textureKeySuffix = textureKeyStream.str();

    std::string anisotropyTextureName = "anisotropyTexture_" + textureKeySuffix;
    std::string anisotropyTextureUri = anisotropyTextureName + ".png";
    ExportTextureCacheItem& constructedAnisotropyTextureCacheItem =
      constructedAnisotropyTextureCache[anisotropyTextureUri];

    int textureIndex = -1;
    int texCoord = -1;
    if (constructedAnisotropyTextureCacheItem.textureIndex < 0) {
        // if (constructedAnisotropyInput.image < 0) {
        //  create gltf anisotropyTexture
        Image gltfAnisotropyImage;
        gltfAnisotropyImage.allocate(anisotropyWidth, anisotropyHeight, 3);
        Image newRoughnessImage;
        Input constructedAnisotropyInput;
        float newRoughnessValue = -1.0f;

        if (convertOpenPBRAnisotropyTexturesToGltf(
              ctx,
              gm,
              hasSpecularRoughnessTexture ? &specularRoughnessImage : nullptr,
              specularRoughnessImageChannel,
              hasSpecularRoughnessAnisotropyTexture ? &specularRoughnessAnisotropyImage : nullptr,
              specularRoughnessAnisotropyImageChannel,
              hasGeometryTangentTexture ? &geometryTangentImage : nullptr,
              specularRoughnessValue,
              specularRoughnessAnisotropyValue,
              gltfAnisotropyImage,
              newRoughnessImage,
              newRoughnessValue)) {
            constructedAnisotropyInput.image =
              inputTranslator.addImage(std::move(gltfAnisotropyImage),
                                       anisotropyTextureName,
                                       anisotropyTextureUri,
                                       ImageFormatPng,
                                       false);
        } else {
            TF_WARN("Error constructing anisotropy texture for material '%s'.",
                    m.displayName.c_str());
            return;
        }

        exportTexture(ctx, constructedAnisotropyInput, textureIndex, texCoord);
        if (textureIndex != -1 && texCoord != -1) {
            constructedAnisotropyTextureCacheItem.textureIndex = textureIndex;
            constructedAnisotropyTextureCacheItem.texCoord = texCoord;
        }

        if (newRoughnessImage.width > 0 && newRoughnessImage.height > 0) {
            std::string roughnessTextureName = "roughnessTexture_" + textureKeySuffix;
            std::string roughnessTextureUri = roughnessTextureName + ".png";
            newRoughnessInput = m.specular_roughness;
            newRoughnessInput.image = inputTranslator.addImage(std::move(newRoughnessImage),
                                                               roughnessTextureName,
                                                               roughnessTextureUri,
                                                               ImageFormatPng,
                                                               true);
            newRoughnessInput.channel = AdobeTokens->r;
        } else if (newRoughnessValue >= 0.0f) {
            newRoughnessInput.value = VtValue(newRoughnessValue);
        }
    } else {
        textureIndex = constructedAnisotropyTextureCacheItem.textureIndex;
        texCoord = constructedAnisotropyTextureCacheItem.texCoord;
    }

    if (textureIndex != -1 && texCoord != -1) {
        addTextureToAnisotropyExt(ext, textureIndex, texCoord);
    }

    // Since we have a texture, we need to set the strength factor to the extension to 1.0.
    // Othersize, the default value of 0.0 would be used and would cause the anisotropy to be
    // disabled.

    addStrengthToAnisotropyExt(ext, 1.0f);

    addMaterialExt(ctx, gm, "KHR_materials_anisotropy", ext);
}

} // end namespace adobe::usd
