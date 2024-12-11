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
#include "gltfAnisotropy.h"
#include "debugCodes.h"
#include "gltfImport.h"
#include <cmath>
#include <iomanip>
#include <sstream>

#include <fileformatutils/common.h>

using namespace PXR_NS;

namespace adobe::usd {

constexpr double PI = 3.14159265358979311600;

// Anisotropy textures can be 4x4 representing a single strength and rotation
constexpr size_t SINGLE_VALUE_IMAGE_DIM_SIZE = 4;

// Returns true if the image is a 4x4 containing a single anisotropy entry
bool
isSingleValueImage(const Image& image)
{
    return image.width == SINGLE_VALUE_IMAGE_DIM_SIZE &&
           image.height == SINGLE_VALUE_IMAGE_DIM_SIZE;
}

// Calculates the ASM anisotropy level based on strength and roughness.
float
calculateASMLevel(float strength, float roughness)
{
    float s2 = strength * strength;
    return std::sqrt(std::sqrt((1.0f - roughness * roughness) * s2));
}

// Reverses the anisotropy strength calculation
float
reverseASMLevel(float anisoLevel, float anisScale, float roughness)
{
    if (roughness > 1.0f) {
        // disabling this log for now to prevent spam from bad roughness textures
        // TF_WARN("Roughness is too high; cannot reverse calculate strength.");
        return 0.0f;
    }
    float denominator = 1.0f - roughness * roughness;
    if (denominator <= 0.0f) {
        return 0.0f;
    }
    float strengthSquared = std::pow(anisoLevel, 4) / denominator;
    return std::sqrt(strengthSquared) / anisScale;
}

// Calculate the ASM anisotropy rotation
float
calculateASMRotation(float angle)
{
    // Normalize the angle to [0, 1)
    float normalized_angle = angle / (2.0f * PI);
    normalized_angle -= std::floor(normalized_angle); // Ensure it's within [0, 1)
    return normalized_angle;
}

// Calculates the normalized ASM anisotropy angle from red and green channel values.
float
calculateASMImageRotation(float redChannelValue, float greenChannelValue, float rotation)
{
    // Convert channel values from [0, 1] to [-1, 1]
    GfVec2f vec(redChannelValue * 2.0f - 1.0f, greenChannelValue * 2.0f - 1.0f);

    // Calculate the angle in radians and apply rotation
    float angle = std::atan2(vec[1], vec[0]) + rotation;
    float normalized_angle = calculateASMRotation(angle);
    return normalized_angle;
}

// Reverses the normalization and rotation to retrieve the original angle in radians.
float
reverseASMRotation(float normalized_angle, float rotation)
{
    float angle = normalized_angle * (2.0f * PI);
    float original_angle = angle - rotation;
    original_angle = std::fmod(original_angle, 2.0f * PI);
    if (original_angle < 0.0f) {
        original_angle += 2.0f * PI;
    }

    return original_angle;
}

// Reverses the calculation of normalized_angle to obtain red and green channel values.
void
reverseCalculateASMImageRotation(float normalized_angle,
                                 float rotation,
                                 float& redChannelValue,
                                 float& greenChannelValue)
{
    float original_angle = reverseASMRotation(normalized_angle, rotation);

    // Convert angle back to vector components
    float x = std::cos(original_angle);
    float y = std::sin(original_angle);
    redChannelValue = (x + 1.0f) / 2.0f;
    greenChannelValue = (y + 1.0f) / 2.0f;
}

// Generates a unique name for the anisotropy image based on prefix, level, and rotation.
std::string
generateAnisotropyImageName(const std::string& prefix, float level, float rotation)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << prefix << "_" << level << "_" << rotation;
    std::string result = ss.str();
    std::replace(result.begin(), result.end(), '.', '_');
    return result;
}

// Extracts the level and rotation values from a formatted anisotropy image name.
bool
extractAnisotropyParamsFromName(const std::string& name, float& level, float& rotation)
{
    level = -1.0f;
    rotation = -1.0f;
    std::stringstream ss(name);
    std::string segment;
    std::vector<std::string> tokens;

    // Split the string by '_'
    while (std::getline(ss, segment, '_')) {
        tokens.emplace_back(segment);
    }
    if (tokens.size() < 5) {
        TF_WARN("Error: Input string does not contain enough segments.");
        return false;
    }
    std::string levelPart1 = tokens[tokens.size() - 4];
    std::string levelPart2 = tokens[tokens.size() - 3];
    std::string rotationPart1 = tokens[tokens.size() - 2];
    std::string rotationPart2 = tokens[tokens.size() - 1];
    std::string levelStr = levelPart1 + "." + levelPart2;
    std::string rotationStr = rotationPart1 + "." + rotationPart2;
    try {
        level = std::stof(levelStr);
    } catch (const std::exception& e) {
        TF_WARN("Error: Failed to convert level string to float. Exception: %s", e.what());
        return false;
    }
    try {
        rotation = std::stof(rotationStr);
    } catch (const std::exception& e) {
        TF_WARN("Error: Failed to convert rotation string to float. Exception: %s", e.what());
        return false;
    }
    return true;
}

// Caches an image by writing it and updating the cache map.
int
cacheAndWriteImage(ImportGltfContext& ctx,
                   std::unordered_map<std::string, int>& cache,
                   const std::string& key,
                   const Image& image)
{
    auto [newIndex, usdImage] = ctx.usd->addImage();
    usdImage.name = key;
    usdImage.uri = key + ".png";
    usdImage.format = ImageFormatPng;

    if (!image.write(usdImage)) {
        TF_WARN("Failed to write anisotropy image: %s", key.c_str());
    }

    cache[key] = newIndex;
    imageWrite(usdImage, "test/" + usdImage.uri, true);

    return newIndex;
}

// Extracts the roughness value from a roughness image.
float
extractRoughness(const tinygltf::Image* roughnessImage,
                 bool bilinearRoughnessSampling,
                 float ncx,
                 float ncy,
                 bool normalize = false)
{
    if (roughnessImage) {
        if (bilinearRoughnessSampling) {
            return sampleBilinear(roughnessImage, ncx, ncy, 0);
        } else {
            // Perform nearest-neighbor sampling to retrieve roughness
            size_t linearX = static_cast<size_t>(ncx * roughnessImage->width);
            size_t linearY = static_cast<size_t>(ncy * roughnessImage->height);
            size_t linearIndex =
              (linearY * roughnessImage->width + linearX) * roughnessImage->component;
            if (linearIndex >= roughnessImage->image.size()) {
                TF_WARN("Linear index out of bounds in roughness image.");
                return 0.0f;
            }

            float roughness = static_cast<float>(roughnessImage->image[linearIndex]);
            if (normalize) {
                roughness /= MAX_COLOR_VALUE;
            }
            return roughness;
        }
    } else {
        TF_WARN("Roughness image is null.");
        return 0.0f;
    }
}

// Processes anisotropy pixels and populates anisotropy level and angle images.
void
processAnisotropyPixels(const Image& anisotropyImage,
                        const tinygltf::Image* roughnessImage,
                        float roughness,
                        bool bilinearRoughnessSampling,
                        const AnisotropyData& anisotropyData,
                        Image& anisoLevelImage,
                        Image& anisoAngleImage)
{
    anisoLevelImage.allocate(anisotropyImage.width, anisotropyImage.height, 1);
    float* anisoLevelPixels = anisoLevelImage.pixels.data();
    anisoAngleImage.allocate(anisotropyImage.width, anisotropyImage.height, 1);
    float* anisoAnglePixels = anisoAngleImage.pixels.data();

    for (size_t y = 0; y < anisotropyImage.height; ++y) {
        float ncy = static_cast<float>(y) / anisotropyImage.height;
        for (size_t x = 0; x < anisotropyImage.width; ++x) {
            float ncx = static_cast<float>(x) / anisotropyImage.width;
            size_t index = (y * anisotropyImage.width + x) * anisotropyImage.channels;
            float redChannelValue = anisotropyImage.pixels[index];
            float greenChannelValue = anisotropyImage.pixels[index + 1];
            float blueChannelValue = anisotropyImage.pixels[index + 2];

            if (roughnessImage) {
                roughness =
                  extractRoughness(roughnessImage, bilinearRoughnessSampling, ncx, ncy, false);
            }

            // Calculate and set anisotropy level for every pixel
            anisoLevelPixels[y * anisotropyImage.width + x] =
              calculateASMLevel(blueChannelValue * anisotropyData.strength, roughness);

            // Calculate and set anisotropy rotation for every pixel
            float normalized_angle = calculateASMImageRotation(
              redChannelValue, greenChannelValue, anisotropyData.rotation);
            anisoAnglePixels[y * anisotropyImage.width + x] = normalized_angle;
        }
    }
}

// Processes anisotropy pixels with roughness, populating the anisotropy level
void
processAnisotropyPixelsFromRoughness(const AnisotropyData& anisotropyData,
                                     const tinygltf::Image* roughnessImage,
                                     bool bilinearRoughnessSampling,
                                     Image& anisoLevelImage)
{
    anisoLevelImage.allocate(roughnessImage->width, roughnessImage->height, 1);
    float* anisoLevelPixels = anisoLevelImage.pixels.data();
    for (size_t y = 0; y < roughnessImage->height; ++y) {
        float ncy = static_cast<float>(y) / roughnessImage->height;
        for (size_t x = 0; x < roughnessImage->width; ++x) {
            float ncx = static_cast<float>(x) / roughnessImage->width;
            float roughness = extractRoughness(roughnessImage, bilinearRoughnessSampling, ncx, ncy);
            anisoLevelPixels[y * roughnessImage->width + x] =
              calculateASMLevel(anisotropyData.strength, roughness);
        }
    }
}

// Gathers the anisotropy data from a glTF material and imports non image ASM values.
bool
importAnisotropyData(ImportGltfContext& ctx,
                     const tinygltf::ExtensionMap& extensions,
                     const tinygltf::Value& anisoExt,
                     Material& m,
                     float roughness,
                     AnisotropyData& anisotropy,
                     Image& anisotropySrcImage)
{
    bool ret = false;
    bool haveStrength = readDoubleValue(anisoExt.Get("anisotropyStrength"), anisotropy.strength);
    bool haveRotation = readDoubleValue(anisoExt.Get("anisotropyRotation"), anisotropy.rotation);
    readTextureInfo(anisoExt.Get("anisotropyTexture"), anisotropy.texture);
    Input anisotropyInput;
    if (anisotropy.texture.index > -1) {
        int imageIndex = importImage(ctx, anisotropy.texture.index, m.name, "anisotropy");
        importTexture(ctx.gltf,
                      imageIndex,
                      anisotropy.texture.index,
                      anisotropy.texture.texCoord,
                      anisotropyInput,
                      AdobeTokens->rgb,
                      AdobeTokens->raw);
        importTextureTransform(extensions, anisotropyInput);
        const ImageAsset& anisotropyImageAsset = ctx.usd->images[anisotropyInput.image];
        anisotropySrcImage.read(anisotropyImageAsset, anisotropySrcImage.channels);

        if (isSingleValueImage(anisotropySrcImage)) {
            if (!haveStrength) {
                anisotropy.strength = anisotropySrcImage.pixels[2];
            }

            float normalized_angle = calculateASMImageRotation(
              anisotropySrcImage.pixels[0], anisotropySrcImage.pixels[1], anisotropy.rotation);
            anisotropy.rotation = normalized_angle;
        } else {
            ret = true;
        }
    }

    float anisoLevel = calculateASMLevel(anisotropy.strength, roughness);
    importValue1(m.anisotropyLevel, anisoLevel);
    float asmRotation = calculateASMRotation(anisotropy.rotation);
    importValue1(m.anisotropyAngle, asmRotation);
    return ret;
}

// Imports anisotropy textures from a glTF material and updates the USD material.
void
importAnisotropyTexture(ImportGltfContext& ctx,
                        const tinygltf::Material& gm,
                        Material& m,
                        float roughness,
                        const AnisotropyData& anisotropyData,
                        const Image& anisotropySrcImage,
                        std::unordered_map<std::string, int>& cache)
{
    // Get Roughness image
    const tinygltf::Image* roughnessImage = nullptr;
    if (gm.pbrMetallicRoughness.metallicRoughnessTexture.index < ctx.gltf->textures.size()) {
        roughnessImage = getImage(ctx.gltf, gm.pbrMetallicRoughness.metallicRoughnessTexture.index);
    }

    // Check if the anisotropy textures are already in the cache
    std::string levelCacheKey = "";
    std::string angleCacheKey = "";
    Image anisoLevelImage;
    Image anisoAngleImage;
    if (anisotropyData.texture.index >= 0) {
        levelCacheKey = generateAnisotropyImageName(AdobeTokens->anisotropyLevelTexture.GetText(),
                                                    anisotropyData.strength,
                                                    anisotropyData.rotation);
        angleCacheKey = generateAnisotropyImageName(AdobeTokens->anisotropyAngleTexture.GetText(),
                                                    anisotropyData.strength,
                                                    anisotropyData.rotation);
    } else if (gm.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
        std::string roughnessPrefix = "_roughness";
        levelCacheKey = generateAnisotropyImageName(AdobeTokens->anisotropyLevelTexture.GetText() +
                                                      roughnessPrefix,
                                                    anisotropyData.strength,
                                                    anisotropyData.rotation);
    }
    int usdAnisoLevelImageIndex = lookupTexture(cache, levelCacheKey);
    int usdAnisoAngleImageIndex = lookupTexture(cache, angleCacheKey);

    bool bilinearRoughnessSampling =
      (roughnessImage && (anisotropySrcImage.width != roughnessImage->width ||
                          anisotropySrcImage.height != roughnessImage->height));

    // Check if we can and need to import the anisotropy textures
    if (anisotropySrcImage.width > 0 && anisotropySrcImage.height > 0) {
        if (usdAnisoLevelImageIndex < 0 && usdAnisoAngleImageIndex < 0) {
            processAnisotropyPixels(anisotropySrcImage,
                                    roughnessImage,
                                    bilinearRoughnessSampling,
                                    roughness,
                                    anisotropyData,
                                    anisoLevelImage,
                                    anisoAngleImage);

            // Not a big fan of this:  Reserve here so the second addImage doesn't invalidate
            // the first ImageAsset because the vector had to be moved
            ctx.usd->reserveImages(2);
            usdAnisoLevelImageIndex =
              cacheAndWriteImage(ctx, cache, levelCacheKey, anisoLevelImage);
            usdAnisoAngleImageIndex =
              cacheAndWriteImage(ctx, cache, angleCacheKey, anisoAngleImage);
        }

        Input levelTextureInput, angleTextureInput;
        setInputImage(m.anisotropyLevel,
                      usdAnisoLevelImageIndex,
                      anisotropyData.texture.texCoord,
                      AdobeTokens->rgb,
                      AdobeTokens->raw);
        setInputImage(m.anisotropyAngle,
                      usdAnisoAngleImageIndex,
                      anisotropyData.texture.texCoord,
                      AdobeTokens->rgb,
                      AdobeTokens->raw);
    } else if (roughnessImage != nullptr && roughnessImage->width > 0 &&
               roughnessImage->height > 0) {
        if (usdAnisoLevelImageIndex < 0) {
            // Case where there is no anisotropy image but anisotropy strength and roughness
            // image are present
            processAnisotropyPixelsFromRoughness(
              anisotropyData, roughnessImage, bilinearRoughnessSampling, anisoLevelImage);
            usdAnisoLevelImageIndex =
              cacheAndWriteImage(ctx, cache, levelCacheKey, anisoLevelImage);
        }
        Input levelTextureInput;
        setInputImage(m.anisotropyLevel,
                      usdAnisoLevelImageIndex,
                      anisotropyData.texture.texCoord,
                      AdobeTokens->rgb,
                      AdobeTokens->raw);
    }
}

// Constructs an anisotropy image by combining level and angle images, considering roughness.
void
constructAnisotropyImage(const Material& m,
                         const Image& levelImage,
                         const Image& angleImage,
                         float anisScale,
                         float anisRotation,
                         const tinygltf::Image* roughnessImage,
                         Image& constructedAnisotropyImage)
{
    int width = std::max(levelImage.width, angleImage.width);
    int height = std::max(levelImage.height, angleImage.height);

    // Check if the roughness image has a different resolution
    bool needsRoughnessResample =
      (roughnessImage != nullptr) && ((levelImage.width != roughnessImage->width) ||
                                      (levelImage.height != roughnessImage->height));

    size_t numPixels = width * height;
    size_t numChannels = constructedAnisotropyImage.channels;
    constructedAnisotropyImage.allocate(width, height, numChannels);

    for (size_t i = 0; i < numPixels; ++i) {
        size_t idxDst = i * numChannels;

        // Calculate normalized coordinates for bilinear sampling
        float roughness = 0.0f;
        if (roughnessImage != nullptr) {
            if (needsRoughnessResample) {
                float u = (i % levelImage.width) / static_cast<float>(levelImage.width);
                float v = (i / levelImage.width) / static_cast<float>(levelImage.height);
                roughness = sampleBilinear(roughnessImage, u, v, 0);
            } else {
                roughness = static_cast<float>(roughnessImage->image[i]) / MAX_COLOR_VALUE;
            }
        } else {
            if (m.roughness.value.IsHolding<float>()) {
                roughness = m.roughness.value.Get<float>();
            }
        }

        // Set anisotropy level (blue channel)
        constructedAnisotropyImage.pixels[idxDst + 2] =
          reverseASMLevel(levelImage.pixels[i], anisScale, roughness);

        // Reconstruct the red and green channels (from angle)
        reverseCalculateASMImageRotation(angleImage.pixels[i],
                                         anisRotation,
                                         constructedAnisotropyImage.pixels[idxDst + 0],
                                         constructedAnisotropyImage.pixels[idxDst + 1]);
    }
}

// Exports the anisotropy extension to a glTF material.
void
exportAnisotropyExtension(ExportGltfContext& ctx,
                          InputTranslator& inputTranslator,
                          const Material& m,
                          tinygltf::Material& gm,
                          std::unordered_map<std::string, Input>& constructedAnisotropyCache)
{
    if (ctx.usd != nullptr) {
        if (m.anisotropyLevel.value.IsEmpty() && m.anisotropyLevel.image < 0) {
            if (m.anisotropyAngle.value.IsEmpty() && m.anisotropyAngle.image < 0) {
                // Return if there is no anisotropy data so as to not write out an empty extension
                return;
            }
        }
        float reconstructedStrength = 1.0f;
        float reconstructedAngle = 0.0f;
        tinygltf::ExtensionMap ext;
        if (m.anisotropyLevel.value.IsHolding<float>()) {
            // Use default roughness if none is available
            float roughness =
              m.roughness.value.IsHolding<float>() ? m.roughness.value.UncheckedGet<float>() : 0.0f;
            reconstructedStrength =
              reverseASMLevel(m.anisotropyLevel.value.UncheckedGet<float>(), 1.0f, roughness);
            addFloatValueToExt(ext, "anisotropyStrength", reconstructedStrength);
        }

        if (m.anisotropyAngle.value.IsHolding<float>()) {
            reconstructedAngle =
              reverseASMRotation(m.anisotropyAngle.value.UncheckedGet<float>(), 0.0f);
            addFloatValueToExt(ext, "anisotropyRotation", reconstructedAngle);
        }

        if (m.anisotropyLevel.image >= 0 || m.anisotropyAngle.image >= 0) {
            std::string anisLevelName = inputTranslator.getImageSourceName(m.anisotropyLevel.image);
            if (extractAnisotropyParamsFromName(
                  anisLevelName, reconstructedStrength, reconstructedAngle)) {
                addFloatValueToExt(ext, "anisotropyStrength", reconstructedStrength);
                addFloatValueToExt(ext, "anisotropyRotation", reconstructedAngle);
            }
            std::string constructedTextureName = "anisotropyTexture_" +
                                                 std::to_string(m.anisotropyLevel.image) + "_" +
                                                 std::to_string(m.anisotropyAngle.image);
            Input& constructedAnisotropyInput = constructedAnisotropyCache[constructedTextureName];
            if (constructedAnisotropyInput.image < 0) {
                Image constructedImage;
                constructedImage.channels = 3;

                // Decode level and angle images
                auto decodedLevel = inputTranslator.getDecodedImage(m.anisotropyLevel.image);
                auto decodedAngle = inputTranslator.getDecodedImage(m.anisotropyAngle.image);
                Image anisotropyLevelImage = decodedLevel.second;
                Image anisotropyAngleImage = decodedAngle.second;

                const tinygltf::Image* roughnessImage = nullptr;
                if (gm.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0 &&
                    gm.pbrMetallicRoughness.metallicRoughnessTexture.index <
                      ctx.gltf->textures.size()) {
                    roughnessImage =
                      getImage(ctx.gltf, gm.pbrMetallicRoughness.metallicRoughnessTexture.index);
                }

                constructAnisotropyImage(m,
                                         anisotropyLevelImage,
                                         anisotropyAngleImage,
                                         reconstructedStrength,
                                         reconstructedAngle,
                                         roughnessImage,
                                         constructedImage);
                constructedAnisotropyInput.image = inputTranslator.addImage(
                  std::move(constructedImage), constructedTextureName, ImageFormatPng, false);
                int textureIndex = -1;
                int texCoord = -1;
                exportTexture(ctx, constructedAnisotropyInput, textureIndex, texCoord);
                if (textureIndex != -1) {
                    std::map<std::string, tinygltf::Value> textureInfo;
                    textureInfo["index"] = tinygltf::Value(constructedAnisotropyInput.image);
                    if (texCoord != 0) {
                        textureInfo["texCoord"] = tinygltf::Value(texCoord);
                    }
                    ext["anisotropyTexture"] = tinygltf::Value(textureInfo);
                }
            } else {
                if (constructedAnisotropyInput.image > -1) {
                    std::map<std::string, tinygltf::Value> textureInfo;
                    textureInfo["index"] = tinygltf::Value(constructedAnisotropyInput.image);
                    if (constructedAnisotropyInput.uvIndex != 0) {
                        textureInfo["texCoord"] =
                          tinygltf::Value(constructedAnisotropyInput.uvIndex);
                    }
                    ext["anisotropyTexture"] = tinygltf::Value(textureInfo);
                }
            }
        }
        addMaterialExt(ctx, gm, "KHR_materials_anisotropy", ext);
    }
}

} // end namespace adobe::usd
