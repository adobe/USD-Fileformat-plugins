/*
Copyright 2023 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include "gltfSpecGloss.h"
#include "debugCodes.h"
#include "gltfImport.h"
#include "importGltfContext.h"
#include <fileformatutils/common.h>
#include <fileformatutils/images.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>

#include <algorithm>
#include <cmath>

using namespace PXR_NS;

namespace adobe::usd {

namespace { // anonymous

inline float
_clamp01(float value)
{
    return std::min(1.0f, std::max(0.0f, value));
}

inline float
_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float
_getBrightness(const float* color)
{
    return std::sqrt(0.299f * color[0] * color[0] + 0.587f * color[1] * color[1] +
                     0.114f * color[2] * color[2]);
}

constexpr float dielectricSpecularReflectance = 0.04f;

bool
_hasMetalness(const float* specular)
{
    float specularBrightness = _getBrightness(specular);
    return specularBrightness >= dielectricSpecularReflectance;
}

float
_solveMetallic(const float* diffuse, const float* specular, float oneMinusSpecularStrength)
{
    // The formula to compute the metallic value was taken from
    // https://learn.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping
    float specularBrightness = _getBrightness(specular);
    if (specularBrightness < dielectricSpecularReflectance) {
        return 0.0f;
    }
    float diffuseBrightness = _getBrightness(diffuse);
    float a = dielectricSpecularReflectance;
    float b =
      diffuseBrightness * oneMinusSpecularStrength / (1.0f - dielectricSpecularReflectance) +
      specularBrightness - 2.0f * dielectricSpecularReflectance;
    float c = dielectricSpecularReflectance - specularBrightness;
    float D = std::max(0.0f, b * b - 4.0f * a * c);
    float d = std::sqrt(D);
    float v = (-b + d) / (2.0f * a);
    return _clamp01(v);
}

void
_convertToMetallicRoughness(const float* diffuse,
                            const float* specular,
                            float* newDiffuse,
                            float* metallic)
{
    // Use formala defined by "KHR_materials_pbrSpecularGlossiness" spec for converting
    // from SpecularGlossiness to MetallicRoughness.
    // The formula was taken from the PbrUtilities.ConvertToMetallicRoughness Javascript
    // function found in
    // https://kcoley.github.io/glTF/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness/examples/convert-between-workflows-bjs/
    // and also documented in
    // https://learn.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping

    constexpr float epsilon = 1e-4f;
    float specularStrength = std::max({ specular[0], specular[1], specular[2] });
    float oneMinusSpecularStrength = 1.0f - specularStrength;
    float m = _solveMetallic(diffuse, specular, oneMinusSpecularStrength);

    float diffuseScale = oneMinusSpecularStrength / (1.0f - dielectricSpecularReflectance) /
                         std::max(epsilon, 1.0f - m);
    float specularDiff = dielectricSpecularReflectance * (1.0f - m);
    float specularScale = 1.0f / std::max(epsilon, m);

    float m2 = m * m; // metallic squared
    *metallic = m;
    newDiffuse[0] =
      _clamp01(_lerp(diffuse[0] * diffuseScale, (specular[0] - specularDiff) * specularScale, m2));
    newDiffuse[1] =
      _clamp01(_lerp(diffuse[1] * diffuseScale, (specular[1] - specularDiff) * specularScale, m2));
    newDiffuse[2] =
      _clamp01(_lerp(diffuse[2] * diffuseScale, (specular[2] - specularDiff) * specularScale, m2));
}

std::string
_toHexString(unsigned int val)
{
    std::ostringstream oss;
    oss << std::hex << val;
    return oss.str();
}

std::string
_genImageName(const std::string& basename, int image1, int image2)
{
    return basename + "-" + _toHexString(image1) + "-" + _toHexString(image2);
}

// This function expects one of the diffuse or specular images to be non empty and if both are
// present, they should be the same size. It computes the new diffuse and metallic-roughness images
// using the specular-glossiness to metallic-roughness conversion function for each pixel.
void
_convertSpecularGlossToMetalicRough(Image& diffuseSrcImage,       // image is in srgb space
                                    GfVec4f& diffuseFactor,       // factors are in linear space
                                    Image& specularSrcImage,      // image is in srgb space
                                    GfVec4f& specularGlossFactor, // factors are in linear space
                                    Image& diffuseDstImage,
                                    Image& mrDstImage,
                                    bool& hasTransparency)
{
    const bool hasDiffuseTexture = !diffuseSrcImage.isEmpty();
    const bool hasSpecularTexture = !specularSrcImage.isEmpty();

    if (!hasDiffuseTexture && !hasSpecularTexture) {
        TF_CODING_ERROR("Expecting one diffuse or specular images to be non empty");
        return;
    }

    // check the texture sizes match
    if (hasDiffuseTexture && hasSpecularTexture &&
        (diffuseSrcImage.width != specularSrcImage.width ||
         diffuseSrcImage.height != specularSrcImage.height)) {
        TF_CODING_ERROR("Diffuse and specular textures are expected to be the same size");
        return;
    }

    const int width = hasDiffuseTexture ? diffuseSrcImage.width : specularSrcImage.width;
    const int height = hasDiffuseTexture ? diffuseSrcImage.height : specularSrcImage.height;

    const bool srcHasAlpha = diffuseSrcImage.channels == 4;

    // If there is constant unit opactiy, we only need 3 channels for the diffuse dst image
    bool dstHasAlpha = srcHasAlpha || diffuseFactor[3] != 1.0f;
    const int dstImageChannels = dstHasAlpha ? 4 : 3;

    diffuseDstImage.allocate(width, height, dstImageChannels);
    mrDstImage.allocate(width, height, 3);

    // Get pointers to the src and dest pixels

    float* diffuseSrc = hasDiffuseTexture ? diffuseSrcImage.pixels.data() : diffuseFactor.data();
    const int diffuseSrcStep = hasDiffuseTexture ? diffuseSrcImage.channels : 0;

    // if there is const opacity, we set the src opacity pointer to a single value
    // and set the src opacity step to 0
    float constOpacityValue = diffuseFactor[3];
    float* opacitySrc = srcHasAlpha ? diffuseSrc + 3 : &constOpacityValue;
    const int opacitySrcStep = srcHasAlpha ? 4 : 0;

    float* specularSrc =
      hasSpecularTexture ? specularSrcImage.pixels.data() : specularGlossFactor.data();
    const int specularSrcStep = hasSpecularTexture ? 4 : 0;

    float* diffuseDst = diffuseDstImage.pixels.data();
    const int diffuseDstStep = dstImageChannels;

    float* opacityDst = dstHasAlpha ? diffuseDst + 3 : &constOpacityValue;
    const int opacityDstStep = dstHasAlpha ? 4 : 0;

    float* mrDst = mrDstImage.pixels.data();

    hasTransparency = false;

    const unsigned int numPixels = width * height;
    for (unsigned int i = 0; i < numPixels; ++i) {
        if (hasDiffuseTexture) {
            diffuseSrc[0] = srgbToLinear(diffuseSrc[0]) * diffuseFactor[0];
            diffuseSrc[1] = srgbToLinear(diffuseSrc[1]) * diffuseFactor[1];
            diffuseSrc[2] = srgbToLinear(diffuseSrc[2]) * diffuseFactor[2];
        }
        if (hasSpecularTexture) {
            specularSrc[0] = srgbToLinear(specularSrc[0]) * specularGlossFactor[0];
            specularSrc[1] = srgbToLinear(specularSrc[1]) * specularGlossFactor[1];
            specularSrc[2] = srgbToLinear(specularSrc[2]) * specularGlossFactor[2];
        }

        float diffuseResult[3];
        float metallic;
        _convertToMetallicRoughness(diffuseSrc, specularSrc, diffuseResult, &metallic);

        // convert diffuse and metallic back to sRGB
        diffuseDst[0] = linearToSRGB(diffuseResult[0]);
        diffuseDst[1] = linearToSRGB(diffuseResult[1]);
        diffuseDst[2] = linearToSRGB(diffuseResult[2]);
        // Yes, we do need to convert metallic from linear to srgb
        metallic = linearToSRGB(metallic);

        if (*opacitySrc < 1.0f)
            hasTransparency = true;
        *opacityDst = *opacitySrc; // preserve opacity (may be noop if hasConstOpacity)

        // The alpha channel of specularSrc contains glossiness, convert it to roughness and
        // assign to green channel. Assign metallic to the blue channel.
        mrDst[0] = 0.0f;
        mrDst[1] = 1.0f - specularSrc[3];
        mrDst[2] = metallic;

        // advance to next pixel
        diffuseSrc += diffuseSrcStep;
        opacitySrc += opacitySrcStep;
        specularSrc += specularSrcStep;
        diffuseDst += diffuseDstStep;
        opacityDst += opacityDstStep;
        mrDst += 3;
    }
}

// convert color component expected to be in the range [0,1] to the range [0,255]
inline int
_toint255(float f)
{
    return (int)(f * 255);
}

// convert rgb components of color to 24 bit int. This will be used as a uniqueness key
// for naming the generated textures
int
_getIntegerKey(const GfVec4f& color)
{
    return (_toint255(color[0]) << 16) + (_toint255(color[1]) << 8) + _toint255(color[2]);
}

} // end anonymous namespace

bool
translateSpecularGlossinessToMetallicRoughness(ImportGltfContext& ctx,
                                               std::unordered_map<std::string, int>& cache,
                                               const Input& diffuseIn,
                                               const Input& specularIn,
                                               const Input& opacityIn,
                                               const std::string& alphaMode,
                                               Input& diffuseOut,
                                               Input& opacityOut,
                                               Input& metallicOut,
                                               Input& roughnessOut)
{
    // we expect the diffuse and specular factors are stored in the value of the diffuse and
    // specular inputs
    if (!diffuseIn.value.IsEmpty() && !diffuseIn.value.IsHolding<GfVec4f>())
        return false;
    if (!specularIn.value.IsEmpty() && !specularIn.value.IsHolding<GfVec4f>())
        return false;

    GfVec4f diffuseFactor = diffuseIn.value.Get<GfVec4f>();
    GfVec4f specularGlossFactor = specularIn.value.Get<GfVec4f>();

    if (diffuseIn.image < 0 && specularIn.image < 0) {

        // Handle simple case of solid colors only
        GfVec3f newDiffuse;
        float newMetallic;
        _convertToMetallicRoughness(
          diffuseFactor.data(), specularGlossFactor.data(), newDiffuse.data(), &newMetallic);
        diffuseOut.image = -1;
        diffuseOut.value = newDiffuse;
        opacityOut.image = -1;
        opacityOut.value = diffuseFactor[3];
        metallicOut.image = -1;
        metallicOut.value = linearToSRGB(newMetallic);
        roughnessOut.image = -1;
        roughnessOut.value = 1.0f - specularGlossFactor[3];
        return true;

    } else if (specularIn.image < 0 && !_hasMetalness(specularGlossFactor.data())) {

        // This case is for when specular factors are all near zero. We simply use the diffuse and
        // opacity inputs
        diffuseOut = diffuseIn;
        opacityOut = opacityIn;
        metallicOut.image = -1;
        metallicOut.value = 0.0f;
        roughnessOut.image = -1;
        roughnessOut.value = 1.0f - specularGlossFactor[3];
        return true;

    } else {

        Image diffuseSrcImage;
        Image specularSrcImage;

        // read the diffuse image (if present)
        if (diffuseIn.image >= 0) {
            const ImageAsset& diffuseImageAsset = ctx.usd->images[diffuseIn.image];
            diffuseSrcImage.read(diffuseImageAsset);
            // if the diffuse image is not rgb, read it again with a forced set of channels
            if (diffuseSrcImage.channels < 3) {
                diffuseSrcImage.read(diffuseImageAsset, diffuseSrcImage.channels < 2 ? 3 : 4);
            }
        }

        // read the specular image (if present)
        if (specularIn.image >= 0) {
            const ImageAsset& specularImageAsset = ctx.usd->images[specularIn.image];
            specularSrcImage.read(specularImageAsset, 4);
        }

        // If both the diffuse and specular images are present but are of different sizes, we ignore
        // the specular image and just use the diffuse texture, setting metallic to 0 and use the
        // glossiness factor to determine roughness.
        if (!diffuseSrcImage.isEmpty() && !specularSrcImage.isEmpty() &&
            (diffuseSrcImage.width != specularSrcImage.width ||
             diffuseSrcImage.height != specularSrcImage.height)) {
            TF_WARN("Diffuse and specular images are of different sizes. Cannot convert from "
                    "specular-gloss to metallic-roughness. Dropping specular");

            diffuseOut = diffuseIn;
            opacityOut = opacityIn;
            metallicOut.image = -1;
            metallicOut.value = 0.0f;
            roughnessOut.image = -1;
            roughnessOut.value = 1.0f - specularGlossFactor[3];
            return true;
        }

        // define keys for diffuse and specular components
        int diffuseKey = diffuseIn.image >= 0 ? diffuseIn.image : _getIntegerKey(diffuseFactor);
        int specularKey =
          specularIn.image >= 0 ? specularIn.image : _getIntegerKey(specularGlossFactor);

        // create texture names. We will use these to determine uniqueness and detect
        // previous conversions
        std::string diffuseTextureName =
          _genImageName("specgloss-diffuse", diffuseKey, specularKey);
        std::string metallicRoughnessTextureName =
          _genImageName("specgloss-mr", diffuseKey, specularKey);

        // lookup texture names in cache to see if the combination of diffuse and specular
        // textures/factors have already been processed
        int diffuseImageIndex = lookupTexture(cache, diffuseTextureName);
        int mrImageIndex = lookupTexture(cache, metallicRoughnessTextureName);

        bool hasTransparency = false;

        // the textures are not in the cache so we need to create them
        if (diffuseImageIndex < 0 || mrImageIndex < 0) {
            Image diffuseDstImage;
            Image metallicRoughnessDstImage;

            _convertSpecularGlossToMetalicRough(diffuseSrcImage,
                                                diffuseFactor,
                                                specularSrcImage,
                                                specularGlossFactor,
                                                diffuseDstImage,
                                                metallicRoughnessDstImage,
                                                hasTransparency);

            // create the new diffuse USD image
            ctx.usd->reserveImages(2);
            auto [usdDiffuseImageIndex, usdDiffuseImage] = ctx.usd->addImage();
            usdDiffuseImage.name = diffuseTextureName;
            usdDiffuseImage.uri = diffuseTextureName + ".png";
            usdDiffuseImage.format = ImageFormatPng;
            diffuseDstImage.write(usdDiffuseImage);
            cache[diffuseTextureName] = usdDiffuseImageIndex;
            diffuseImageIndex = usdDiffuseImageIndex;

            // create the new metallicRoughness USD image
            auto [usdMetallicImageIndex, usdMRImage] = ctx.usd->addImage();
            usdMRImage.name = metallicRoughnessTextureName;
            usdMRImage.uri = metallicRoughnessTextureName + ".png";
            usdMRImage.format = ImageFormatPng;
            metallicRoughnessDstImage.write(usdMRImage);
            cache[metallicRoughnessTextureName] = usdMetallicImageIndex;
            mrImageIndex = usdMetallicImageIndex;
        }

        // for the new diffuse and opacity Inputs, use the wrapping, scale, bias and 2d transforms
        // of one of the diffuse or specular inputs (preferably the diffuse input)
        diffuseOut = diffuseIn.image >= 0 ? diffuseIn : specularIn;
        setInputImage(diffuseOut, diffuseImageIndex, 0, AdobeTokens->rgb, AdobeTokens->sRGB);

        if (hasTransparency && alphaMode != "OPAQUE") {
            opacityOut = diffuseOut;
            setInputImage(opacityOut, diffuseImageIndex, 0, AdobeTokens->a, AdobeTokens->raw);
        } else {
            opacityOut.image = -1;
            opacityOut.value = VtValue();
        }

        // for the new metallic and roughness Inputs, use the wrapping, scale, bias and 2d
        // transforms of one of the diffuse or specular inputs (preferably the specular input)
        metallicOut = specularIn.image >= 0 ? specularIn : diffuseIn;
        roughnessOut = specularIn.image >= 0 ? specularIn : diffuseIn;

        // metallic uses the b channel
        setInputImage(metallicOut, mrImageIndex, 0, AdobeTokens->b, AdobeTokens->raw);

        // roughness uses the g channel
        setInputImage(roughnessOut, mrImageIndex, 0, AdobeTokens->g, AdobeTokens->raw);
    }

    return true;
}

} // end namespace adobe::usd
