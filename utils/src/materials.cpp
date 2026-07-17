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
#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>
#include <fileformatutils/images.h>
#include <fileformatutils/materials.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/pxr.h>
#include <vector>

using namespace PXR_NS;
namespace adobe::usd {

const TfToken&
channel2Token(int channel)
{
    switch (channel) {
        case 0:
            return AdobeTokens->r;
        case 1:
            return AdobeTokens->g;
        case 2:
            return AdobeTokens->b;
        case 3:
            return AdobeTokens->a;
        default:
            TF_WARN("Invalid channel index: %d", channel);
            return AdobeTokens->invalid;
    }
}

int
token2Channel(const TfToken& token)
{
    if (token == AdobeTokens->r) {
        return 0;
    } else if (token == AdobeTokens->g) {
        return 1;
    } else if (token == AdobeTokens->b) {
        return 2;
    } else if (token == AdobeTokens->a) {
        return 3;
    } else if (token == AdobeTokens->rgb) {
        TF_WARN("Unexpected rgb channel token, assuming data is only in the red channel");
        return 0;
    } else {
        TF_WARN("Unexpected channel token '%s'", token.GetString().c_str());
        return -1;
    }
}

std::string
input2key(int imageIndex, const TfToken& channel, uint8_t val)
{
    if (imageIndex >= 0) {
        return std::to_string(imageIndex) + (channel.IsEmpty() ? "x" : channel.GetString());
    } else {
        return std::to_string(val);
    }
}

std::string
input2key(int imageIndex, int channelIndex)
{
    const TfToken& token = channel2Token(channelIndex);
    if (imageIndex >= 0) {
        return std::to_string(imageIndex) + token.GetString();
    }
    return token.GetString();
}

// Mirror threshold for Phong-to-PBR roughness conversion
// Shininess > ~600 produces roughness < 0.08, which should be treated as a perfect mirror
static constexpr float MIRROR_THRESHOLD = 0.08f;

// Phong to PBR conversion, taken from:
// https://docs.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping
void
phongToPbr(const Image& diffuse,
           const Image& specular,
           const Image& shininess,
           Image& albedo,
           Image& roughness,
           Image& metallic,
           float shininessFactor)
{
    float w = diffuse.width;
    float h = diffuse.height;

    albedo.allocate(w, h, 3);
    metallic.allocate(w, h, 1);
    roughness.allocate(w, h, 1);
    float dsr = 0.04; // dielectricSpecularReflectance
    int pixelCount = w * h;
    for (int i = 0; i < pixelCount; i++) {
        int r = 3 * i;
        int g = 3 * i + 1;
        int b = 3 * i + 2;
        // Read required pixel values once
        const float diffR = diffuse.pixels[r];
        const float diffG = diffuse.pixels[g];
        const float diffB = diffuse.pixels[b];
        const float specR = specular.pixels[r];
        const float specG = specular.pixels[g];
        const float specB = specular.pixels[b];
        const float shin = shininess.pixels[i];

        float specularIntensity = 0.2125 * specR + 0.7154 * specG + 0.0721 * specB;
        float diffuseBrightness =
          0.299 * diffR * diffR + 0.587 * diffG * diffG + 0.114 * diffB * diffB;
        float specularBrightness =
          0.299 * specR * specR + 0.587 * specG * specG + 0.114 * specB * specB;
        float specularStrength = std::max(specR, std::max(specG, specB));
        float rou = sqrt(2 / (shininessFactor * shin * specularIntensity + 2));

        // Treat near-zero roughness (high Phong shininess) as a perfect mirror. Kept consistent
        // with the value-based overload below.
        if (rou < MIRROR_THRESHOLD) {
            rou = 0.0;
        }

        float specComplement = 1 - specularStrength;
        float A = dsr;
        float B = (diffuseBrightness * (specComplement / (1 - A)) + specularBrightness) - 2 * A;
        float C = A - specularBrightness;
        float squareRoot = sqrt(std::max(0.0f, B * B - 4 * A * C));
        float value = (-B + squareRoot) / (2 * A);
        float met = std::min(1.0f, std::max(0.0f, value));

        float factor = specComplement / (1.0f - dsr) / std::max(1e-4, 1.0 - met);
        float dielectricR = diffR * factor;
        float dielectricG = diffG * factor;
        float dielectricB = diffB * factor;
        float metR = (specR - dsr * (1.0 - met)) * (1.0 / std::max(1e-4f, met));
        float metG = (specG - dsr * (1.0 - met)) * (1.0 / std::max(1e-4f, met));
        float metB = (specB - dsr * (1.0 - met)) * (1.0 / std::max(1e-4f, met));
        float lerpPoint = met * met;
        float albR = dielectricR * (1 - lerpPoint) + metR * lerpPoint;
        float albG = dielectricG * (1 - lerpPoint) + metG * lerpPoint;
        float albB = dielectricB * (1 - lerpPoint) + metB * lerpPoint;
        albR = std::min(1.0f, std::max(0.0f, albR));
        albG = std::min(1.0f, std::max(0.0f, albG));
        albB = std::min(1.0f, std::max(0.0f, albB));

        // Write resulting pixel values once
        albedo.pixels[r] = albR;
        albedo.pixels[g] = albG;
        albedo.pixels[b] = albB;
        metallic.pixels[i] = met;
        roughness.pixels[i] = rou;
    }
}

void
phongToPbr(const GfVec3f& diffuse,
           GfVec3f& specular,
           float shininess,
           GfVec3f& albedo,
           float& roughness,
           float& metallic,
           float shininessFactor)
{
    // The experimental specular/shininess attenuation that used to live here has been removed.
    // It muted bright speculars (white chrome read as gray metal, cyan read as muted cyan) and
    // could drive shininess negative above ~2000, so the conversion now uses specular and
    // shininess directly.

    float dsr = 0.04; // dielectricSpecularReflectance
    float specularIntensity = 0.2125 * specular[0] + 0.7154 * specular[1] + 0.0721 * specular[2];
    float diffuseBrightness = 0.299 * diffuse[0] * diffuse[0] + 0.587 * diffuse[1] * diffuse[1] +
                              0.114 * diffuse[2] * diffuse[2];
    float specularBrightness = 0.299 * specular[0] * specular[0] +
                               0.587 * specular[1] * specular[1] +
                               0.114 * specular[2] * specular[2];
    float specularStrength = std::max(specular[0], std::max(specular[1], specular[2]));

    // Roughness from Phong: alpha = sqrt(2 / (N_s + 2)), with specularIntensity accounting for
    // colored speculars.
    roughness = sqrt(2 / (shininessFactor * shininess * specularIntensity + 2));

    // High Phong shininess (above ~600, which maps to roughness below MIRROR_THRESHOLD) describes
    // a near-perfect mirror. Clamp the tiny residual to 0 so it renders as a clean mirror instead
    // of a faintly rough surface.
    if (roughness < MIRROR_THRESHOLD) {
        roughness = 0.0;
    }

    float specComplement = 1 - specularStrength;
    float A = dsr;
    float B = (diffuseBrightness * (specComplement / (1 - A)) + specularBrightness) - 2 * A;
    float C = A - specularBrightness;
    float squareRoot = sqrt(std::max(0.0f, B * B - 4 * A * C));
    float value = (-B + squareRoot) / (2 * A);
    metallic = std::min(1.0f, std::max(0.0f, value));

    // Note on Phong-to-PBR conversion ambiguity:
    // A Phong material with black diffuse + colored specular (e.g., diffuse=(0,0,0),
    // specular=(0,1,1)) is mathematically interpreted as a colored metal, resulting in metallic=1.0
    // and baseColor=specular. This is physically correct: metals have no diffuse component and
    // their color comes from specular. However, legacy Phong materials sometimes used this pattern
    // for artistic "black base + colored highlights" which would be a dielectric in PBR. Artists
    // should adjust such materials post-import if needed.

    float factor = specComplement / (1.0f - dsr) / std::max(1e-4, 1.0 - metallic);
    float dielectricR = diffuse[0] * factor;
    float dielectricG = diffuse[1] * factor;
    float dielectricB = diffuse[2] * factor;
    float metR = (specular[0] - dsr * (1.0 - metallic)) * (1.0 / std::max(1e-4f, metallic));
    float metG = (specular[1] - dsr * (1.0 - metallic)) * (1.0 / std::max(1e-4f, metallic));
    float metB = (specular[2] - dsr * (1.0 - metallic)) * (1.0 / std::max(1e-4f, metallic));
    float lerpPoint = metallic * metallic;
    albedo[0] = dielectricR * (1 - lerpPoint) + metR * lerpPoint;
    albedo[1] = dielectricG * (1 - lerpPoint) + metG * lerpPoint;
    albedo[2] = dielectricB * (1 - lerpPoint) + metB * lerpPoint;
    albedo[0] = std::min(1.0f, std::max(0.0f, albedo[0]));
    albedo[1] = std::min(1.0f, std::max(0.0f, albedo[1]));
    albedo[2] = std::min(1.0f, std::max(0.0f, albedo[2]));
}

// Computes only the roughness component of a Phong-to-PBR conversion, without touching metallic.
// Used when the caller wants to preserve an explicit metallic/reflectionFactor value from the
// source material rather than letting the full Phong-to-PBR algorithm infer metallicness from
// specular brightness (which over-estimates metallic for car-paint and other high-specular
// dielectrics).
static float
phongRoughness(float shininess, const GfVec3f& specular, float shininessFactor)
{
    float specularIntensity = 0.2125f * specular[0] + 0.7154f * specular[1] + 0.0721f * specular[2];
    float roughness = sqrt(2.0f / (shininessFactor * shininess * specularIntensity + 2.0f));
    if (roughness < MIRROR_THRESHOLD) {
        roughness = 0.0f;
    }
    return roughness;
}

bool
bumpToNormal(const Image& bump, Image& normal, float multiplier)
{
    if (bump.pixels.empty())
        return false;
    int w = bump.width;
    int h = bump.height;
    normal.allocate(w, h, 3);
    const float* bumpSrc = bump.pixels.data();
    float* normalDst = normal.pixels.data();
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            float center = bumpSrc[i * w + j];
            float ny = i != 0 ? multiplier * (center - bumpSrc[(i - 1) * w + j]) : 0;
            float nx = j != 0 ? multiplier * (bumpSrc[i * w + j - 1] - center) : 0;
            float nz = sqrt(1.0f - std::min(1.0f, sqrt(nx * nx + ny * ny)));
            int idx = 3 * (i * w + j);
            normalDst[idx] = nx / 2 + 0.5f;
            normalDst[idx + 1] = ny / 2 + 0.5f;
            normalDst[idx + 2] = nz / 2 + 0.5f;
        }
    }
    return true;
}

float
singleScatterToMultiscatter(float singleScatter, float anisotropy)
{
    float s = std::sqrt((1.0f - singleScatter) / (1.0f - singleScatter * anisotropy));
    return (1.0f - s) * (1.0f - 0.139f * s) / (1.0f + 1.17f * s);
}

GfVec3f
singleScatterToMultiscatter(const GfVec3f& singleScatter, float anisotropy)
{
    return GfVec3f(singleScatterToMultiscatter(singleScatter[0], anisotropy),
                   singleScatterToMultiscatter(singleScatter[1], anisotropy),
                   singleScatterToMultiscatter(singleScatter[2], anisotropy));
}

float
multiscatterToSingleScatter(float multiscatter, float anisotropy)
{
    multiscatter = std::clamp(multiscatter, 0.0f, 0.9999f);
    anisotropy = std::clamp(anisotropy, -0.9999f, 0.9999f);

    const float s = 4.09712f + 4.20863f * multiscatter;
    const float p = 9.59217f + 41.6808f * multiscatter + 17.7126f * multiscatter * multiscatter;
    const float singleScatter = 1.0f - (s - std::sqrt(p)) * (s - std::sqrt(p));
    const float denom = std::max(1.0e-4f, 1.0f - anisotropy * multiscatter * multiscatter);
    return std::clamp(singleScatter / denom, 0.0f, 1.0f);
}

GfVec3f
multiscatterToSingleScatter(const GfVec3f& multiscatter, float anisotropy)
{
    return GfVec3f(multiscatterToSingleScatter(multiscatter[0], anisotropy),
                   multiscatterToSingleScatter(multiscatter[1], anisotropy),
                   multiscatterToSingleScatter(multiscatter[2], anisotropy));
}

InputTranslator::InputTranslator(bool exportImages,
                                 std::vector<ImageAsset>& inputImages,
                                 const std::string& debugTag)
  : mDebugTag(debugTag)
  , mExportImages(exportImages)
  , mImagesSrc(std::move(inputImages))
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%s: InputTranslator source images:\n", mDebugTag.c_str());
    for (size_t i = 0; i < mImagesSrc.size(); i++) {
        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "  image[%lu]: %s\n", i, mImagesSrc[i].name.c_str());
    }
    mDecodedImages.resize(mImagesSrc.size());
    mDecodedMap.resize(mImagesSrc.size(), false);
}

InputTranslator::~InputTranslator() {}

void
InputTranslator::translateDirectInternal(int imageIdx, Input& out)
{
    const ImageAsset& asset = mImagesSrc[imageIdx];
    int imageIndex = -1;
    std::string key = TfGetBaseName(asset.uri);
    if (key.rfind("direct-") != 0) {
        // Only add direct- if image uri doesn't already start with it, to avoid multiple
        // "direct-" prefixes
        key = "direct-" + key;
    }
    const auto it = mCache.find(key);
    if (it != mCache.end()) {
        imageIndex = it->second;
    } else {
        imageIndex = mImagesDst.size();
        mImagesDst.push_back(ImageAsset());
        ImageAsset& newAsset = mImagesDst.back();
        newAsset.uri = key;
        newAsset.name = asset.name;
        if (asset.format == ImageFormatUnknown && imageIdx < (int)mDecodedMap.size() &&
            mDecodedMap[imageIdx]) {
            // Intermediate image: decoded pixels exist but no encoded bytes (format is Unknown).
            // Infer the encode format from the URI extension (e.g. a cache key ending in ".exr"
            // should stay EXR); fall back to PNG if the extension is absent or unrecognised.
            const ImageFormat inferredFormat = getFormat(TfStringGetSuffix(asset.uri));
            newAsset.format =
              (inferredFormat != ImageFormatUnknown) ? inferredFormat : ImageFormatPng;
            mDecodedImages[imageIdx].write(newAsset);
        } else {
            newAsset.format = asset.format;
            newAsset.image = asset.image; // create a copy
        }
        mCache[key] = imageIndex;
    }
    out.image = imageIndex;
}

bool
InputTranslator::translateDirect(const Input& in, Input& out, bool intermediate)
{
    if (intermediate) {
        out = in;
        return true;
    }

    if (in.image >= 0) {
        out = in;
        translateDirectInternal(in.image, out);
        return true;
    } else if (!in.value.IsEmpty()) {
        out = in;
        return true;
    }
    return false;
}

bool
InputTranslator::translateToSingle(const std::string& name,
                                   const Input& in,
                                   Input& out,
                                   bool intermediate)
{
    return translateToSingleAffine(name, in, 1.0f, 0.0f, out, intermediate);
}

bool
InputTranslator::translateToSingleAffine(const std::string& name,
                                         const Input& in,
                                         float scale,
                                         float bias,
                                         Input& out,
                                         bool intermediate)
{
    if (intermediate) {
        out = in;
        return true;
    }

    if (in.image >= 0) {
        int channelIndex = token2Channel(in.channel);
        if (channelIndex >= 0) {
            return extractChannel(name, in, channelIndex, scale, bias, out, false);
        } else {
            TF_WARN("Expecting a source image referencing a single channel");
            return false;
        }
    } else if (!in.value.IsEmpty()) {
        out = in;

        // apply scale and bias to source scale and bias
        out.scale = scale * in.scale;
        out.bias = scale * in.bias + GfVec4f(bias);
        return true;
    }
    return false;
}

// TODO: complete testing of translateFactor on images
// TODO: complete testing of 'intermediate' capability
bool
InputTranslator::translateFactor(const Input& in,
                                 const Input& factor,
                                 Input& out,
                                 bool intermediate)
{
    bool inEmpty = in.isEmpty();
    bool factorEmpty = factor.isEmpty();
    if (inEmpty && factorEmpty) {
        return false;
    }

    if (factorEmpty) {
        return translateDirect(in, out, intermediate);
    }
    if (inEmpty) {
        // If the factor is the only valid input and has an image we translate it directly, with the
        // assumption that even if it is a single channel texture, it can be read and used as a RGB
        // input, which is the assumed format for out.
        if (factor.image >= 0) {
            bool result = translateDirect(factor, out, intermediate);
            if (result) {
                out.channel = AdobeTokens->rgb;
            }
            return result;
        } else {
            // We know it's a constant value and it should be a single channel float value. But out
            // has to be a float3 value. So we upgrade the value if necessary.
            if (factor.value.IsHolding<float>()) {
                float f = factor.value.UncheckedGet<float>();
                out.value = GfVec3f(f);
                return true;
            } else if (factor.value.IsHolding<GfVec3f>()) {
                const GfVec3f& v = factor.value.UncheckedGet<GfVec3f>();
                out.value = v;
                TF_WARN("Factor image had an unexpected 3 channel value (expected single float)");
                return true;
            } else {
                TF_WARN("Factor image had an unexpected channel value (type %s)",
                        factor.value.GetTypeName().c_str());
                return false;
            }
        }
    }

    if (factor.numChannels() != 1) {
        TF_WARN("Can't multiply with factor that isn't a float input. Factor has %d channels",
                factor.numChannels());
        return false;
    }

    if (in.image >= 0 && factor.image >= 0) {
        // Both inputs are images
        // Storage format is determined by the in input
        const ImageAsset& inImageAsset = mImagesSrc[in.image];
        std::string assetName =
          "factor-" + std::to_string(in.image) + "-" + std::to_string(factor.image);
        std::string key = assetName + "." + getFormatExtension(inImageAsset.format);
        int imageIndex = -1;
        const auto& it = mCache.find(key);
        if (it != mCache.end()) {
            imageIndex = it->second;
        } else {
            Image outImage;
            if (mExportImages) {
                auto [inImageValid, inImage] = getDecodedImage(in.image);
                auto [factorImageValid, factorImage] = getDecodedImage(factor.image);
                GUARD(inImageValid && factorImageValid, "Invalid images");
                imageMult(inImage, factorImage, outImage);
            }
            imageIndex =
              addImage(std::move(outImage), assetName, key, inImageAsset.format, intermediate);
        }
        // Copy the input image's settings and update to the new image index
        out = in;
        out.image = imageIndex;
    } else if (in.image >= 0) {
        // in input is an image and factor is a single float
        translateDirect(in, out, intermediate);
        float f = factor.value.GetWithDefault(1.0f);
        if (f != 1.0f) {
            out.scale *= f;
        }
    } else if (factor.image >= 0) {
        // factor is an image and the in input is a constant value
        translateDirect(factor, out, intermediate);
        if (in.value.IsHolding<float>()) {
            float f = in.value.UncheckedGet<float>();
            out.scale *= f;
        } else if (in.value.IsHolding<GfVec2f>()) {
            const GfVec2f& v = in.value.UncheckedGet<GfVec2f>();
            out.scale[0] *= v[0];
            out.scale[1] *= v[1];
        } else if (in.value.IsHolding<GfVec3f>()) {
            const GfVec3f& v = in.value.UncheckedGet<GfVec3f>();
            out.scale[0] *= v[0];
            out.scale[1] *= v[1];
            out.scale[2] *= v[2];
        } else if (in.value.IsHolding<GfVec4f>()) {
            const GfVec4f& v = in.value.UncheckedGet<GfVec4f>();
            out.scale[0] *= v[0];
            out.scale[1] *= v[1];
            out.scale[2] *= v[2];
            out.scale[3] *= v[3];
        } else {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "translateFactor in input is not holding a float value\n");
        }
    } else {
        // Both inputs are constant values
        if (factor.value.IsHolding<float>()) {
            float f = factor.value.UncheckedGet<float>();
            if (in.value.IsHolding<float>()) {
                out.value = in.value.UncheckedGet<float>() * f;
            } else if (in.value.IsHolding<GfVec2f>()) {
                out.value = in.value.UncheckedGet<GfVec2f>() * f;
            } else if (in.value.IsHolding<GfVec3f>()) {
                out.value = in.value.UncheckedGet<GfVec3f>() * f;
            } else if (in.value.IsHolding<GfVec4f>()) {
                out.value = in.value.UncheckedGet<GfVec4f>() * f;
            } else {
                TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                             "translateFactor in input is not holding a float value\n");
            }
        }
    }
    return true;
}

namespace {

int
_getInputComponentCount(const Input& input)
{
    if (input.image >= 0) {
        if (input.channel == AdobeTokens->rgba) {
            return 4;
        }
        if (input.channel == AdobeTokens->rgb) {
            return 3;
        }
        return 1;
    }
    if (input.value.IsHolding<float>() || input.value.IsHolding<int>()) {
        return 1;
    }
    if (input.value.IsHolding<GfVec2f>()) {
        return 2;
    }
    if (input.value.IsHolding<GfVec3f>()) {
        return 3;
    }
    if (input.value.IsHolding<GfVec4f>()) {
        return 4;
    }
    return 0;
}

void
_getConstantInputValues(const Input& input, int outChannels, float* values)
{
    for (int i = 0; i < outChannels; ++i) {
        values[i] = 0.0f;
    }

    float f;
    GfVec2f v2;
    GfVec3f v3;
    GfVec4f v4;

    if (getInputValue(input, &f)) {
        for (int i = 0; i < outChannels; ++i) {
            values[i] = f;
        }
    } else if (getInputValue(input, &v2)) {
        for (int i = 0; i < std::min(outChannels, 2); ++i) {
            values[i] = v2[i];
        }
        for (int i = 2; i < outChannels; ++i) {
            values[i] = values[0];
        }
    } else if (getInputValue(input, &v3)) {
        for (int i = 0; i < std::min(outChannels, 3); ++i) {
            values[i] = v3[i];
        }
        if (outChannels == 4) {
            values[3] = input.scale[3] + input.bias[3];
        }
    } else if (getInputValue(input, &v4)) {
        for (int i = 0; i < outChannels; ++i) {
            values[i] = v4[i];
        }
    }
}

// Samples an input image at normalized UV coordinates using bilinear filtering.
bool
_sampleInputImageBilinear(const Input& input,
                          const Image& image,
                          float u,
                          float v,
                          int outChannels,
                          float* values)
{
    const int w = image.width;
    const int h = image.height;
    const int ch = image.channels;

    // Map UV to pixel space with pixel centers at x+0.5.  Clamp UV to [0,1] first so that
    // floor() gives consistent x0/y0 and the fractional parts tx/ty stay in [0,1).
    const float px = std::max(0.0f, std::min(1.0f, u)) * w - 0.5f;
    const float py = std::max(0.0f, std::min(1.0f, v)) * h - 0.5f;
    const float fpx = std::floor(px);
    const float fpy = std::floor(py);
    const int x0 = std::max(0, static_cast<int>(fpx));
    const int y0 = std::max(0, static_cast<int>(fpy));
    const int x1 = std::min(w - 1, x0 + 1);
    const int y1 = std::min(h - 1, y0 + 1);
    const float tx = px - fpx;
    const float ty = py - fpy;

    // Per-corner base pointers — the (y*width + x)*channels offset is constant per corner;
    // adding the channel index c gives the exact element without per-iteration arithmetic.
    // p00 is always needed; p10/p01/p11 are only needed for bilinear interpolation.
    const float* const pixels = image.pixels.data();
    const float* const p00 = pixels + (y0 * w + x0) * ch;

    // Exact-pixel fast path: when both fractional parts are zero the UV lands directly on
    // pixel (x0, y0) — no interpolation is needed and the three other corners are never
    // touched.  This is always the case when the source image and the output image share
    // the same dimensions (pixel-centre UVs map back to exact integers after the round-trip).
    if (tx == 0.0f && ty == 0.0f) {
        if (input.channel == AdobeTokens->rgb || input.channel == AdobeTokens->rgba) {
            const int srcChannels = std::min(outChannels, ch);
            for (int c = 0; c < srcChannels; ++c) {
                values[c] = p00[c] * input.scale[c] + input.bias[c];
            }
            if (outChannels > ch) {
                const float lastVal = p00[ch - 1];
                const int broadcastEnd = std::min(outChannels, 3);
                for (int c = ch; c < broadcastEnd; ++c) {
                    values[c] = lastVal * input.scale[c] + input.bias[c];
                }
                if (outChannels == 4) {
                    values[3] = 1.0f;
                }
            }
            return true;
        }
        int srcChannel = token2Channel(input.channel);
        if (srcChannel < 0) {
            return false;
        }
        if (srcChannel >= ch) {
            if (ch == 1) {
                srcChannel = 0;
            } else {
                TF_WARN("Input channel %s out of range for source image with %d channels",
                        input.channel.GetText(),
                        ch);
                return false;
            }
        }
        const float val = p00[srcChannel] * input.scale[0] + input.bias[0];
        values[0] = val;
        for (int c = 1; c < outChannels; ++c) {
            values[c] = (c == 3) ? 1.0f : val;
        }
        return true;
    }

    // Bilinear path — compute the three remaining corner pointers and the four weights.
    const float* const p10 = pixels + (y0 * w + x1) * ch;
    const float* const p01 = pixels + (y1 * w + x0) * ch;
    const float* const p11 = pixels + (y1 * w + x1) * ch;

    const float w00 = (1.0f - tx) * (1.0f - ty);
    const float w10 = tx * (1.0f - ty);
    const float w01 = (1.0f - tx) * ty;
    const float w11 = tx * ty;

    if (input.channel == AdobeTokens->rgb || input.channel == AdobeTokens->rgba) {
        if (outChannels == ch) {
            // Fast path: source and output channel counts match — no broadcasting or alpha
            // synthesis needed.  The inner loop is branch-free.
            for (int c = 0; c < outChannels; ++c) {
                const float val = w00 * p00[c] + w10 * p10[c] + w01 * p01[c] + w11 * p11[c];
                values[c] = val * input.scale[c] + input.bias[c];
            }
            return true;
        }

        // Slow path: output channel count differs from the source.
        // Region 1: channels that exist in the source — direct indexed, no clamping.
        const int srcChannels = std::min(outChannels, ch);
        for (int c = 0; c < srcChannels; ++c) {
            const float val = w00 * p00[c] + w10 * p10[c] + w01 * p01[c] + w11 * p11[c];
            values[c] = val * input.scale[c] + input.bias[c];
        }
        if (outChannels > ch) {
            // Region 2: broadcast the last source channel into any non-alpha slots.
            // The bilinear value is the same for every broadcast channel, so compute it once.
            const float lastVal =
              w00 * p00[ch - 1] + w10 * p10[ch - 1] + w01 * p01[ch - 1] + w11 * p11[ch - 1];
            const int broadcastEnd = std::min(outChannels, 3);
            for (int c = ch; c < broadcastEnd; ++c) {
                values[c] = lastVal * input.scale[c] + input.bias[c];
            }
            // Region 3: synthesize alpha = 1.0 when the source has no alpha channel.
            if (outChannels == 4) {
                values[3] = 1.0f;
            }
        }
        return true;
    }

    int srcChannel = token2Channel(input.channel);
    if (srcChannel < 0) {
        return false;
    }
    if (srcChannel >= ch) {
        if (ch == 1) {
            srcChannel = 0;
        } else {
            TF_WARN("Input channel %s out of range for source image with %d channels",
                    input.channel.GetText(),
                    ch);
            return false;
        }
    }
    const float val =
      w00 * p00[srcChannel] + w10 * p10[srcChannel] + w01 * p01[srcChannel] + w11 * p11[srcChannel];
    values[0] = val * input.scale[0] + input.bias[0];
    for (int c = 1; c < outChannels; ++c) {
        // Synthetic alpha (channel 3 when the source is single-channel) should be 1.0,
        // not a copy of the sampled scalar value.
        values[c] = (c == 3) ? 1.0f : values[0];
    }
    return true;
}

void
_setTranslatedInputImageDefaults(const Input& reference, int channels, Input& out)
{
    out = reference;
    out.channel = channels == 1   ? AdobeTokens->r
                  : channels == 4 ? AdobeTokens->rgba
                                  : AdobeTokens->rgb;
    out.scale = kDefaultTexScale;
    out.bias = kDefaultTexBias;
}

void
_setTranslatedInputConstant(const std::vector<float>& values, Input& out)
{
    if (values.size() == 1) {
        out.value = values[0];
    } else if (values.size() == 2) {
        out.value = GfVec2f(values[0], values[1]);
    } else if (values.size() == 3) {
        out.value = GfVec3f(values[0], values[1], values[2]);
    } else if (values.size() == 4) {
        out.value = GfVec4f(values[0], values[1], values[2], values[3]);
    }
    out.image = -1;
    out.scale = kDefaultTexScale;
    out.bias = kDefaultTexBias;
}

}

template<typename T>
bool
_valuesAreEqual(const std::vector<T>& values)
{
    if (values.empty()) {
        return true;
    }
    T firstValue = values[0];
    for (size_t i = 1; i < values.size(); ++i) {
        if (firstValue != values[i])
            return false;
    }
    return true;
}

// Checks that all image inputs share the same UV transform (uvIndex, uvRotation, uvScale,
// uvTranslation).
// - When all match: copies the common values onto \p out and returns true.
// - When any differ: resets \p out's UV transform to identity and returns false, indicating the
//   caller should bake each input's transform into the pixel data instead.
// Inputs with no image (image < 0) are skipped.
bool
_resolveOutputUVTransform(const std::vector<const Input*>& inputs, Input& out)
{
    std::vector<int> uvIndices;
    std::vector<float> rotations;
    std::vector<GfVec2f> scales;
    std::vector<GfVec2f> translations;
    for (const Input* input : inputs) {
        if (input && input->image >= 0) {
            uvIndices.push_back(input->uvIndex);
            rotations.push_back(input->uvRotation);
            scales.push_back(input->uvScale);
            translations.push_back(input->uvTranslation);
        }
    }
    const bool match = _valuesAreEqual(uvIndices) && _valuesAreEqual(rotations) &&
                       _valuesAreEqual(scales) && _valuesAreEqual(translations);
    if (match) {
        if (!uvIndices.empty()) {
            out.uvIndex = uvIndices[0];
            out.uvRotation = rotations[0];
            out.uvScale = scales[0];
            out.uvTranslation = translations[0];
        }
    } else {
        // Transforms will be baked into pixel data; output has identity UV transform.
        out.uvIndex = 0;
        out.uvRotation = kDefaultUvRotation;
        out.uvScale = kDefaultUvScale;
        out.uvTranslation = kDefaultUvTranslation;
    }
    return match;
}

// Applies an input's UV transform (scale, rotation, translation) to the normalized coordinates
// Precomputed 2×3 affine matrix for a UV transform.  Built once per input outside the pixel
// loop so that cos/sin and the constant folding are not repeated per pixel.
struct UVTransform
{
    float a, b, c; // su = a*u + b*v + c
    float d, e, f; // sv = d*u + e*v + f
    bool isIdentity;
};

// Build a UVTransform from an Input's uvRotation / uvScale / uvTranslation.
// Expands the place2d convention (rotate around (0.5,0.5), then scale, then translate) into
// the equivalent 2×3 matrix so that per-pixel work is just two multiply-adds.
UVTransform
_buildUVTransform(const Input& input)
{
    if (input.hasDefaultTransform()) {
        return { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, true };
    }
    const float r = input.uvRotation * (static_cast<float>(M_PI) / 180.0f);
    const float cosR = std::cos(r);
    const float sinR = std::sin(r);
    const float sx = input.uvScale[0];
    const float sy = input.uvScale[1];
    const float tx = input.uvTranslation[0];
    const float ty = input.uvTranslation[1];
    return { cosR * sx, -sinR * sx, 0.5f * sx * (1.0f - cosR + sinR) + tx,
             sinR * sy, cosR * sy,  0.5f * sy * (1.0f - sinR - cosR) + ty,
             false };
}

// (u, v) and returns the resulting source UV, wrapped to [0, 1) with repeat semantics.
// Call _buildUVTransform once per input outside the pixel loop and pass the result here.
void
_applyUVTransform(const UVTransform& xf, float u, float v, float& su, float& sv)
{
    if (xf.isIdentity) {
        su = u;
        sv = v;
        return;
    }
    su = xf.a * u + xf.b * v + xf.c;
    sv = xf.d * u + xf.e * v + xf.f;
    su -= std::floor(su);
    sv -= std::floor(sv);
}

// Returns true if all image inputs share the same UV transform. Constant inputs (image < 0)
// are ignored. Used to decide whether to bake transforms into pixel data or propagate as metadata.
bool
_inputsShareUVTransform(const std::vector<const Input*>& inputs)
{
    std::vector<int> uvIndices;
    std::vector<float> rotations;
    std::vector<GfVec2f> scales;
    std::vector<GfVec2f> translations;
    for (const Input* input : inputs) {
        if (input && input->image >= 0) {
            uvIndices.push_back(input->uvIndex);
            rotations.push_back(input->uvRotation);
            scales.push_back(input->uvScale);
            translations.push_back(input->uvTranslation);
        }
    }
    return _valuesAreEqual(uvIndices) && _valuesAreEqual(rotations) && _valuesAreEqual(scales) &&
           _valuesAreEqual(translations);
}

// Produces a cache-key fragment encoding the UV transform of an image input, for use when the
// transform is being baked into pixel data rather than propagated as output metadata.
// Returns an empty string for constant inputs (image < 0) or default transforms.
std::string
_uvTransformKey(const Input& input)
{
    if (input.image < 0 || input.hasDefaultTransform()) {
        return "";
    }
    return TfStringPrintf("-uv%d_r%.4f_s%.4f,%.4f_t%.4f,%.4f",
                          input.uvIndex,
                          input.uvRotation,
                          input.uvScale[0],
                          input.uvScale[1],
                          input.uvTranslation[0],
                          input.uvTranslation[1]);
}

// ---------------------------------------------------------------------------
// _applyImageOp — shared implementation for translateProduct / translateMax / translateLerp
// ---------------------------------------------------------------------------
// All three functions share the same structure: cache-key construction, image decode, output-size
// computation, UV-transform building, and the per-pixel sample loop.  Only the per-pixel math
// differs; that is supplied by the caller as a small lambda (PixelFn).
//
// PixelFn signature:  void(float* const* bufs, float* outPixel, int outChannels)
//   bufs[i] points to the slot.channels float values that were sampled (or evaluated as a
//   constant) for slot i.  The function must write exactly outChannels floats to outPixel.

template<typename PixelFn>
bool
InputTranslator::_applyImageOp(const std::string& name,
                               const std::string& opTag,
                               const std::string& extraKeySuffix,
                               std::initializer_list<_ImageOpSlot> slots,
                               int outChannels,
                               bool intermediate,
                               Input& out,
                               PixelFn pixelFn)
{
    const std::vector<_ImageOpSlot> slotsVec(slots);
    const int slotCount = static_cast<int>(slotsVec.size());

    // Collect raw input pointers for UV-transform helpers.
    std::vector<const Input*> inputPtrs;
    inputPtrs.reserve(slotCount);
    for (const auto& s : slotsVec)
        inputPtrs.push_back(s.input);

    // Find the first image-bearing input; it supplies the output UV metadata.
    const Input* reference = nullptr;
    for (const Input* p : inputPtrs) {
        if (p->image >= 0) {
            reference = p;
            break;
        }
    }
    GUARD(reference && getDecodedImage(reference->image).first, "Invalid reference image");

    // When UV transforms differ across image inputs, each input's transform is baked into the
    // pixel data; the cache key must then include the transform parameters to avoid collisions.
    const bool transformsMatch = _inputsShareUVTransform(inputPtrs);

    // Build cache key: name-opTag-key0-key1-…[uvKeys][extraSuffix].png
    std::string key = name + "-" + opTag + "-";
    for (int s = 0; s < slotCount; ++s) {
        if (s > 0)
            key += "-";
        key += input2key(slotsVec[s].input->image, slotsVec[s].input->channel, 0);
    }
    if (!transformsMatch) {
        for (const auto& slot : slotsVec)
            key += _uvTransformKey(*slot.input);
    }
    key += extraKeySuffix + ".png";

    int imageIndex = -1;
    const auto it = mCache.find(key);
    if (it != mCache.end()) {
        imageIndex = it->second;
    } else {
        Image outImage;
        if (mExportImages) {
            // Per-slot decode state.
            struct SlotState
            {
                Image image;
                UVTransform xf = {};
            };
            std::vector<SlotState> state(slotCount);

            // Decode each image-bearing slot.
            for (int s = 0; s < slotCount; ++s) {
                if (slotsVec[s].input->image >= 0) {
                    auto [ok, img] = getDecodedImage(slotsVec[s].input->image);
                    if (!ok) {
                        TF_WARN("Failed to decode image for '%s'", name.c_str());
                        return false;
                    }
                    state[s].image = img;
                }
            }

            // Output resolution = union of all contributing image dimensions.
            int outW = 0, outH = 0;
            for (int s = 0; s < slotCount; ++s) {
                if (slotsVec[s].input->image >= 0) {
                    outW = std::max(outW, state[s].image.width);
                    outH = std::max(outH, state[s].image.height);
                }
            }
            outImage.allocate(outW, outH, outChannels);

            // Build per-slot affine UV transforms once, outside the pixel loop.
            for (int s = 0; s < slotCount; ++s)
                state[s].xf = _buildUVTransform(transformsMatch ? Input{} : *slotsVec[s].input);

            // Allocate per-slot sample buffers.
            std::vector<std::vector<float>> bufs(slotCount);
            std::vector<float*> bufPtrs(slotCount);
            for (int s = 0; s < slotCount; ++s) {
                bufs[s].resize(slotsVec[s].channels);
                bufPtrs[s] = bufs[s].data();
            }

            const int pixelCount = outW * outH;
            for (int i = 0; i < pixelCount; ++i) {
                const float u = (i % outW + 0.5f) / outW;
                const float v = (i / outW + 0.5f) / outH;
                float su, sv;
                for (int s = 0; s < slotCount; ++s) {
                    const _ImageOpSlot& slot = slotsVec[s];
                    float* buf = bufPtrs[s];
                    if (slot.input->image >= 0) {
                        _applyUVTransform(state[s].xf, u, v, su, sv);
                        if (!_sampleInputImageBilinear(
                              *slot.input, state[s].image, su, sv, slot.channels, buf)) {
                            TF_WARN("Failed to sample image for '%s'", name.c_str());
                            return false;
                        }
                        if (slot.linearize) {
                            for (int c = 0; c < slot.channels; ++c)
                                buf[c] = srgbToLinear(buf[c]);
                        }
                    } else {
                        _getConstantInputValues(*slot.input, slot.channels, buf);
                    }
                }
                pixelFn(bufPtrs.data(), outImage.pixels.data() + i * outChannels, outChannels);
            }
        }
        imageIndex = addImage(std::move(outImage), name, key, ImageFormatPng, intermediate);
        mCache[key] = imageIndex;
    }

    _setTranslatedInputImageDefaults(*reference, outChannels, out);
    _resolveOutputUVTransform(inputPtrs, out);
    out.image = imageIndex;
    return true;
}

bool
InputTranslator::translateProduct(const std::string& name,
                                  const Input& in,
                                  const Input& factor,
                                  Input& out,
                                  bool intermediate,
                                  bool linearize)
{
    if (in.isEmpty() || factor.isEmpty())
        return false;

    const int outChannels = std::max(_getInputComponentCount(in), _getInputComponentCount(factor));
    if (outChannels <= 0)
        return false;

    if (in.image < 0 && factor.image < 0) {
        std::vector<float> lhs(outChannels), rhs(outChannels);
        _getConstantInputValues(in, outChannels, lhs.data());
        _getConstantInputValues(factor, outChannels, rhs.data());
        for (int i = 0; i < outChannels; ++i)
            lhs[i] *= rhs[i];
        out = in;
        _setTranslatedInputConstant(lhs, out);
        return true;
    }

    return _applyImageOp(name,
                         "product",
                         linearize ? "-lin" : "",
                         { { &in, outChannels, linearize }, { &factor, outChannels, linearize } },
                         outChannels,
                         intermediate,
                         out,
                         [linearize](float* const* b, float* dst, int ch) {
                             for (int c = 0; c < ch; ++c) {
                                 const float val = b[0][c] * b[1][c];
                                 dst[c] = linearize ? linearToSRGB(val) : val;
                             }
                         });
}

bool
InputTranslator::translateMax(const std::string& name,
                              const Input& in0,
                              const Input& in1,
                              Input& out,
                              bool intermediate)
{
    if (in0.isEmpty() || in1.isEmpty()) {
        return false;
    }

    const int outChannels = std::max(_getInputComponentCount(in0), _getInputComponentCount(in1));
    if (outChannels <= 0) {
        return false;
    }

    if (in0.image < 0 && in1.image < 0) {
        std::vector<float> lhs(outChannels);
        std::vector<float> rhs(outChannels);
        _getConstantInputValues(in0, outChannels, lhs.data());
        _getConstantInputValues(in1, outChannels, rhs.data());
        for (int i = 0; i < outChannels; ++i) {
            lhs[i] = std::max(lhs[i], rhs[i]);
        }
        out = in0;
        _setTranslatedInputConstant(lhs, out);
        return true;
    }

    return _applyImageOp(name,
                         "max",
                         "",
                         { { &in0, outChannels, false }, { &in1, outChannels, false } },
                         outChannels,
                         intermediate,
                         out,
                         [](float* const* b, float* dst, int ch) {
                             for (int c = 0; c < ch; ++c)
                                 dst[c] = std::max(b[0][c], b[1][c]);
                         });
}

bool
InputTranslator::translateLerp(const std::string& name,
                               const Input& in0,
                               const Input& in1,
                               const Input& mask,
                               Input& out,
                               bool intermediate,
                               bool linearize)
{
    if (mask.isEmpty())
        return translateDirect(in0, out, intermediate);
    if (in0.isEmpty())
        return translateDirect(in1, out, intermediate);
    if (in1.isEmpty())
        return translateDirect(in0, out, intermediate);
    if (mask.numChannels() != 1) {
        TF_WARN("translateLerp expects a single-channel mask input");
        return false;
    }

    const int outChannels = std::max(_getInputComponentCount(in0), _getInputComponentCount(in1));
    if (outChannels <= 0)
        return false;

    if (in0.image < 0 && in1.image < 0 && mask.image < 0) {
        std::vector<float> lhs(outChannels), rhs(outChannels), tvals(1);
        _getConstantInputValues(in0, outChannels, lhs.data());
        _getConstantInputValues(in1, outChannels, rhs.data());
        _getConstantInputValues(mask, 1, tvals.data());
        const float t = std::clamp(tvals[0], 0.0f, 1.0f);
        for (int i = 0; i < outChannels; ++i)
            lhs[i] = lhs[i] * (1.0f - t) + rhs[i] * t;
        out = in0;
        _setTranslatedInputConstant(lhs, out);
        return true;
    }

    // The mask drives blending weight; include it in the transform check so that a mask on a
    // different UV set than in0/in1 also triggers per-input baking.
    return _applyImageOp(
      name,
      "lerp",
      linearize ? "-lin" : "",
      { { &in0, outChannels, linearize }, { &in1, outChannels, linearize }, { &mask, 1, false } },
      outChannels,
      intermediate,
      out,
      [linearize](float* const* b, float* dst, int ch) {
          const float t = std::clamp(b[2][0], 0.0f, 1.0f);
          for (int c = 0; c < ch; ++c) {
              const float val = b[0][c] * (1.0f - t) + b[1][c] * t;
              dst[c] = linearize ? linearToSRGB(val) : val;
          }
      });
}

// TODO: complete testing of translateAffine on images
// TODO: complete testing of 'intermediate' capability
bool
InputTranslator::extractChannel(const std::string& name,
                                const Input& in,
                                int channelIndex,
                                float scale,
                                float bias,
                                Input& out,
                                bool intermediate)
{
    if (channelIndex < 0 || channelIndex > 3) {
        TF_WARN("Invalid channel index");
        return false;
    }
    out = in;
    if (intermediate) {
        return true;
    }

    // apply scale and bias to source channel scale and bias
    float newscale = scale * in.scale[channelIndex];
    float newbias = scale * in.bias[channelIndex] + bias;

    if (in.image >= 0) {
        const ImageAsset& inImageAsset = mImagesSrc[in.image];
        std::string assetName = name + "-" + input2key(in.image, channelIndex);
        std::string key = assetName + "." + getFormatExtension(inImageAsset.format);
        int texture = -1;
        const auto& it = mCache.find(key);
        if (it != mCache.end()) {
            texture = it->second;
        } else {
            Image outImage;
            if (mExportImages) {
                auto [inImageValid, inImage] = getDecodedImage(in.image);
                GUARD(inImageValid, "Invalid image");
                if (inImage.channels == 1 && newscale == 1.0f && newbias == 0.0f) {
                    // If the source image has a single channel and there isn't a
                    // scale or bias to be applied, we just copy but ensure we set
                    // the out channel to 'r' and reset the scale and bias
                    bool result = translateDirect(in, out, false);
                    if (result) {
                        out.channel = AdobeTokens->r;
                        out.scale = kDefaultTexScale;
                        out.bias = kDefaultTexBias;
                    }
                    return result;
                } else {
                    // apply scale and bias to source channel and store in single channel outImage
                    imageExtractChannel(inImage, channelIndex, newscale, newbias, outImage);
                }
            }
            texture = addImage(std::move(outImage), assetName, key, inImageAsset.format, false);
        }
        out.image = texture;
        out.channel = AdobeTokens->r;
        out.colorspace = AdobeTokens->raw;
    }

    if (in.value.IsHolding<float>()) {
        out.value = in.value.UncheckedGet<float>() * newscale + bias;
    } else if (in.value.IsHolding<GfVec2f>()) {
        if (channelIndex < 2)
            out.value = in.value.UncheckedGet<GfVec2f>()[channelIndex] * newscale + newbias;
    } else if (in.value.IsHolding<GfVec3f>()) {
        if (channelIndex < 3)
            out.value = in.value.UncheckedGet<GfVec3f>()[channelIndex] * newscale + newbias;
    } else if (in.value.IsHolding<GfVec4f>()) {
        if (channelIndex < 4)
            out.value = in.value.UncheckedGet<GfVec4f>()[channelIndex] * newscale + bias;
    }

    // Clear the scale and bias since it was applied to the pixel values and constants
    out.scale = kDefaultTexScale;
    out.bias = kDefaultTexBias;
    return true;
}

// TODO: complete testing of translateAffine on images
// TODO: complete testing of 'intermediate' capability
bool
InputTranslator::translateAffine(const std::string& name,
                                 const Input& in,
                                 float scale,
                                 float bias,
                                 Input& out,
                                 bool intermediate)
{
    out = in;
    if (in.image >= 0) {
        const ImageAsset& inImageAsset = mImagesSrc[in.image];
        std::string assetName = name + "-" + std::to_string(in.image);
        std::string key = assetName + "." + getFormatExtension(inImageAsset.format);
        int texture = -1;
        const auto& it = mCache.find(key);
        if (it != mCache.end()) {
            texture = it->second;
        } else {
            Image outImage;
            if (mExportImages) {
                auto [inImageValid, inImage] = getDecodedImage(in.image);
                GUARD(inImageValid, "Invalid image");
                imageTransformAffine(inImage, scale, bias, outImage);
            }
            texture =
              addImage(std::move(outImage), assetName, key, inImageAsset.format, intermediate);
        }
        out.image = texture;
    }

    if (in.value.IsHolding<float>()) {
        out.value = in.value.UncheckedGet<float>() * scale + bias;
    } else if (in.value.IsHolding<GfVec2f>()) {
        out.value = in.value.UncheckedGet<GfVec2f>() * scale + GfVec2f(bias);
    } else if (in.value.IsHolding<GfVec3f>()) {
        out.value = in.value.UncheckedGet<GfVec3f>() * scale + GfVec3f(bias);
    } else if (in.value.IsHolding<GfVec4f>()) {
        out.value = in.value.UncheckedGet<GfVec4f>() * scale + GfVec4f(bias);
    }

    // Clear the scale and bias since it was applied to the pixel values and the constants
    out.scale = kDefaultTexScale;
    out.bias = kDefaultTexBias;
    return true;
}

bool
InputTranslator::translatePhong2PBR(const Input& diffuseIn,
                                    const Input& specularIn,
                                    const Input& glosinessIn,
                                    // const Input& ns,
                                    Input& diffuseOut,
                                    Input& metallicOut,
                                    Input& roughnessOut)
{
    if (!diffuseIn.value.IsEmpty() && !diffuseIn.value.IsHolding<GfVec3f>())
        return false;
    if (!specularIn.value.IsEmpty() && !specularIn.value.IsHolding<GfVec3f>())
        return false;
    if (!glosinessIn.value.IsEmpty() && !glosinessIn.value.IsHolding<float>())
        return false;
    // Phong 2 PBR translation is costly, so we better skip it if we only have diffuse
    if (diffuseIn.image >= 0 && specularIn.image < 0 && glosinessIn.image < 0) {
        translateDirect(diffuseIn, diffuseOut);
    } else if (specularIn.image >= 0 || glosinessIn.image >= 0) {
        std::string key = "phong2pbr-" + std::to_string(diffuseIn.image) + "-" +
                          std::to_string(specularIn.image) + "-" +
                          std::to_string(glosinessIn.image);
        std::string diffuseKey = key + "-diff.png";
        std::string metallicKey = key + "-met.png";
        std::string roughnessKey = key + "-rou.png";

        int diffuseTexture = -1;
        int metallicTexture = -1;
        int roughnessTexture = -1;

        const auto& diffIt = mCache.find(diffuseKey);
        const auto& metIt = mCache.find(metallicKey);
        const auto& rouIt = mCache.find(roughnessKey);
        if (diffIt != mCache.end() && metIt != mCache.end() && rouIt != mCache.end()) {
            diffuseTexture = diffIt->second;
            metallicTexture = metIt->second;
            roughnessTexture = rouIt->second;
        } else {
            Image albedo;
            Image roughness;
            Image metallic;

            if (mExportImages) {
                Image diffuse;
                Image specular;
                Image shininess;
                // Only decode source textures that actually exist. An absent source
                // (image == -1) leaves its component empty; the empty-component handling
                // below substitutes a sensible default. A present but corrupt or over-sized
                // source still fails read() and aborts, preserving the dimension/overflow
                // guards.
                if (diffuseIn.image != -1)
                    GUARD(diffuse.read(mImagesSrc[diffuseIn.image], 3), "Invalid diffuse image");
                if (specularIn.image != -1)
                    GUARD(specular.read(mImagesSrc[specularIn.image], 3), "Invalid specular image");
                if (glosinessIn.image != -1)
                    GUARD(shininess.read(mImagesSrc[glosinessIn.image], 1), "Invalid gloss image");

                // We need to regularize dimensions. Diffuse component has priority.
                int width = diffuse.width;
                int height = diffuse.height;
                if (diffuse.pixels.empty()) {
                    width = std::max(specular.width, shininess.width);
                    height = std::max(specular.height, shininess.height);
                }

                bool diffuseEmpty = diffuse.pixels.empty();
                bool specularEmpty = specular.pixels.empty();
                bool shininessEmpty = shininess.pixels.empty();
                bool specularSize = width != specular.width || height != specular.height;
                bool shininessSize = width != shininess.width || height != shininess.height;

                // If non-empty, diffuse cannot possibly have invalid dimensions,
                // since in that case diffuse dictated the dimensions
                if (diffuseEmpty) {
                    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Diffuse component empty\n");
                    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Created diffuse component\n");
                    diffuse.allocate(width, height, 3);
                    diffuse.set(0.90196f, 0.90196f, 0.90196f, 1.0f);
                }
                if (specularEmpty || specularSize) {
                    if (specularEmpty) {
                        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Specular component empty\n");
                    }
                    if (specularSize) {
                        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Invalid specular size\n");
                    }
                    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Created specular component\n");
                    specular.allocate(width, height, 3);
                    specular.set(0.5f, 0.5f, 0.5f, 1.0f);
                }
                if (shininessEmpty || shininessSize) {
                    if (shininessEmpty) {
                        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Shininess component empty\n");
                    }
                    if (shininessSize) {
                        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Invalid shininess size\n");
                    }
                    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Created shininess component\n");
                    shininess.allocate(width, height, 1);
                    shininess.set(0.5f, 0.5f, 0.5f, 1.0f);
                }
                phongToPbr(diffuse, specular, shininess, albedo, roughness, metallic, 20);
            }

            diffuseTexture = mImagesDst.size();
            mImagesDst.push_back(ImageAsset());
            ImageAsset& diff = mImagesDst.back();
            diff.uri = diffuseKey;
            diff.name = TfToken(diffuseKey);
            diff.format = ImageFormatPng;
            albedo.write(diff); // no-op if texture empty

            metallicTexture = mImagesDst.size();
            mImagesDst.push_back(ImageAsset());
            ImageAsset& met = mImagesDst.back();
            met.uri = metallicKey;
            met.name = TfToken(metallicKey);
            met.format = ImageFormatPng;
            metallic.write(met); // no-op if texture empty

            roughnessTexture = mImagesDst.size();
            mImagesDst.push_back(ImageAsset());
            ImageAsset& rou = mImagesDst.back();
            rou.uri = roughnessKey;
            rou.name = TfToken(roughnessKey);
            rou.format = ImageFormatPng;
            roughness.write(rou); // no-op if texture empty

            mCache[diffuseKey] = diffuseTexture;
            mCache[metallicKey] = metallicTexture;
            mCache[roughnessKey] = roughnessTexture;
        }

        diffuseOut.image = diffuseTexture;
        diffuseOut.uvIndex = 0;
        diffuseOut.channel = AdobeTokens->rgb;
        diffuseOut.wrapS = AdobeTokens->repeat;
        diffuseOut.wrapT = AdobeTokens->repeat;
        diffuseOut.colorspace = AdobeTokens->sRGB;

        metallicOut.image = metallicTexture;
        metallicOut.uvIndex = 0;
        metallicOut.channel = AdobeTokens->r;
        metallicOut.wrapS = AdobeTokens->repeat;
        metallicOut.wrapT = AdobeTokens->repeat;
        metallicOut.colorspace = AdobeTokens->raw;

        roughnessOut.image = roughnessTexture;
        roughnessOut.uvIndex = 0;
        roughnessOut.channel = AdobeTokens->r;
        roughnessOut.wrapS = AdobeTokens->repeat;
        roughnessOut.wrapT = AdobeTokens->repeat;
        roughnessOut.colorspace = AdobeTokens->raw;
    } else if (!diffuseIn.value.IsEmpty() && specularIn.value.IsEmpty() &&
               glosinessIn.value.IsEmpty()) {
        diffuseOut.value = diffuseIn.value;
    } else if (!specularIn.value.IsEmpty() || !glosinessIn.value.IsEmpty()) {
        GfVec3f diffuseValue = diffuseIn.value.Get<GfVec3f>();
        GfVec3f specularValue =
          !specularIn.value.IsEmpty() ? specularIn.value.Get<GfVec3f>() : GfVec3f(.5);
        float shininessValue =
          glosinessIn.value.IsHolding<float>() ? glosinessIn.value.UncheckedGet<float>() : 0.5f;
        // float shininess = m.ns == -1 ? 1 : m.ns;
        GfVec3f albedo;
        float roughness;
        float metallic;
        phongToPbr(diffuseValue, specularValue, shininessValue, albedo, roughness, metallic, 1);
        diffuseOut.value = albedo;
        roughnessOut.value = roughness;
        metallicOut.value = metallic;
    }
    return true;
}

bool
InputTranslator::translatePhong2Roughness(const Input& specularIn,
                                          const Input& shininessIn,
                                          Input& roughnessOut)
{
    if (!specularIn.value.IsEmpty() && !specularIn.value.IsHolding<GfVec3f>())
        return false;
    if (!shininessIn.value.IsEmpty() && !shininessIn.value.IsHolding<float>())
        return false;

    // Texture-based path: fall back to full phong-to-PBR image bake, but discard the metallic
    // and diffuse outputs — we only keep the roughness texture.
    if (specularIn.image >= 0 || shininessIn.image >= 0) {
        Input unusedDiffuse;
        Input unusedMetallic;
        return translatePhong2PBR(
          Input{}, specularIn, shininessIn, unusedDiffuse, unusedMetallic, roughnessOut);
    }

    // Value-based path: compute roughness directly without the full metallic solve.
    GfVec3f specularValue =
      !specularIn.value.IsEmpty() ? specularIn.value.Get<GfVec3f>() : GfVec3f(0.5f);
    float shininessValue =
      shininessIn.value.IsHolding<float>() ? shininessIn.value.UncheckedGet<float>() : 0.5f;
    roughnessOut.value = phongRoughness(shininessValue, specularValue, 1);
    return true;
}

bool
InputTranslator::translateNormals(const Input& bumpIn, const Input& normalsIn, Input& normalsOut)
{
    if (normalsIn.image >= 0) {
        translateDirect(normalsIn, normalsOut);
    } else if (bumpIn.image >= 0) {
        int normalTexture = -1;
        std::string key = "bump2Normal-" + std::to_string(bumpIn.image) + ".png";
        if (const auto& it = mCache.find(key); it != mCache.end()) {
            normalTexture = it->second;
        } else {
            Image normal;
            if (mExportImages) {
                const ImageAsset& bumpAsset = mImagesSrc[bumpIn.image];
                Image bump;
                GUARD(bump.read(bumpAsset, 1), "Invalid bump image");
                bumpToNormal(bump, normal, 3);
            }
            normalTexture = mImagesDst.size();
            mImagesDst.push_back(ImageAsset());
            ImageAsset& norm = mImagesDst.back();
            norm.uri = key;
            norm.name = key;
            norm.format = ImageFormatPng;
            normal.write(norm); // no-op if texture empty
        }
        normalsOut.image = normalTexture;
        normalsOut.uvIndex = 0;
        normalsOut.channel = AdobeTokens->rgb;
        normalsOut.wrapS = AdobeTokens->repeat;
        normalsOut.wrapT = AdobeTokens->repeat;
    }
    normalsOut.colorspace = AdobeTokens->raw;
    normalsOut.scale = kOpenGLNormalTexScale;
    normalsOut.bias = kOpenGLNormalTexBias;
    return true;
}

bool
InputTranslator::translateTransparency2Opacity(const Input& transparency, Input& opacity)
{
    translateDirect(transparency, opacity);
    opacity.scale = GfVec4f(-1);
    opacity.bias = GfVec4f(1);
    if (transparency.value.IsHolding<float>()) {
        opacity.value = 1 - transparency.value.UncheckedGet<float>();
    }
    return true;
}

bool
InputTranslator::translateOpacity2Transparency(const Input& opacity, Input& transparency)
{
    if (opacity.image >= 0) {
        int channelIndex = token2Channel(opacity.channel);
        if (channelIndex < 0)
            channelIndex = 0;
        float newscale = -1.0f * opacity.scale[channelIndex];
        float newbias = 1.0 - opacity.bias[channelIndex];

        // if there is already an inversion applied, we don't need to do anything
        if (newscale == 1.0f && newbias == 0.0f) {
            bool result = translateDirect(opacity, transparency, false);
            transparency.scale = kDefaultTexScale;
            transparency.bias = kDefaultTexBias;
            return result;
        } else {
            // invert the source scale/bias and apply to source opacity image to get new
            // transparency image
            return translateToSingleAffine(
              "transparency", opacity, -1.0f, 1.0f, transparency, false);
        }
    } else {
        translateDirect(opacity, transparency);
    }

    if (opacity.value.IsHolding<float>()) {
        transparency.value = 1 - opacity.value.UncheckedGet<float>();
    }
    return true;
}

bool
InputTranslator::translateAmbient2Occlusion(const Input& ambient, Input& occlusion)
{
    // if (ambient.pixels) {
    //     int index = readUSDImage(usd, ambient, name + "occlusion");
    //     readUSDTexture(index, um.occlusion, "r");
    // } else if (!fbxsdk::FbxProperty::HasDefaultValue(lambert->Ambient) &&
    //             !fbxsdk::FbxProperty::HasDefaultValue(lambert->AmbientFactor)
    // ) {
    //     GfVec3f v = readPropValue(lambert->Ambient);
    //     float f = readPropValue(lambert->AmbientFactor);
    //     float vMagnitude = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    //     vMagnitude = std::min(1.0f, std::max(0.0f, vMagnitude));
    //     um.occlusion.value = true;
    //     um.occlusion.value = vMagnitude * f;
    // }
    return true;
}

void
_collect2DTransformValues(const Input& input,
                          std::vector<float>& rotations,
                          std::vector<GfVec2f>& scales,
                          std::vector<GfVec2f>& translations)
{
    // We are only interested in 2d transform values when there is a texture
    if (input.image >= 0) {
        rotations.push_back(input.uvRotation);
        scales.push_back(input.uvScale);
        translations.push_back(input.uvTranslation);
    }
}

bool
InputTranslator::translateMultiscatterToSingleScatter(const std::string& name,
                                                      const Input& in,
                                                      float anisotropy,
                                                      Input& out,
                                                      bool intermediate)
{
    out = in;
    if (intermediate) {
        return true;
    }

    if (in.image >= 0) {
        // The scale components represent the multiscatterColorFactor (per-channel tint).
        // Because the multiscatter→single-scatter conversion is nonlinear, the factor must be
        // applied per-texel before the conversion rather than carried along as metadata.
        // Include the scale in the cache key so that different factors produce distinct images.
        std::string key =
          name + "-" + input2key(in.image, in.channel, 0) + "-singlescatter-" +
          TfStringPrintf("%.4f-%.4f-%.4f-%.4f", in.scale[0], in.scale[1], in.scale[2], anisotropy) +
          ".png";
        int texture = -1;
        const auto it = mCache.find(key);
        if (it != mCache.end()) {
            texture = it->second;
        } else {
            Image outImage;
            if (mExportImages) {
                auto [inImageValid, inImage] = getDecodedImage(in.image);
                GUARD(inImageValid, "Invalid image");
                const int outputChannels = inImage.channels >= 4 ? 4 : 3;
                if (!outImage.allocate(inImage.width, inImage.height, outputChannels)) {
                    TF_WARN("Failed to allocate output image for %s", key.c_str());
                    return false;
                }
                const int pixelCount = inImage.width * inImage.height;
                for (int i = 0; i < pixelCount; ++i) {
                    const int inIdx = i * inImage.channels;
                    const int outIdx = i * outputChannels;
                    // Apply the multiscatterColorFactor to each channel before converting to
                    // single-scatter albedo.  Clamp to [0,1] since the formula requires it.
                    const float rawR = inImage.pixels[inIdx + 0];
                    const float rawG = inImage.channels >= 3 ? inImage.pixels[inIdx + 1] : rawR;
                    const float rawB = inImage.channels >= 3 ? inImage.pixels[inIdx + 2] : rawR;
                    const float r = std::clamp(rawR * in.scale[0], 0.0f, 1.0f);
                    const float g = std::clamp(rawG * in.scale[1], 0.0f, 1.0f);
                    const float b = std::clamp(rawB * in.scale[2], 0.0f, 1.0f);
                    outImage.pixels[outIdx + 0] = multiscatterToSingleScatter(r, anisotropy);
                    outImage.pixels[outIdx + 1] = multiscatterToSingleScatter(g, anisotropy);
                    outImage.pixels[outIdx + 2] = multiscatterToSingleScatter(b, anisotropy);
                    if (outputChannels == 4) {
                        outImage.pixels[outIdx + 3] =
                          inImage.channels >= 4 ? inImage.pixels[inIdx + 3] : 1.0f;
                    }
                }
            }
            texture = addImage(std::move(outImage), key, key, ImageFormatPng, false);
            mCache[key] = texture;
        }
        out.image = texture;
        // The factor has been consumed into the pixel values; reset to default (white) so
        // it is not re-emitted as a separate multiscatterColorFactor on export.
        out.scale = GfVec4f(1.0f);
        return true;
    }

    if (in.value.IsHolding<float>()) {
        out.value = multiscatterToSingleScatter(in.value.UncheckedGet<float>(), anisotropy);
        return true;
    } else if (in.value.IsHolding<GfVec3f>()) {
        const GfVec3f multiscatter = in.value.UncheckedGet<GfVec3f>();
        out.value = GfVec3f(multiscatterToSingleScatter(multiscatter[0], anisotropy),
                            multiscatterToSingleScatter(multiscatter[1], anisotropy),
                            multiscatterToSingleScatter(multiscatter[2], anisotropy));
        return true;
    }

    return !in.value.IsEmpty();
}

bool
InputTranslator::translateSingleScatterToMultiscatter(const std::string& name,
                                                      const Input& in,
                                                      float anisotropy,
                                                      Input& out,
                                                      bool intermediate)
{
    out = in;
    if (intermediate) {
        return true;
    }

    if (in.image >= 0) {
        // The scale components represent the single-scatter albedo factor.  Because the
        // single-scatter→multiscatter conversion is nonlinear, the factor must be applied
        // per-texel before the conversion rather than carried along as metadata.
        // Include the scale in the cache key so that different factors produce distinct images.
        std::string key =
          name + "-" + input2key(in.image, in.channel, 0) + "-multiscatter-" +
          TfStringPrintf("%.4f-%.4f-%.4f-%.4f", in.scale[0], in.scale[1], in.scale[2], anisotropy) +
          ".png";
        int texture = -1;
        const auto it = mCache.find(key);
        if (it != mCache.end()) {
            texture = it->second;
        } else {
            Image outImage;
            if (mExportImages) {
                auto [inImageValid, inImage] = getDecodedImage(in.image);
                GUARD(inImageValid, "Invalid image");
                const int outputChannels = inImage.channels >= 4 ? 4 : 3;
                if (!outImage.allocate(inImage.width, inImage.height, outputChannels)) {
                    TF_WARN("Failed to allocate output image for %s", key.c_str());
                    return false;
                }
                const int pixelCount = inImage.width * inImage.height;
                for (int i = 0; i < pixelCount; ++i) {
                    const int inIdx = i * inImage.channels;
                    const int outIdx = i * outputChannels;
                    // Apply the single-scatter factor to each channel before converting to
                    // multiscatter albedo.  Clamp to [0,1] since the formula requires it.
                    const float rawR = inImage.pixels[inIdx + 0];
                    const float rawG = inImage.channels >= 3 ? inImage.pixels[inIdx + 1] : rawR;
                    const float rawB = inImage.channels >= 3 ? inImage.pixels[inIdx + 2] : rawR;
                    const float r = std::clamp(rawR * in.scale[0], 0.0f, 1.0f);
                    const float g = std::clamp(rawG * in.scale[1], 0.0f, 1.0f);
                    const float b = std::clamp(rawB * in.scale[2], 0.0f, 1.0f);
                    outImage.pixels[outIdx + 0] = singleScatterToMultiscatter(r, anisotropy);
                    outImage.pixels[outIdx + 1] = singleScatterToMultiscatter(g, anisotropy);
                    outImage.pixels[outIdx + 2] = singleScatterToMultiscatter(b, anisotropy);
                    if (outputChannels == 4) {
                        outImage.pixels[outIdx + 3] =
                          inImage.channels >= 4 ? inImage.pixels[inIdx + 3] : 1.0f;
                    }
                }
            }
            // Before adding an intermediate image to mImagesSrc, ensure mDecodedImages is
            // sized to match so the new entry lands at the correct index.
            while (mDecodedImages.size() < mImagesSrc.size()) {
                mDecodedImages.push_back(Image());
                mDecodedMap.push_back(false);
            }
            // Store as intermediate (mImagesSrc) so that the caller's translateDirect step
            // correctly encodes and references this image via translateDirectInternal.
            // Using intermediate=false would put it in mImagesDst with an index that
            // translateDirect would then misinterpret as a mImagesSrc index.
            texture = addImage(std::move(outImage), key, key, ImageFormatPng, true);
            mCache[key] = texture;
        }
        out.image = texture;
        // The factor has been consumed into the pixel values; reset to default (white) so
        // it is not re-emitted as a separate multiscatterColorFactor on export.
        out.scale = GfVec4f(1.0f);
        return true;
    }

    if (in.value.IsHolding<float>()) {
        out.value = singleScatterToMultiscatter(in.value.UncheckedGet<float>(), anisotropy);
        return true;
    } else if (in.value.IsHolding<GfVec3f>()) {
        const GfVec3f singleScatter = in.value.UncheckedGet<GfVec3f>();
        out.value = GfVec3f(singleScatterToMultiscatter(singleScatter[0], anisotropy),
                            singleScatterToMultiscatter(singleScatter[1], anisotropy),
                            singleScatterToMultiscatter(singleScatter[2], anisotropy));
        return true;
    }

    return !in.value.IsEmpty();
}

bool
InputTranslator::translateMix(const std::string& name,
                              const TfToken& colorspace,
                              const Input& in0,
                              const Input& in1,
                              const Input& in2,
                              const Input& in3,
                              Input& out)
{
    int im0 = in0.image;
    int im1 = in1.image;
    int im2 = in2.image;
    int im3 = in3.image;
    int ch0 = in0.image >= 0 ? token2Channel(in0.channel) : -1;
    int ch1 = in1.image >= 0 ? token2Channel(in1.channel) : -1;
    int ch2 = in2.image >= 0 ? token2Channel(in2.channel) : -1;
    int ch3 = in3.image >= 0 ? token2Channel(in3.channel) : -1;
    float val0 = in0.value.IsHolding<float>() ? in0.value.UncheckedGet<float>() : 0.0f;
    float val1 = in1.value.IsHolding<float>() ? in1.value.UncheckedGet<float>() : 0.0f;
    float val2 = in2.value.IsHolding<float>() ? in2.value.UncheckedGet<float>() : 0.0f;
    float val3 = in3.value.IsHolding<float>() ? in3.value.UncheckedGet<float>() : 0.0f;
    uint8_t vali0 = static_cast<uint8_t>(val0);
    uint8_t vali1 = static_cast<uint8_t>(val1);
    uint8_t vali2 = static_cast<uint8_t>(val2);
    uint8_t vali3 = static_cast<uint8_t>(val3);
    if (!in0.value.IsEmpty() || !in1.value.IsEmpty() || !in2.value.IsEmpty() ||
        !in3.value.IsEmpty()) {
        out.value = GfVec4f(val0, val1, val2, val3);
    }
    if ((im0 >= 0 && ch0 != -1) || (im1 >= 0 && ch1 != -1) || (im2 >= 0 && ch2 != -1) ||
        (im3 >= 0 && ch3 != -1)) {

        // Check if the inputs are actually the same image and channels simply being copied
        int validImage = im0 != -1 ? im0 : im1 != -1 ? im1 : im2 != -1 ? im2 : im3 != -1 ? im3 : -1;
        bool sameImage = (im0 == -1 || im0 == validImage) && (im1 == -1 || im1 == validImage) &&
                         (im2 == -1 || im2 == validImage) && (im3 == -1 || im3 == validImage);
        bool sameChannels = (im0 == -1 || ch0 == 0) && (im1 == -1 || ch1 == 1) &&
                            (im2 == -1 || ch2 == 2) && (im3 == -1 || ch3 == 3);
        if (sameImage && sameChannels) {
            // Use existing infrastructure to translate the image directly, so it can be found in
            // the cache if translateDirect has already been called, and translateDirect can find
            // it if called later
            translateDirectInternal(validImage, out);
        } else {
            std::string key = name + "-" + input2key(im0, in0.channel, vali0) + "-" +
                              input2key(im1, in1.channel, vali1) + "-" +
                              input2key(im2, in2.channel, vali2) + "-" +
                              input2key(im3, in3.channel, vali3);
            int imageIndex = -1;
            const auto& it = mCache.find(key);
            if (it != mCache.end()) {
                imageIndex = it->second;
            } else {
                imageIndex = mImagesDst.size();
                mImagesDst.push_back(ImageAsset());
                if (mExportImages) {
                    ImageAsset& newImage = mImagesDst.back();
                    Image mixed;
                    auto copyChannel = [&](int image, int channelSrc, int channelDst) -> bool {
                        if (image == -1 || channelSrc == -1) {
                            return true;
                        }
                        auto [imageValid, imageSrc] = getDecodedImage(image);
                        if (!imageValid) {
                            return false;
                        }
                        if (mixed.pixels.empty()) {
                            mixed.allocate(imageSrc.width, imageSrc.height, 4);
                            mixed.set(val0, val1, val2, val3);
                        }
                        mixed.copyChannel(imageSrc, channelSrc, channelDst);
                        return true;
                    };
                    GUARD(copyChannel(im0, ch0, 0), "Invalid source image for channel 0");
                    GUARD(copyChannel(im1, ch1, 1), "Invalid source image for channel 1");
                    GUARD(copyChannel(im2, ch2, 2), "Invalid source image for channel 2");
                    GUARD(copyChannel(im3, ch3, 3), "Invalid source image for channel 3");
                    newImage.uri = key + ".png";
                    newImage.name = key;
                    // We need to set the format before we can write to it
                    newImage.format = ImageFormatPng;
                    mixed.write(newImage);
                }
                mCache[key] = imageIndex;
            }
            out.image = imageIndex;
        }
        out.uvIndex = 0;
        out.channel = AdobeTokens->rgba;
        out.wrapS = AdobeTokens->repeat;
        out.wrapT = AdobeTokens->repeat;
        out.colorspace = colorspace;
        out.scale = GfVec4f(in0.scale[0], in1.scale[1], in2.scale[2], in3.scale[3]);
        out.bias = GfVec4f(in0.bias[0], in1.bias[1], in2.bias[2], in3.bias[3]);

        // collect all the 2d transforms for each input into separate arrays so we can
        // check each set for equality and then assign to the output or issue a warning.
        std::vector<float> rotations;
        std::vector<GfVec2f> scales;
        std::vector<GfVec2f> translations;
        rotations.reserve(4);
        scales.reserve(4);
        translations.reserve(4);
        _collect2DTransformValues(in0, rotations, scales, translations);
        _collect2DTransformValues(in1, rotations, scales, translations);
        _collect2DTransformValues(in2, rotations, scales, translations);
        _collect2DTransformValues(in3, rotations, scales, translations);

        if (_valuesAreEqual(rotations)) {
            if (!rotations.empty()) {
                out.uvRotation = rotations[0];
            }
        } else {
            TF_WARN("Cannot copy uvRotation as inputs differ.");
        }
        if (_valuesAreEqual(scales)) {
            if (!scales.empty()) {
                out.uvScale = scales[0];
            }
        } else {
            TF_WARN("Cannot copy uvScale as inputs differ.");
        }
        if (_valuesAreEqual(translations)) {
            if (!translations.empty()) {
                out.uvTranslation = translations[0];
            }
        } else {
            TF_WARN("Cannot copy uvTranslation as inputs differ.");
        }
    }
    return true;
}

const ImageAsset&
InputTranslator::getImage(int i) const
{
    static ImageAsset defaultImage;

    if (i >= 0 && (size_t)i < mImagesDst.size()) {
        return mImagesDst[i];
    } else {
        TF_WARN("Image index doesn't exist: %d  returning default ImageAsset", i);
        return defaultImage;
    }
}

std::vector<ImageAsset>&
InputTranslator::getImages()
{
    return mImagesDst;
}

std::string
InputTranslator::getImageSourceName(int index) const
{
    if (index >= 0 && (size_t)index < mImagesSrc.size()) {
        return mImagesSrc[index].name;
    } else {
        TF_WARN("Image index doesn't exist: %d  returning empty string", index);
        return "";
    }
}

Input
InputTranslator::split3f(const Input& in, int channel)
{
    Input out = in;
    out.value = in.value.IsHolding<GfVec3f>() ? VtValue(in.value.UncheckedGet<GfVec3f>()[channel])
                                              : VtValue();
    out.channel = channel2Token(channel);
    return out;
}

std::pair<GfVec4f, GfVec4f>
InputTranslator::computeRange(const Input& input)
{
    std::pair<GfVec4f, GfVec4f> result = { GfVec4f(FLT_MAX), GfVec4f(-FLT_MAX) };
    if (input.image != -1) {
        auto [imageValid, imageSrc] = getDecodedImage(input.image);
        if (imageValid) {
            result = imageSrc.computeRange();
        }
    } else if (input.value.IsHolding<float>()) {
        float r = input.value.UncheckedGet<float>();
        result.first[0] = r;
        result.second[0] = r;
    } else if (input.value.IsHolding<GfVec3f>()) {
        GfVec3f rgb = input.value.UncheckedGet<GfVec3f>();
        result.first[0] = rgb[0];
        result.first[1] = rgb[1];
        result.first[2] = rgb[2];
        result.second[0] = rgb[0];
        result.second[1] = rgb[1];
        result.second[2] = rgb[2];
    } else if (input.value.IsHolding<GfVec4f>()) {
        GfVec4f rgba = input.value.UncheckedGet<GfVec4f>();
        result.first[0] = rgba[0];
        result.first[1] = rgba[1];
        result.first[2] = rgba[2];
        result.first[3] = rgba[3];
        result.second[0] = rgba[0];
        result.second[1] = rgba[1];
        result.second[2] = rgba[2];
        result.second[3] = rgba[3];
    }

    return result;
}

std::pair<bool, Image&>
InputTranslator::getDecodedImage(int index)
{
    static Image defaultImage;
    if (index < 0 || index >= (int)mDecodedMap.size()) {
        TF_WARN("Invalid image index: %d", index);
        return { false, defaultImage };
    }
    if (mDecodedMap[index]) {
        return { true, mDecodedImages[index] };
    } else {
        ImageAsset& imageAsset = mImagesSrc[index];
        mDecodedMap[index] = mDecodedImages[index].read(imageAsset);
        if (!mDecodedMap[index]) {
            TF_RUNTIME_ERROR("Couldn't read image %s (index %d)", imageAsset.uri.c_str(), index);
        }
        return { mDecodedMap[index], mDecodedImages[index] };
    }
}

int
InputTranslator::addImage(Image&& image,
                          const std::string& assetName,
                          const std::string& assetUri,
                          ImageFormat format,
                          bool intermediate)
{
    if (intermediate) {
        // Store the image directly as decoded, so that we can retrieve it immediately
        mDecodedImages.push_back(std::move(image));
        mDecodedMap.push_back(true);
        // We do not put this image into the imageAsset to not pay for encoding the image
        // Also note, we store this in the mImagesSrc and not mImagesDst, since this is an
        // intermediate image.
        int texture = mImagesSrc.size();
        mImagesSrc.push_back(ImageAsset());
        // Store the name for debugging purposes. This asset is never loaded from or written to
        // disk, since it's intermediate. Also, the encoded format is still ImageFormatUnknown
        ImageAsset& imageAsset = mImagesSrc.back();
        imageAsset.name = assetName;
        imageAsset.uri = assetName;
        return texture;
    } else {
        ImageAsset imageAsset;
        imageAsset.name = assetName;
        imageAsset.uri = assetUri;
        // Note, the format of the image asset needs to be set, otherwise the writing/encoding
        // will not work
        imageAsset.format = format;
        image.write(imageAsset);
        return addImage(std::move(imageAsset));
    }
}

int
InputTranslator::addImage(ImageAsset&& image)
{
    int texture = mImagesDst.size();
    mImagesDst.push_back(std::move(image));
    return texture;
}

}
