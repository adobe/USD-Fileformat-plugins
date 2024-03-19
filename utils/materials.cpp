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
#include "materials.h"
#include "common.h"
#include "debugCodes.h"
#include "images.h"
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
    // Attenuate specular and shininess, so higher metallics are not excessive (experimental)
    float k = .5;
    specular = GfVec3f(specular[0] - k * specular[0] * specular[0],
                       specular[1] - k * specular[1] * specular[1],
                       specular[2] - k * specular[2] * specular[2]);
    float k2 = .5;
    shininess = shininess - k2 * shininess * shininess / 1000;

    float dsr = 0.04; // dielectricSpecularReflectance
    float specularIntensity = 0.2125 * specular[0] + 0.7154 * specular[1] + 0.0721 * specular[2];
    float diffuseBrightness = 0.299 * diffuse[0] * diffuse[0] + 0.587 * diffuse[1] * diffuse[1] +
                              0.114 * diffuse[2] * diffuse[2];
    float specularBrightness = 0.299 * specular[0] * specular[0] +
                               0.587 * specular[1] * specular[1] +
                               0.114 * specular[2] * specular[2];
    float specularStrength = std::max(specular[0], std::max(specular[1], specular[2]));
    roughness = sqrt(2 / (shininessFactor * shininess * specularIntensity + 2));

    float specComplement = 1 - specularStrength;
    float A = dsr;
    float B = (diffuseBrightness * (specComplement / (1 - A)) + specularBrightness) - 2 * A;
    float C = A - specularBrightness;
    float squareRoot = sqrt(std::max(0.0f, B * B - 4 * A * C));
    float value = (-B + squareRoot) / (2 * A);
    metallic = std::min(1.0f, std::max(0.0f, value));

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

bool
InputTranslator::translateDirect(const Input& in, Input& out, bool intermediate)
{
    if (intermediate) {
        out = in;
        return true;
    }

    if (in.image >= 0) {
        out = in;
        const ImageAsset& asset = mImagesSrc[in.image];
        int imageIndex = -1;
        std::string key = "direct-" + TfGetBaseName(asset.uri);
        const auto& it = mCache.find(key);
        if (it != mCache.end()) {
            imageIndex = it->second;
        } else {
            imageIndex = mImagesDst.size();
            mImagesDst.push_back(ImageAsset());
            ImageAsset& newAsset = mImagesDst.back();
            newAsset.uri = key;
            newAsset.name = asset.name;
            newAsset.format = asset.format;
            newAsset.image = asset.image; // create a copy
            mCache[key] = imageIndex;
        }
        out.image = imageIndex;
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
        GfVec4f srcScale = in.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
        GfVec4f srcBias = in.bias.GetWithDefault<GfVec4f>(GfVec4f(0.0f));
        GfVec4f newscale = scale * srcScale;
        GfVec4f newbias = scale * srcBias + GfVec4f(bias);
        if (newscale != GfVec4f(1.0f)) {
            out.scale = newscale;
        } else {
            out.scale = VtValue();
        }
        if (newbias != GfVec4f(0.0f)) {
            out.bias = newbias;
        } else {
            out.bias = VtValue();
        }
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
        std::string key = "factor-" + std::to_string(in.image) + "-" +
                          std::to_string(factor.image) + "." +
                          getFormatExtension(inImageAsset.format);
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
            imageIndex = addImage(std::move(outImage), key, inImageAsset.format, intermediate);
        }
        // Copy the input image's settings and update to the new image index
        out = in;
        out.image = imageIndex;
    } else if (in.image >= 0) {
        // in input is an image and factor is a single float
        translateDirect(in, out, intermediate);
        float f = factor.value.GetWithDefault(1.0f);
        if (f != 1.0f) {
            if (out.scale.IsHolding<GfVec4f>()) {
                GfVec4f scale = out.scale.UncheckedGet<GfVec4f>();
                scale *= f;
                out.scale = scale;
            } else {
                out.scale = GfVec4f(f);
            }
        }
    } else if (factor.image >= 0) {
        // factor is an image and the in input is a constant value
        translateDirect(factor, out, intermediate);
        GfVec4f scale = out.scale.GetWithDefault(GfVec4f(1.0f));
        if (in.value.IsHolding<float>()) {
            float f = in.value.UncheckedGet<float>();
            scale *= f;
        } else if (in.value.IsHolding<GfVec2f>()) {
            const GfVec2f& v = in.value.UncheckedGet<GfVec2f>();
            scale[0] *= v[0];
            scale[1] *= v[1];
        } else if (in.value.IsHolding<GfVec3f>()) {
            const GfVec3f& v = in.value.UncheckedGet<GfVec3f>();
            scale[0] *= v[0];
            scale[1] *= v[1];
            scale[2] *= v[2];
        } else if (in.value.IsHolding<GfVec4f>()) {
            const GfVec4f& v = in.value.UncheckedGet<GfVec4f>();
            scale[0] *= v[0];
            scale[1] *= v[1];
            scale[2] *= v[2];
            scale[3] *= v[3];
        } else {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "translateFactor in input is not holding a float value\n");
        }
        out.scale = scale;
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

    GfVec4f srcScale = in.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
    GfVec4f srcBias = in.bias.GetWithDefault<GfVec4f>(GfVec4f(0.0f));
    // apply scale and bias to source channel scale and bias
    float newscale = scale * srcScale[channelIndex];
    float newbias = scale * srcBias[channelIndex] + bias;

    if (in.image >= 0) {
        const ImageAsset& inImageAsset = mImagesSrc[in.image];
        std::string key = name + "-" + input2key(in.image, channelIndex) + "." +
                          getFormatExtension(inImageAsset.format);
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
                        out.scale = VtValue();
                        out.bias = VtValue();
                    }
                    return result;
                } else {
                    // apply scale and bias to source channel and store in single channel outImage
                    imageExtractChannel(inImage, channelIndex, newscale, newbias, outImage);
                }
            }
            texture = addImage(std::move(outImage), key, inImageAsset.format, false);
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
    out.scale = VtValue();
    out.bias = VtValue();
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
        std::string key =
          name + "-" + std::to_string(in.image) + "." + getFormatExtension(inImageAsset.format);
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
            texture = addImage(std::move(outImage), key, inImageAsset.format, intermediate);
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
    out.scale = VtValue();
    out.bias = VtValue();
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
                // Whether textures exist or not, first attempt to decode what we can.
                const ImageAsset& diffAsset =
                  diffuseIn.image != -1 ? mImagesSrc[diffuseIn.image] : ImageAsset();
                const ImageAsset& specAsset =
                  specularIn.image != -1 ? mImagesSrc[specularIn.image] : ImageAsset();
                const ImageAsset& glossAsset =
                  glosinessIn.image != -1 ? mImagesSrc[glosinessIn.image] : ImageAsset();
                Image diffuse;
                Image specular;
                Image shininess;
                GUARD(diffuse.read(diffAsset, 3), "Invalid diffuse image");
                GUARD(specular.read(specAsset, 3), "Invalid specular image");
                GUARD(shininess.read(glossAsset, 1), "Invalid gloss image");

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
        float shininessValue = !glosinessIn.value.IsEmpty() ? glosinessIn.value.Get<float>() : .5;
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
    normalsOut.scale = GfVec4f(2);
    normalsOut.bias = GfVec4f(-1);
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
        GfVec4f srcScale = opacity.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
        GfVec4f srcBias = opacity.bias.GetWithDefault<GfVec4f>(GfVec4f(0.0f));
        int channelIndex = token2Channel(opacity.channel);
        if (channelIndex < 0)
            channelIndex = 0;
        float newscale = -1.0f * srcScale[channelIndex];
        float newbias = 1.0 - srcBias[channelIndex];

        // if there is already an inversion applied, we don't need to do anything
        if (newscale == 1.0f && newbias == 0.0f) {
            bool result = translateDirect(opacity, transparency, false);
            transparency.scale = VtValue();
            transparency.bias = VtValue();
            return result;
        } else {
            // invert the source scale/bias and apply to source opacity image to get new
            // transparency image
            return translateToSingleAffine("transparency", opacity, -1.0f, 1.0f, transparency, false);
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
                          std::vector<VtValue>& rotations,
                          std::vector<VtValue>& scales,
                          std::vector<VtValue>& translations)
{
    // We are only interested in 2d transform values when there is a texture
    if (input.image >= 0) {
        rotations.push_back(input.transformRotation);
        scales.push_back(input.transformScale);
        translations.push_back(input.transformTranslation);
    }
}

bool
_valuesAreEqual(const std::vector<VtValue>& values)
{
    for (size_t i = 1; i < values.size(); ++i) {
        if (values[0] != values[i])
            return false;
    }
    return true;
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
                int validImage = im0 != -1   ? im0
                                 : im1 != -1 ? im1
                                 : im2 != -1 ? im2
                                 : im3 != -1 ? im3
                                             : -1;
                bool sameImage =
                  (im0 == -1 || im0 == validImage) && (im1 == -1 || im1 == validImage) &&
                  (im2 == -1 || im2 == validImage) && (im3 == -1 || im3 == validImage);
                bool sameChannels = (im0 == -1 || ch0 == 0) && (im1 == -1 || ch1 == 1) &&
                                    (im2 == -1 || ch2 == 2) && (im3 == -1 || ch3 == 3);
                ImageAsset& newImage = mImagesDst.back();
                if (sameImage && sameChannels) {
                    ImageAsset& image = mImagesSrc[validImage];
                    newImage.uri = key + "." + getFormatExtension(image.format);
                    newImage.name = key;
                    newImage.format = image.format;
                    newImage.image = std::move(image.image);
                } else {
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
            }
            TF_DEBUG_MSG(FILE_FORMAT_UTIL, "key: %s\n", key.c_str());
            mCache[key] = imageIndex;
        }
        out.image = imageIndex;
        out.uvIndex = 0;
        out.channel = AdobeTokens->rgba;
        out.wrapS = AdobeTokens->repeat;
        out.wrapT = AdobeTokens->repeat;
        out.colorspace = colorspace;
        if (!in0.scale.IsEmpty() || !in1.scale.IsEmpty() || !in2.scale.IsEmpty() ||
            !in3.scale.IsEmpty()) {
            float scale0 =
              in0.scale.IsHolding<GfVec4f>() ? in0.scale.UncheckedGet<GfVec4f>()[0] : 1;
            float scale1 =
              in1.scale.IsHolding<GfVec4f>() ? in1.scale.UncheckedGet<GfVec4f>()[1] : 1;
            float scale2 =
              in2.scale.IsHolding<GfVec4f>() ? in2.scale.UncheckedGet<GfVec4f>()[2] : 1;
            float scale3 =
              in3.scale.IsHolding<GfVec4f>() ? in3.scale.UncheckedGet<GfVec4f>()[3] : 1;
            out.scale = GfVec4f(scale0, scale1, scale2, scale3);
        }
        if (!in0.bias.IsEmpty() || !in1.bias.IsEmpty() || !in2.bias.IsEmpty() ||
            !in3.bias.IsEmpty()) {
            float bias0 = in0.bias.IsHolding<GfVec4f>() ? in0.bias.UncheckedGet<GfVec4f>()[0] : 0;
            float bias1 = in1.bias.IsHolding<GfVec4f>() ? in1.bias.UncheckedGet<GfVec4f>()[1] : 0;
            float bias2 = in2.bias.IsHolding<GfVec4f>() ? in2.bias.UncheckedGet<GfVec4f>()[2] : 0;
            float bias3 = in3.bias.IsHolding<GfVec4f>() ? in3.bias.UncheckedGet<GfVec4f>()[3] : 0;
            out.bias = GfVec4f(bias0, bias1, bias2, bias3);
        }

        // collect all the 2d transforms for each input into separate arrays so we can
        // check each set for equality and then assign to the output or issue a warning.
        std::vector<PXR_NS::VtValue> rotations;
        std::vector<PXR_NS::VtValue> scales;
        std::vector<PXR_NS::VtValue> translations;
        rotations.reserve(4);
        scales.reserve(4);
        translations.reserve(4);
        _collect2DTransformValues(in0, rotations, scales, translations);
        _collect2DTransformValues(in1, rotations, scales, translations);
        _collect2DTransformValues(in2, rotations, scales, translations);
        _collect2DTransformValues(in3, rotations, scales, translations);

        if (_valuesAreEqual(rotations)) {
            out.transformRotation = (rotations.size() > 0) ? rotations[0] : VtValue();
        } else {
            TF_WARN("Cannot copy transformRotation as inputs differ.");
        }
        if (_valuesAreEqual(scales)) {
            out.transformScale = (scales.size() > 0) ? scales[0] : VtValue();
        } else {
            TF_WARN("Cannot copy transformScale as inputs differ.");
        }
        if (_valuesAreEqual(translations)) {
            out.transformTranslation = (translations.size() > 0) ? translations[0] : VtValue();
        } else {
            TF_WARN("Cannot copy transformTranslation as inputs differ.");
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
        imageAsset.uri = assetName;
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