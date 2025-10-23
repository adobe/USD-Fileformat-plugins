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
#include "gltfImport.h"
#include "debugCodes.h"
#include "gltfAnisotropy.h"
#include "gltfSpecGloss.h"
#include "importGltfContext.h"
#include <fileformatutils/common.h>
#include <fileformatutils/images.h>
#include <fileformatutils/neuralAssetsHelper.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>

#include <algorithm>
#include <numeric>

using namespace PXR_NS;

namespace adobe::usd {

// search for key in cache. The keys are the texture names and values are the image indexes
int
lookupTexture(const std::unordered_map<std::string, int>& cache, const std::string& key)
{
    const auto& it = cache.find(key);
    return (it != cache.end()) ? it->second : -1;
}

// Set the input data for an image
void
setInputImage(Input& input,
              int imageIndex,
              int uvIndex,
              const TfToken& channel,
              const TfToken& colorspace)
{
    input.image = imageIndex;
    input.value = VtValue();
    input.uvIndex = uvIndex;
    input.wrapS = AdobeTokens->repeat;
    input.wrapT = AdobeTokens->repeat;
    input.channel = channel;
    input.colorspace = colorspace;
}

// Metadata on glTF is found in various fields of the asset entity.
// Metadata on USD will be stored uniformily in the CustomLayerData dictionary.
bool
importMetadata(ImportGltfContext& ctx)
{
    // Version check
    float version = 0.0f;
    try {
        version = std::stof(ctx.gltf->asset.version);
    } catch (const std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Error: Invalid version. Exception: %s\n", e.what());
        return false;
    }
    if (version < 2.0f) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                     "Error: glTF version is less than 2.0. Found version: %s\n",
                     ctx.gltf->asset.version.c_str());
        return false;
    }

    // metadata.SetValueAtPath("version", PXR_NS::VtValue(ctx.gltf->asset.version)); // glTF version
    // dropped metadata.SetValueAtPath("minVersion", PXR_NS::VtValue(ctx.gltf->asset.miVersion)); //
    // what is minVersion ?
    for (auto& extra : ctx.gltf->asset.extras.Get<tinygltf::Value::Object>()) {
        ctx.usd->metadata.SetValueAtPath(extra.first,
                                         PXR_NS::VtValue(extra.second.Get<std::string>()));
    }
    // 'generator' could be on both asset.generator and asset.extras["generator"]. Regardless,
    // replace with our own.
    ctx.usd->metadata.SetValueAtPath("generator", PXR_NS::VtValue("Adobe usdGltf 1.0"));
    // 'copyright' could be on both asset.copyright and asset.extras["copyright"]. Give priority to
    // the former.
    if (!ctx.gltf->asset.copyright.empty()) {
        ctx.usd->metadata.SetValueAtPath("copyright", PXR_NS::VtValue(ctx.gltf->asset.copyright));
    }

    return true;
}

void
importCameras(ImportGltfContext& ctx)
{
    ctx.usd->cameras.resize(ctx.gltf->cameras.size());
    for (size_t i = 0; i < ctx.gltf->cameras.size(); i++) {
        const tinygltf::Camera& gCamera = ctx.gltf->cameras[i];
        Camera& usdCamera = ctx.usd->cameras[i];
        GfCamera& uCamera = usdCamera.camera;
        usdCamera.displayName = gCamera.name;
        if (gCamera.type == "perspective") {
            uCamera.SetProjection(GfCamera::Perspective);
            uCamera.SetClippingRange(
              GfRange1f(gCamera.perspective.znear, gCamera.perspective.zfar));
            uCamera.SetPerspectiveFromAspectRatioAndFieldOfView(gCamera.perspective.aspectRatio,
                                                                gCamera.perspective.yfov * rad2deg,
                                                                GfCamera::FOVVertical,
                                                                36 // TODO define better default
            );
            usdCamera.f = uCamera.GetFocalLength();
            usdCamera.nearZ = gCamera.perspective.znear;
            usdCamera.farZ = gCamera.perspective.zfar;
            usdCamera.fov = gCamera.perspective.yfov;
            usdCamera.aspectRatio = gCamera.perspective.aspectRatio;
        } else {
            uCamera.SetProjection(GfCamera::Orthographic);
            uCamera.SetClippingRange(
              GfRange1f(gCamera.orthographic.znear, gCamera.orthographic.zfar));
            float aspectRatio = gCamera.orthographic.xmag / gCamera.orthographic.ymag;

            uCamera.SetOrthographicFromAspectRatioAndSize(
              aspectRatio, gCamera.orthographic.xmag, GfCamera::FOVHorizontal);
            uCamera.SetFocusDistance(gCamera.orthographic.xmag);
            usdCamera.projection = GfCamera::Orthographic;
            usdCamera.fov = 36.0f;
            usdCamera.aspectRatio = aspectRatio;
            usdCamera.f = uCamera.GetFocalLength();
            usdCamera.nearZ = gCamera.orthographic.znear;
            usdCamera.farZ = gCamera.orthographic.zfar;
        }
        usdCamera.horizontalAperture = uCamera.GetHorizontalAperture();
        usdCamera.verticalAperture = uCamera.GetVerticalAperture();
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "gltf::import camera\n");
    }
}

bool
readDoubleValue(const tinygltf::Value& val, double& value)
{
    if (val.IsNumber()) {
        value = val.GetNumberAsDouble();
        return true;
    }

    return false;
}

bool
readDoubleArray(const tinygltf::Value& arrayVal, double* array, int arraySize)
{
    if (!arrayVal.IsArray() || static_cast<int>(arrayVal.ArrayLen()) != arraySize) {
        return false;
    }

    for (int idx = 0; idx < arraySize; ++idx) {
        const tinygltf::Value& elemVal = arrayVal.Get(idx);
        if (elemVal.IsNumber()) {
            array[idx] = elemVal.GetNumberAsDouble();
        }
    }

    return true;
}

bool
readExtensionMap(const tinygltf::Value& obj, tinygltf::ExtensionMap& extensions)
{
    if (!obj.IsObject()) {
        return false;
    }

    for (const std::string& key : obj.Keys()) {
        extensions[key] = obj.Get(key);
    }

    return true;
}

bool
readTextureInfo(const tinygltf::Value& val, tinygltf::TextureInfo& textureInfo)
{
    if (!val.IsObject()) {
        return false;
    }

    if (const tinygltf::Value& idxVal = val.Get("index"); idxVal.IsInt()) {
        textureInfo.index = idxVal.GetNumberAsInt();
    } else {
        return false;
    }

    if (const tinygltf::Value& tcVal = val.Get("texCoord"); tcVal.IsInt()) {
        textureInfo.texCoord = tcVal.GetNumberAsInt();
    }

    textureInfo.extras = val.Get("extras");
    readExtensionMap(val.Get("extensions"), textureInfo.extensions);

    return true;
}

bool
readNormalTextureInfo(const tinygltf::Value& val, tinygltf::NormalTextureInfo& normalTextureInfo)
{
    if (!val.IsObject()) {
        return false;
    }

    if (const tinygltf::Value& idxVal = val.Get("index"); idxVal.IsInt()) {
        normalTextureInfo.index = idxVal.GetNumberAsInt();
    } else {
        return false;
    }

    if (const tinygltf::Value& tcVal = val.Get("texCoord"); tcVal.IsInt()) {
        normalTextureInfo.texCoord = tcVal.GetNumberAsInt();
    }

    if (const tinygltf::Value& scaleVal = val.Get("scale"); scaleVal.IsNumber()) {
        normalTextureInfo.scale = scaleVal.GetNumberAsDouble();
    }

    normalTextureInfo.extras = val.Get("extras");
    readExtensionMap(val.Get("extensions"), normalTextureInfo.extensions);

    return true;
}

void
importScale1(Input& input, double factor)
{
    if (factor != 1) {
        input.scale = GfVec4f(factor, factor, factor, factor);
    }
}

void
importScale3(Input& input, const double* factor, double mult = 1.0)
{
    if (factor[0] != 1 || factor[1] != 1 || factor[2] != 1 || mult != 1.0) {
        input.scale = GfVec4f(mult * factor[0], mult * factor[1], mult * factor[2], mult);
    }
}

void
importValue1(Input& input, double value)
{
    input.value = static_cast<float>(value);
}

void
importValue3(Input& input, const double* value, double mult = 1.0)
{
    input.value = GfVec3f(mult * value[0], mult * value[1], mult * value[2]);
}

bool
isInputUsed(const Input& input)
{
    return input.image >= 0 || !input.value.IsEmpty();
}

bool
importWebPTextureSource(const tinygltf::ExtensionMap& extensions, int* imageIndex)
{
    if (auto extIt = extensions.find("EXT_texture_webp"); extIt != extensions.end()) {
        const tinygltf::Value& webpExt = extIt->second;
        if (const tinygltf::Value& sourceVal = webpExt.Get("source"); sourceVal.IsInt()) {
            *imageIndex = sourceVal.GetNumberAsInt();
            return true;
        }
    }

    return false;
}

int
importImage(ImportGltfContext& ctx,
            int textureIndex,
            const std::string& materialName,
            const std::string& imageName)
{
    // Check the cache on the context if we've processed this texture before
    auto [it, inserted] = ctx.imageMap.insert({ textureIndex, -1 });
    if (!inserted) {
        return it->second;
    }

    auto [usdImageIndex, usdImage] = ctx.usd->addImage();
    const tinygltf::Texture& texture = ctx.gltf->textures[textureIndex];
    int imageIndex = texture.source;
    if (imageIndex < 0) {
        importWebPTextureSource(texture.extensions, &imageIndex);
    }
    if (imageIndex < 0) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                     "For material %s: texture %d without a valid source image\n",
                     materialName.c_str(),
                     textureIndex);
        return -1;
    }
    const tinygltf::Image& image = ctx.gltf->images[imageIndex];

    const std::string uriStem = TfStringGetBeforeSuffix(TfGetBaseName(image.uri));
    const std::string uriExtension = TfGetExtension(image.uri);
    // Add uri to list of filenames exported as metadata
    if (!image.uri.empty()) {
        ctx.filenames.push_back(image.uri);
    }
    usdImage.name = !image.name.empty() ? image.name
                    : !uriStem.empty()  ? uriStem
                                        : materialName + "_" + imageName;
    ctx.uniqueImageNameEnforcer.enforceUniqueness(usdImage.name);
    usdImage.uri = usdImage.name;
    if (uriExtension == "png" || image.mimeType == "image/png") {
        usdImage.format = ImageFormatPng;
        usdImage.uri += ".png";
    } else if (uriExtension == "jpg" || uriExtension == "jpeg" || image.mimeType == "image/jpg" ||
               image.mimeType == "image/jpeg") {
        usdImage.format = ImageFormatJpg;
        usdImage.uri += ".jpg";
    } else if (uriExtension == "webp" || image.mimeType == "image/webp") {
        usdImage.format = ImageFormatWebp;
        usdImage.uri += ".webp";
    } else {
        TF_DEBUG_MSG(
          FILE_FORMAT_GLTF, "Could not read image with extension %s\n", uriExtension.c_str());
        return -1;
    }
    // make a copy of the image data
    usdImage.image = image.image;
    // Cache the new USD image index
    it->second = usdImageIndex;
    return usdImageIndex;
}

TfToken
getMipMapCode(int filter)
{
    switch (filter) {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
            return AdobeTokens->nearest;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
            return AdobeTokens->linear;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
            return AdobeTokens->nearestMipmapNearest;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
            return AdobeTokens->linearMipmapNearest;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            return AdobeTokens->nearestMipmapLinear;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            return AdobeTokens->linearMipmapLinear;
        default:
            return AdobeTokens->linear;
    }
}

// Note, if a single texture channel is read from a RGB texture, like in the case of of reading the
// roughness channel from a metalRoughness texture, the texture reader needs to be marked as
// reading from a "raw" color space instead of sRGB. The same is true for reading normal maps
bool
importTexture(const tinygltf::Model* gltf,
              int imageIndex,
              int textureIndex,
              int uvIndex,
              Input& input,
              const TfToken& channel,
              const TfToken& colorSpace)
{
    tinygltf::Texture texture = gltf->textures[textureIndex];
    int samplerIndex = texture.sampler;
    if (samplerIndex >= 0) {
        tinygltf::Sampler sampler = gltf->samplers[samplerIndex];
        switch (sampler.wrapS) {
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
                input.wrapS = AdobeTokens->repeat;
                break;
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                input.wrapS = AdobeTokens->clamp;
                break;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                input.wrapS = AdobeTokens->mirror;
                break;
            default:
                input.wrapS = AdobeTokens->repeat;
                break;
        }
        switch (sampler.wrapT) {
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
                input.wrapT = AdobeTokens->repeat;
                break;
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                input.wrapT = AdobeTokens->clamp;
                break;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                input.wrapT = AdobeTokens->mirror;
                break;
            default:
                input.wrapT = AdobeTokens->repeat;
                break;
        }

        input.minFilter = getMipMapCode(sampler.minFilter);
        input.magFilter = getMipMapCode(sampler.magFilter);

    } else {
        // The GLTF spec defaults to 'repeat' and we need to explicitly set that, since the default
        // in USD is 'black' (technically 'useMetadata')
        input.wrapS = AdobeTokens->repeat;
        input.wrapT = AdobeTokens->repeat;
        input.minFilter = AdobeTokens->linear;
        input.magFilter = AdobeTokens->linear;
    }
    input.image = imageIndex;
    input.uvIndex = uvIndex;
    input.channel = channel;
    if (channel == AdobeTokens->a) {
        // Note, the alpha channel should never get the sRGB transformation, so specifying raw
        // is redundant. Currently it also causes issues when we read color and opacity from the
        // same texture and the texture is tagged differently. Once that is resolved, there should
        // be no issue authoring the color space for alpha again.
    } else {
        input.colorspace = colorSpace;
    }
    return true;
}

bool
importTextureTransform(const tinygltf::ExtensionMap& extensions, Input& input)
{
    auto posIt = extensions.find("KHR_texture_transform");

    // If the "KHR_texture_transform" is not supported, we ignore the
    // transform values on the input. However, we still need to perform the
    // (1.0 - T) flip which is applied here. Previously, we were flipping the
    // V values of the UV coordinates when reading the mesh but since the
    // glTF texture coordinates may have been defined using non-normalized values,
    // the V inversion is applied here.
    if (posIt == extensions.end()) {
        input.transformScale = GfVec2f(1.0f, -1.0f);
        input.transformTranslation = GfVec2f(0.0f, 1.0f);
        return true;
    }

    const tinygltf::Value& value = posIt->second;
    const tinygltf::Value& rotation = value.Get("rotation");
    const tinygltf::Value& scale = value.Get("scale");
    const tinygltf::Value& offset = value.Get("offset");

    // The rotation value in glTF is in radians, but USD expects degrees.
    if (rotation.IsNumber()) {
        float rotationValue = rotation.GetNumberAsDouble() * rad2deg;
        if (rotationValue != 0.0f) {
            input.transformRotation = rotationValue;
        }
    }

    // As mentioned above, the T flip needs to be applied here. This is done
    // by multiplying the y-scale value by -1 and using (1.0 - ty) as the new
    // ty translation.
    float sx = 1.0f;
    float sy = -1.0f;
    if (scale.IsArray() && scale.ArrayLen() == 2) {
        const tinygltf::Value& v0 = scale.Get(0);
        const tinygltf::Value& v1 = scale.Get(1);
        sx = v0.GetNumberAsDouble();
        sy = -v1.GetNumberAsDouble();
    }
    if (sx != 1.0f || sy != 1.0f) {
        input.transformScale = GfVec2f(sx, sy);
    }

    float tx = 0.0f;
    float ty = 1.0f;
    if (offset.IsArray() && offset.ArrayLen() == 2) {
        const tinygltf::Value& v0 = offset.Get(0);
        const tinygltf::Value& v1 = offset.Get(1);
        tx = v0.GetNumberAsDouble();
        ty = 1.0f - v1.GetNumberAsDouble();
    }

    if (tx != 0.0f || ty != 0.0f) {
        input.transformTranslation = GfVec2f(tx, ty);
    }
    return true;
}

void
importInput(ImportGltfContext& ctx,
            const std::string& materialName,
            const std::string& inputName,
            Input& input,
            const tinygltf::TextureInfo& texture,
            const TfToken& channels,
            const double* factor = nullptr,
            const double defaultFactor = 0.0)
{
    if (channels == AdobeTokens->rgb) {
        TF_CODING_ERROR("importInput can only be used for single channel textures: %s %s %s",
                        materialName.c_str(),
                        inputName.c_str(),
                        channels.GetText());
        return;
    }

    if (texture.index >= 0) {
        int imageIndex = importImage(ctx, texture.index, materialName, inputName);
        // Single channel texture reads are always in the "raw" color space and not sRGB
        importTexture(
          ctx.gltf, imageIndex, texture.index, texture.texCoord, input, channels, AdobeTokens->raw);
        importTextureTransform(texture.extensions, input);
        if (factor != nullptr) {
            importScale1(input, *factor);
        }
    } else if (factor != nullptr) {
        if (*factor != defaultFactor) {
            importValue1(input, *factor);
        }
    }
}

void
importColorInput(ImportGltfContext& ctx,
                 const std::string& materialName,
                 const std::string& inputName,
                 Input& input,
                 const tinygltf::TextureInfo& texture,
                 const double factor[3],
                 const double defaultFactor = 0.0)
{
    if (texture.index >= 0) {
        int imageIndex = importImage(ctx, texture.index, materialName, inputName);
        // Color inputs are always read as sRGB
        importTexture(ctx.gltf,
                      imageIndex,
                      texture.index,
                      texture.texCoord,
                      input,
                      AdobeTokens->rgb,
                      AdobeTokens->sRGB);
        importTextureTransform(texture.extensions, input);
        importScale3(input, factor);
    } else if (factor[0] != defaultFactor || factor[1] != defaultFactor ||
               factor[2] != defaultFactor) {
        importValue3(input, factor);
    }
}

void
importNormalInput(ImportGltfContext& ctx,
                  const std::string& materialName,
                  const std::string& inputName,
                  Input& input,
                  const tinygltf::NormalTextureInfo& texture)
{
    if (texture.index >= 0) {
        int imageIndex = importImage(ctx, texture.index, materialName, inputName);
        // Normal maps should not get the sRGB treatment and hence should be read as "raw"
        importTexture(ctx.gltf,
                      imageIndex,
                      texture.index,
                      texture.texCoord,
                      input,
                      AdobeTokens->rgb,
                      AdobeTokens->raw);
        importTextureTransform(texture.extensions, input);
        // Note, while the normal scale usually works, the official usdchecker will flag
        // scale and bias that are not 2 and -1 for normal map texture readers
        // https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usdUtils/complianceChecker.py#L568
        const double scale = texture.scale;
        input.scale = GfVec4f(2 * scale, 2 * scale, 2 * scale, 1);
        input.bias = GfVec4f(-1 * scale, -1 * scale, -1 * scale, 0);
    }
}

void
applyInputMultiplier(Input& input, const GfVec3f& mult)
{
    if (input.image >= 0) {
        GfVec4f scale = input.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
        input.scale = GfVec4f(mult[0] * scale[0], mult[1] * scale[1], mult[2] * scale[2], scale[3]);
    } else if (input.value.IsHolding<GfVec3f>()) {
        const GfVec3f& value = input.value.UncheckedGet<GfVec3f>();
        input.value = GfVec3f(mult[0] * value[0], mult[1] * value[1], mult[2] * value[2]);
    } else {
        input.value = mult;
    }
}

struct Clearcoat
{
    double factor = 0.0;
    tinygltf::TextureInfo texture; // r channel
    double roughnessFactor = 0.0;
    tinygltf::TextureInfo roughnessTexture;    // g channel
    tinygltf::NormalTextureInfo normalTexture; // rgb channels
};

bool
importClearcoat(const tinygltf::ExtensionMap& extensions, Clearcoat* clearcoat)
{
    auto extIt = extensions.find("KHR_materials_clearcoat");
    if (extIt != extensions.end()) {
        const tinygltf::Value& coatExt = extIt->second;
        readDoubleValue(coatExt.Get("clearcoatFactor"), clearcoat->factor);
        readTextureInfo(coatExt.Get("clearcoatTexture"), clearcoat->texture);
        readDoubleValue(coatExt.Get("clearcoatRoughnessFactor"), clearcoat->roughnessFactor);
        readTextureInfo(coatExt.Get("clearcoatRoughnessTexture"), clearcoat->roughnessTexture);
        readNormalTextureInfo(coatExt.Get("clearcoatNormalTexture"), clearcoat->normalTexture);
        return true;
    }

    return false;
}

bool
importEmissionStrength(const tinygltf::ExtensionMap& extensions, double* emissiveStrength)
{
    if (auto extIt = extensions.find("KHR_materials_emissive_strength");
        extIt != extensions.end()) {
        const tinygltf::Value& emissiveStrengthExt = extIt->second;
        readDoubleValue(emissiveStrengthExt.Get("emissiveStrength"), *emissiveStrength);
        return true;
    }
    return false;
}

bool
importIor(const tinygltf::ExtensionMap& extensions, double* ior)
{
    if (auto extIt = extensions.find("KHR_materials_ior"); extIt != extensions.end()) {
        const tinygltf::Value& iorExt = extIt->second;
        readDoubleValue(iorExt.Get("ior"), *ior);
        return true;
    }
    return false;
}

struct Sheen
{
    double colorFactor[3] = { 0.0, 0.0, 0.0 };
    tinygltf::TextureInfo colorTexture; // rgb channels
    double roughnessFactor = 0.0;
    tinygltf::TextureInfo roughnessTexture; // a channel
};

bool
importSheen(const tinygltf::ExtensionMap& extensions, Sheen* sheen)
{
    auto extIt = extensions.find("KHR_materials_sheen");
    if (extIt != extensions.end()) {
        const tinygltf::Value& sheenExt = extIt->second;
        readDoubleArray(sheenExt.Get("sheenColorFactor"), sheen->colorFactor, 3);
        readTextureInfo(sheenExt.Get("sheenColorTexture"), sheen->colorTexture);
        readDoubleValue(sheenExt.Get("sheenRoughnessFactor"), sheen->roughnessFactor);
        readTextureInfo(sheenExt.Get("sheenRoughnessTexture"), sheen->roughnessTexture);
        return true;
    }

    return false;
}

struct Specular
{
    double factor = 1.0;
    tinygltf::TextureInfo texture; // a channel
    double colorFactor[3] = { 1.0, 1.0, 1.0 };
    tinygltf::TextureInfo colorTexture; // rgb channels
};

bool
importSpecular(const tinygltf::ExtensionMap& extensions, Specular* specular)
{
    auto extIt = extensions.find("KHR_materials_specular");
    if (extIt != extensions.end()) {
        const tinygltf::Value& specExt = extIt->second;
        readDoubleValue(specExt.Get("specularFactor"), specular->factor);
        readTextureInfo(specExt.Get("specularTexture"), specular->texture);
        readDoubleArray(specExt.Get("specularColorFactor"), specular->colorFactor, 3);
        readTextureInfo(specExt.Get("specularColorTexture"), specular->colorTexture);
        return true;
    }

    return false;
}

struct Transmission
{
    double factor = 0.0;
    tinygltf::TextureInfo texture; // r channel
};

bool
importTransmission(const tinygltf::ExtensionMap& extensions, Transmission* transmission)
{
    if (auto extIt = extensions.find("KHR_materials_transmission"); extIt != extensions.end()) {
        const tinygltf::Value& transExt = extIt->second;
        readDoubleValue(transExt.Get("transmissionFactor"), transmission->factor);
        readTextureInfo(transExt.Get("transmissionTexture"), transmission->texture);
        return true;
    }

    return false;
}

struct Volume
{
    double thicknessFactor = 0.0;
    tinygltf::TextureInfo thicknessTexture; // g channel
    // Note, the GLTF standard specifies a default of infinity, but ASM works better with 0
    double attenuationDistance = 0.0;
    double attenuationColor[3] = { 1.0, 1.0, 1.0 };
};

bool
importVolume(const tinygltf::ExtensionMap& extensions, Volume* volume)
{
    if (auto extIt = extensions.find("KHR_materials_volume"); extIt != extensions.end()) {
        const tinygltf::Value& volumeExt = extIt->second;
        readDoubleValue(volumeExt.Get("thicknessFactor"), volume->thicknessFactor);
        readTextureInfo(volumeExt.Get("thicknessTexture"), volume->thicknessTexture);
        readDoubleValue(volumeExt.Get("attenuationDistance"), volume->attenuationDistance);
        readDoubleArray(volumeExt.Get("attenuationColor"), volume->attenuationColor, 3);
        return true;
    }

    return false;
}

// Adobe extension for supporting specular level for clearcoat (similar to specular extension)
struct AdobeClearcoatSpecular
{
    double ior = 1.5;
    double factor = 1.0;
    tinygltf::TextureInfo texture; // b channel
};

bool
importAdobeClearcoatSpecular(const tinygltf::ExtensionMap& extensions,
                             AdobeClearcoatSpecular* clearcoatSpecular)
{
    auto extIt = extensions.find("ADOBE_materials_clearcoat_specular");
    if (extIt != extensions.end()) {
        const tinygltf::Value& coatExt = extIt->second;
        readDoubleValue(coatExt.Get("clearcoatIor"), clearcoatSpecular->ior);
        readDoubleValue(coatExt.Get("clearcoatSpecularFactor"), clearcoatSpecular->factor);
        readTextureInfo(coatExt.Get("clearcoatSpecularTexture"), clearcoatSpecular->texture);
        return true;
    }

    return false;
}

// Adobe extension for supporting colored tinting of clearcoat
struct AdobeClearcoatTint
{
    double factor[3] = { 1.0, 1.0, 1.0 };
    tinygltf::TextureInfo texture; // rgb channels
};

bool
importAdobeClearcoatTint(const tinygltf::ExtensionMap& extensions,
                         AdobeClearcoatTint* clearcoatTint)
{
    auto extIt = extensions.find("ADOBE_materials_clearcoat_tint");
    if (extIt != extensions.end()) {
        const tinygltf::Value& coatExt = extIt->second;
        readDoubleArray(coatExt.Get("clearcoatTintFactor"), clearcoatTint->factor, 3);
        readTextureInfo(coatExt.Get("clearcoatTintTexture"), clearcoatTint->texture);
        return true;
    }

    return false;
}

// This is not a ratified extension yet!
// KHR_materials_diffuse_transmission
struct DiffuseTransmission
{
    double factor = 0.0;
    tinygltf::TextureInfo texture;      // a channel
    tinygltf::TextureInfo colorTexture; // rgb channels
    double colorFactor[3] = { 1.0, 1.0, 1.0 };
};

bool
importDiffuseTransmission(const tinygltf::ExtensionMap& extensions,
                          DiffuseTransmission* diffuseTransmission)
{
    if (auto extIt = extensions.find("KHR_materials_diffuse_transmission");
        extIt != extensions.end()) {
        const tinygltf::Value& dtExt = extIt->second;
        readDoubleValue(dtExt.Get("diffuseTransmissionFactor"), diffuseTransmission->factor);
        readTextureInfo(dtExt.Get("diffuseTransmissionTexture"), diffuseTransmission->texture);
        readTextureInfo(dtExt.Get("diffuseTransmissionColorTexture"),
                        diffuseTransmission->colorTexture);
        readDoubleArray(
          dtExt.Get("diffuseTransmissionColorFactor"), diffuseTransmission->colorFactor, 3);
        return true;
    }

    return false;
}

// This is not a ratified extension yet!
// KHR_materials_subsurface (AKA KHR_materials_sss)
struct Subsurface
{
    double scatterDistance = std::numeric_limits<double>::infinity();
    double scatterColor[3] = { 1.0, 1.0, 1.0 };
};

bool
importSubsurface(const tinygltf::ExtensionMap& extensions, Subsurface* subsurface)
{
    auto extIt = extensions.find("KHR_materials_subsurface");
    // KHR_materials_subsurface was known as KHR_materials_sss during development and there are a
    // few assets out there that use the old name. We should remove this fallback eventually
    if (extIt == extensions.end()) {
        extIt = extensions.find("KHR_materials_sss");
    }

    if (extIt != extensions.end()) {
        const tinygltf::Value& sssExt = extIt->second;
        readDoubleValue(sssExt.Get("scatterDistance"), subsurface->scatterDistance);
        readDoubleArray(sssExt.Get("scatterColor"), subsurface->scatterColor, 3);
        return true;
    }

    return false;
}

bool
importUnlit(const tinygltf::ExtensionMap& extensions)
{
    auto extIt = extensions.find("KHR_materials_unlit");
    return extIt != extensions.end();
}

void
importMaterials(ImportGltfContext& ctx)
{
    // map used to track created textures converted from specular glossiness to avoid duplication
    std::unordered_map<std::string, int> specGlossTextureCache;

    // map used to track created textures converted from anisotropy to avoid duplication
    std::unordered_map<std::string, int> anisotropyTextureCache;

    ctx.usd->materials.resize(ctx.gltf->materials.size());
    for (size_t i = 0; i < ctx.gltf->materials.size(); i++) {
        // gm = glTF material, m = USD material
        const tinygltf::Material& gm = ctx.gltf->materials[i];
        Material& m = ctx.usd->materials[i];
        m.displayName = gm.name.empty() ? "Material" + std::to_string(i) : gm.name;

        // KHR_materials_pbrSpecularGlossiness data, in extensions, requires some cherrypicking.
        auto it = gm.extensions.find("KHR_materials_pbrSpecularGlossiness");
        if (it != gm.extensions.end()) {
            const tinygltf::Value& specGlossVal = it->second;
            const tinygltf::Value& diffuseFactorVal = specGlossVal.Get("diffuseFactor");
            const tinygltf::Value& specularFactorVal = specGlossVal.Get("specularFactor");
            const tinygltf::Value& glossinessFactorVal = specGlossVal.Get("glossinessFactor");
            const tinygltf::Value& diffuseTextureVal = specGlossVal.Get("diffuseTexture");
            const tinygltf::Value& specGlossTextureVal =
              specGlossVal.Get("specularGlossinessTexture");
            double diffuseFactor[4] = { 1, 1, 1, 1 }; // default diffuseFactor values
            if (diffuseFactorVal.IsArray()) {
                readDoubleArray(diffuseFactorVal, diffuseFactor, 4);
            }

            double specularFactor[3] = { 1, 1, 1 }; // default specularFactor values
            if (specularFactorVal.IsArray()) {
                readDoubleArray(specularFactorVal, specularFactor, 3);
            }

            float glosinessFactor = 1.0; // default glossinessFactor
            if (glossinessFactorVal.IsNumber()) {
                glosinessFactor = glossinessFactorVal.GetNumberAsDouble();
            }

            Input diffuseColor;
            Input specularColor;
            Input opacity;
            diffuseColor.value =
              GfVec4f(diffuseFactor[0], diffuseFactor[1], diffuseFactor[2], diffuseFactor[3]);
            specularColor.value =
              GfVec4f(specularFactor[0], specularFactor[1], specularFactor[2], glosinessFactor);

            tinygltf::TextureInfo diffuseTextureInfo;
            if (!readTextureInfo(diffuseTextureVal, diffuseTextureInfo))
                diffuseTextureInfo.index = -1;
            if (diffuseTextureInfo.index >= 0) {
                int imageIndex =
                  importImage(ctx, diffuseTextureInfo.index, m.displayName, "diffuse");
                importTexture(ctx.gltf,
                              imageIndex,
                              diffuseTextureInfo.index,
                              diffuseTextureInfo.texCoord,
                              diffuseColor,
                              AdobeTokens->rgb,
                              AdobeTokens->sRGB);
                importTextureTransform(gm.extensions, diffuseColor);

                if (gm.alphaMode == "BLEND" || gm.alphaMode == "MASK") {
                    opacity = diffuseColor;
                    importTexture(ctx.gltf,
                                  imageIndex,
                                  diffuseTextureInfo.index,
                                  diffuseTextureInfo.texCoord,
                                  opacity,
                                  AdobeTokens->a,
                                  AdobeTokens->raw);
                    importScale1(opacity, diffuseFactor[3]);
                }
            }

            tinygltf::TextureInfo specularTextureInfo;
            if (!readTextureInfo(specGlossTextureVal, specularTextureInfo))
                specularTextureInfo.index = -1;
            if (specularTextureInfo.index >= 0) {
                int imageIndex =
                  importImage(ctx, specularTextureInfo.index, m.displayName, "specGloss");
                importTexture(ctx.gltf,
                              imageIndex,
                              specularTextureInfo.index,
                              specularTextureInfo.texCoord,
                              specularColor,
                              AdobeTokens->rgb,
                              AdobeTokens->sRGB);
                importTextureTransform(gm.extensions, specularColor);
            }

            translateSpecularGlossinessToMetallicRoughness(ctx,
                                                           specGlossTextureCache,
                                                           diffuseColor,
                                                           specularColor,
                                                           opacity,
                                                           gm.alphaMode,
                                                           m.diffuseColor,
                                                           m.opacity,
                                                           m.metallic,
                                                           m.roughness);

        } else {
            int diffuseTexture = gm.pbrMetallicRoughness.baseColorTexture.index;
            int mrTexture = gm.pbrMetallicRoughness.metallicRoughnessTexture.index;
            const std::vector<double>& diffuse = gm.pbrMetallicRoughness.baseColorFactor;
            // Import pbrMetallicRoughness.baseColorTexture from glTF
            if (diffuseTexture >= 0) {
                int imageIndex = importImage(ctx, diffuseTexture, m.displayName, "diffuse");
                importTexture(ctx.gltf,
                              imageIndex,
                              diffuseTexture,
                              gm.pbrMetallicRoughness.baseColorTexture.texCoord,
                              m.diffuseColor,
                              AdobeTokens->rgb,
                              AdobeTokens->sRGB);
                importScale3(m.diffuseColor, diffuse.data());
                importTextureTransform(gm.pbrMetallicRoughness.baseColorTexture.extensions,
                                       m.diffuseColor);
                if (gm.alphaMode == "BLEND" || gm.alphaMode == "MASK") {
                    importTexture(ctx.gltf,
                                  imageIndex,
                                  diffuseTexture,
                                  gm.pbrMetallicRoughness.baseColorTexture.texCoord,
                                  m.opacity,
                                  AdobeTokens->a,
                                  AdobeTokens->raw);
                    importScale1(m.opacity, diffuse[3]);
                    m.opacity.transformRotation = m.diffuseColor.transformRotation;
                    m.opacity.transformScale = m.diffuseColor.transformScale;
                    m.opacity.transformTranslation = m.diffuseColor.transformTranslation;
                }
            } else if (diffuse.size()) {
                importValue3(m.diffuseColor, diffuse.data());
                importValue1(m.opacity, diffuse[3]);
            }
            // Import pbrMetallicRoughness.metallicRoughnessTexture from glTF
            if (mrTexture >= 0) {
                int imageIndex = importImage(ctx, mrTexture, m.displayName, "metallicRoughness");
                importTexture(ctx.gltf,
                              imageIndex,
                              mrTexture,
                              gm.pbrMetallicRoughness.metallicRoughnessTexture.texCoord,
                              m.roughness,
                              AdobeTokens->g,
                              AdobeTokens->raw);
                importTexture(ctx.gltf,
                              imageIndex,
                              mrTexture,
                              gm.pbrMetallicRoughness.metallicRoughnessTexture.texCoord,
                              m.metallic,
                              AdobeTokens->b,
                              AdobeTokens->raw);

                importScale1(m.metallic, gm.pbrMetallicRoughness.metallicFactor);
                importScale1(m.roughness, gm.pbrMetallicRoughness.roughnessFactor);
                importTextureTransform(gm.pbrMetallicRoughness.metallicRoughnessTexture.extensions,
                                       m.roughness);
                m.metallic.transformRotation = m.roughness.transformRotation;
                m.metallic.transformScale = m.roughness.transformScale;
                m.metallic.transformTranslation = m.roughness.transformTranslation;
            } else {
                importValue1(m.metallic, gm.pbrMetallicRoughness.metallicFactor);
                importValue1(m.roughness, gm.pbrMetallicRoughness.roughnessFactor);
            }

            double ior = 1.5;
            if (importIor(gm.extensions, &ior)) {
                importValue1(m.ior, ior);
            }

            Specular specular;
            if (importSpecular(gm.extensions, &specular)) {
                importInput(ctx,
                            m.displayName,
                            "specularLevel",
                            m.specularLevel,
                            specular.texture,
                            AdobeTokens->a,
                            &specular.factor,
                            1.0);
                importColorInput(ctx,
                                 m.displayName,
                                 "specularColor",
                                 m.specularColor,
                                 specular.colorTexture,
                                 specular.colorFactor,
                                 1.0);
            }

            auto extIt = gm.extensions.find("KHR_materials_anisotropy");
            if (extIt != gm.extensions.end()) {
                AnisotropyData anisotropyData;
                Image anisotropySrcImage;
                float roughness = 0.0f;
                if (m.roughness.value.IsHolding<float>()) {
                    roughness = m.roughness.value.UncheckedGet<float>();
                }
                if (importAnisotropyData(ctx,
                                         gm.extensions,
                                         extIt->second,
                                         m,
                                         roughness,
                                         anisotropyData,
                                         anisotropySrcImage)) {
                    importAnisotropyTexture(ctx,
                                            gm,
                                            m,
                                            roughness,
                                            anisotropyData,
                                            anisotropySrcImage,
                                            anisotropyTextureCache);
                }
            }

            Clearcoat clearcoat;
            if (importClearcoat(gm.extensions, &clearcoat)) {
                importInput(ctx,
                            m.displayName,
                            "clearcoat",
                            m.clearcoat,
                            clearcoat.texture,
                            AdobeTokens->r,
                            &clearcoat.factor);
                importInput(ctx,
                            m.displayName,
                            "clearcoatRoughness",
                            m.clearcoatRoughness,
                            clearcoat.roughnessTexture,
                            AdobeTokens->g,
                            &clearcoat.roughnessFactor);
                importNormalInput(ctx,
                                  m.displayName,
                                  "clearcoatNormal",
                                  m.clearcoatNormal,
                                  clearcoat.normalTexture);
            }

            AdobeClearcoatSpecular clearcoatSpecular;
            if (importAdobeClearcoatSpecular(gm.extensions, &clearcoatSpecular)) {
                importValue1(m.clearcoatIor, clearcoatSpecular.ior);
                importInput(ctx,
                            m.displayName,
                            "clearcoatSpecular",
                            m.clearcoatSpecular,
                            clearcoatSpecular.texture,
                            AdobeTokens->b,
                            &clearcoatSpecular.factor,
                            1.0);
            }

            AdobeClearcoatTint clearcoatTint;
            if (importAdobeClearcoatTint(gm.extensions, &clearcoatTint)) {
                importColorInput(ctx,
                                 m.displayName,
                                 "clearcoatColor",
                                 m.clearcoatColor,
                                 clearcoatTint.texture,
                                 clearcoatTint.factor,
                                 1.0);
            }

            Sheen sheen;
            if (importSheen(gm.extensions, &sheen)) {
                importColorInput(ctx,
                                 m.displayName,
                                 "sheenColor",
                                 m.sheenColor,
                                 sheen.colorTexture,
                                 sheen.colorFactor);
                importInput(ctx,
                            m.displayName,
                            "sheenRoughness",
                            m.sheenRoughness,
                            sheen.roughnessTexture,
                            AdobeTokens->a,
                            &sheen.roughnessFactor);
            }

            Transmission transmission;
            bool hasTransmission = false;
            if (importTransmission(gm.extensions, &transmission)) {
                importInput(ctx,
                            m.displayName,
                            "transmission",
                            m.transmission,
                            transmission.texture,
                            AdobeTokens->r,
                            &transmission.factor);
                hasTransmission = true;
                // Note, the GLTF material model uses the baseColor to tint transmission through
                // a surface. To emulate that behavior with ASM 4.0 we try to map the baseColor
                // to the clearcoatColor and activate the clearcoat. This becomes complicated if
                // the clearcoat is already in use. We try our best below, but we're not trying
                // to blend signals to make this work at all cost
                if (isInputUsed(m.diffuseColor)) {
                    if (!isInputUsed(m.clearcoat)) {
                        // Use the transmission strength as the strength for the lobe
                        m.clearcoat = m.transmission;
                        // Transfer the values from the regular specular lobe
                        m.clearcoatRoughness = m.roughness;
                        m.clearcoatNormal = m.normal;
                        m.clearcoatSpecular = m.specularLevel;
                        m.clearcoatIor = m.ior;

                        if (!isInputUsed(m.clearcoatColor)) {
                            m.clearcoatColor = m.diffuseColor;
                            // Mark that material as having a specific purpose for the clearcoat
                            // that was not authored in the source asset
                            m.clearcoatModelsTransmissionTint = true;
                        } else {
                            TF_WARN("Can't map baseColor to clearcoatColor for transmission, since "
                                    "clearcoatColor is in use, for material %s",
                                    m.displayName.c_str());
                        }
                    } else {
                        TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                                     "Can't touch clearcoat lobe to enable "
                                     "transmission tinting on material %s\n",
                                     m.displayName.c_str());
                    }
                }
            }

            DiffuseTransmission diffuseTransmission;
            if (importDiffuseTransmission(gm.extensions, &diffuseTransmission)) {
                // Note, the ASM 4.0 model does not have a diffuse transmission lobe, so we're
                // approximating this effect by mapping it to general micro-facet transmission
                // and volume absorption. Ideally we would make the micro-facet roughness very
                // high to approach a diffuse transmission, but this would mess with general
                // specular, so we're not changing roughness.
                if (!hasTransmission) {
                    importInput(ctx,
                                m.displayName,
                                "transmission",
                                m.transmission,
                                diffuseTransmission.texture,
                                AdobeTokens->a,
                                &diffuseTransmission.factor);
                    importColorInput(ctx,
                                     m.displayName,
                                     "absorptionColor",
                                     m.absorptionColor,
                                     diffuseTransmission.colorTexture,
                                     diffuseTransmission.colorFactor);
                } else {
                    TF_WARN("Material %s has both KHR_materials_transmission and "
                            "KHR_materials_diffuse_transmission. Ignoring the latter.",
                            m.displayName.c_str());
                }
            }

            Volume volume;
            if (importVolume(gm.extensions, &volume) && volume.thicknessFactor > 0.0) {
                importInput(ctx,
                            m.displayName,
                            "thickness",
                            m.volumeThickness,
                            volume.thicknessTexture,
                            AdobeTokens->g,
                            &volume.thicknessFactor);
                importValue1(m.absorptionDistance, volume.attenuationDistance);
                // absorptionColor from the extension is a constant and we use it as a
                // multiplier on the existing absorptionColor, which is often the same as
                // diffuse
                GfVec3f mult(volume.attenuationColor[0],
                             volume.attenuationColor[1],
                             volume.attenuationColor[2]);
                applyInputMultiplier(m.absorptionColor, mult);
            }

            Subsurface subsurface;
            if (importSubsurface(gm.extensions, &subsurface)) {
                importValue1(m.scatteringDistance, subsurface.scatterDistance);
                importValue3(m.scatteringColor, subsurface.scatterColor);
            }
        }
        bool unlit = importUnlit(gm.extensions);
        double emissiveStrength = 1.0;
        importEmissionStrength(gm.extensions, &emissiveStrength);
        if (gm.emissiveTexture.index >= 0) {
            int imageIndex = importImage(ctx, gm.emissiveTexture.index, m.displayName, "emissive");
            importTexture(ctx.gltf,
                          imageIndex,
                          gm.emissiveTexture.index,
                          gm.emissiveTexture.texCoord,
                          m.emissiveColor,
                          AdobeTokens->rgb,
                          AdobeTokens->sRGB);
            importScale3(m.emissiveColor, gm.emissiveFactor.data(), emissiveStrength);
            importTextureTransform(gm.emissiveTexture.extensions, m.emissiveColor);
        } else if (gm.emissiveFactor.size() == 3 &&
                   (gm.emissiveFactor[0] > 0 || gm.emissiveFactor[1] > 0 ||
                    gm.emissiveFactor[2] > 0)) {
            importValue3(m.emissiveColor, gm.emissiveFactor.data(), emissiveStrength);
        } else if (unlit) {
            m.emissiveColor = m.diffuseColor;
            std::array<double, 3> black = { 0, 0, 0 };
            importValue3(m.diffuseColor, black.data());
            m.isUnlit = true;
        }
        if (gm.alphaMode == "MASK") {
            importValue1(m.opacityThreshold, gm.alphaCutoff);
        }

        // Import normal map
        if (gm.normalTexture.index >= 0) {
            int imageIndex = importImage(ctx, gm.normalTexture.index, m.displayName, "normal");

            // Normal maps should not get the sRGB treatment and hence should be read as "raw"
            // 8-bit channel data
            importTexture(ctx.gltf,
                          imageIndex,
                          gm.normalTexture.index,
                          gm.normalTexture.texCoord,
                          m.normal,
                          AdobeTokens->rgb,
                          AdobeTokens->raw);
            importTextureTransform(gm.normalTexture.extensions, m.normal);
            // normal.scale for 8-bit normal maps is 2,2,2,1 and normal.bias is -1,-1,-1, 0
            // We then incorporate the scale from the glTF normalTexture into the
            // normal.scale and normal.bias. The official usdchecker will flag scale and bias
            // that are not 2 and -1 for normal map texture readers:
            // https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usdUtils/complianceChecker.py#L568
            float xyScale = 2.0f * gm.normalTexture.scale;
            float xyBias = -1.0f * gm.normalTexture.scale;
            m.normal.scale = GfVec4f(xyScale, xyScale, 2.0f, 1.0f);
            m.normal.bias = GfVec4f(xyBias, xyBias, -1.0f, 0.0f);
            importValue1(m.normalScale, gm.normalTexture.scale);
        }
        if (gm.occlusionTexture.index >= 0) {
            int imageIndex =
              importImage(ctx, gm.occlusionTexture.index, m.displayName, "occlusion");
            importTexture(ctx.gltf,
                          imageIndex,
                          gm.occlusionTexture.index,
                          gm.occlusionTexture.texCoord,
                          m.occlusion,
                          AdobeTokens->r,
                          AdobeTokens->raw);
            importScale1(m.occlusion, gm.occlusionTexture.strength);
            importTextureTransform(gm.occlusionTexture.extensions, m.occlusion);
        } else if (gm.occlusionTexture.strength != 1.0) {
            importValue1(m.occlusion, gm.occlusionTexture.strength);
        }
    }
}

void
importMeshJointWeights(const tinygltf::Model& model,
                       const tinygltf::Primitive& primitive,
                       Mesh& mesh)
{
    constexpr int MaxJointWeightSets = 8;
    static const std::vector<std::string> jointIndexKeys({ "JOINTS_0",
                                                           "JOINTS_1",
                                                           "JOINTS_2",
                                                           "JOINTS_3",
                                                           "JOINTS_4",
                                                           "JOINTS_5",
                                                           "JOINTS_6",
                                                           "JOINTS_7" });
    static const std::vector<std::string> jointWeightKeys({ "WEIGHTS_0",
                                                            "WEIGHTS_1",
                                                            "WEIGHTS_2",
                                                            "WEIGHTS_3",
                                                            "WEIGHTS_4",
                                                            "WEIGHTS_5",
                                                            "WEIGHTS_6",
                                                            "WEIGHTS_7" });

    int jointsIndices[MaxJointWeightSets];
    int weightsIndices[MaxJointWeightSets];
    jointsIndices[0] = getPrimitiveAttribute(primitive, jointIndexKeys[0]);
    weightsIndices[0] = getPrimitiveAttribute(primitive, jointWeightKeys[0]);

    // fast exit if there are no joints or weights.
    if (jointsIndices[0] == -1 && weightsIndices[0] == -1) {
        return;
    }

    int numJointSets = 1;
    for (int i = 1; i < MaxJointWeightSets; ++i) {
        jointsIndices[i] = getPrimitiveAttribute(primitive, jointIndexKeys[i]);
        weightsIndices[i] = getPrimitiveAttribute(primitive, jointWeightKeys[i]);
        if (jointsIndices[i] == -1)
            break;
        ++numJointSets;
    }

    int jointCounts[MaxJointWeightSets];
    int weightCounts[MaxJointWeightSets];
    for (int i = 0; i < numJointSets; ++i) {
        jointCounts[i] = getAccessorElementCount(model, jointsIndices[i]);
        weightCounts[i] = getAccessorElementCount(model, weightsIndices[i]);
    }

    // If there is no data, return
    if (jointCounts[0] == 0)
        return;

    // validate the joint indices and weights counts match
    for (int i = 0; i < numJointSets; ++i) {
        if (jointCounts[i] != weightCounts[i] || (i > 0 && jointCounts[i] != jointCounts[0])) {
            TF_WARN("Mismatch number of joint indices and weights for mesh '%s'",
                    mesh.displayName.c_str());
            return;
        }
    }

    const int vertexCount = jointCounts[0];

    mesh.joints = PXR_NS::VtArray<int>(vertexCount * numJointSets * 4);
    mesh.weights = PXR_NS::VtArray<float>(vertexCount * numJointSets * 4);

    if (numJointSets == 1) {
        readAccessorInts(model, jointsIndices[0], mesh.joints);
        readAccessorDataToFloat(
          model, weightsIndices[0], reinterpret_cast<float*>(mesh.weights.data()));
    } else {
        // read each pair of joint indices and weights
        PXR_NS::VtArray<int> joints[MaxJointWeightSets];
        PXR_NS::VtArray<float> weights[MaxJointWeightSets];
        for (int i = 0; i < numJointSets; ++i) {
            joints[i].resize(vertexCount * 4);
            readAccessorInts(model, jointsIndices[i], joints[i]);
            weights[i].resize(vertexCount * 4);
            readAccessorDataToFloat(
              model, weightsIndices[i], reinterpret_cast<float*>(weights[i].data()));
        }

        // combine the 4 values of joint indices and weights for each set of values into a
        // contiguous set of N*4 values per vertex
        int* jointsDst = reinterpret_cast<int*>(mesh.joints.data());
        float* weightsDst = reinterpret_cast<float*>(mesh.weights.data());
        for (int i = 0; i < vertexCount; ++i) {
            for (int j = 0; j < numJointSets; ++j) {
                memcpy(jointsDst, joints[j].data() + 4 * i, 4 * sizeof(int));
                memcpy(weightsDst, weights[j].data() + 4 * i, 4 * sizeof(float));
                jointsDst += 4;
                weightsDst += 4;
            }
        }
    }

    mesh.isRigid = false;
    mesh.influenceCount = numJointSets * 4;
}

/**
 * Helper function to extract the indices from the GLTF. If none are found, artificially create
 * them, assuming points define sequential triangles.
 *
 * @param model The tinygltf model containing the GLTF data, from which to extract the indices
 * @param indicesIndex The index of the accessor for the indices of the primitive. If this is
 *                     negative, then there is assumed to be no index data
 * @param numVertices The number of vertices in the mesh, for use in creating artificial indices
 *                    if none are found
 * @param dst The VtArray of ints to store the indices in. This array will be resized and
 * rewritten
 */
void
getIndices(const tinygltf::Model& model,
           int indicesIndex,
           int numVertices,
           PXR_NS::VtArray<int>& dst)
{
    if (indicesIndex >= 0) {
        dst.resize(getAccessorElementCount(model, indicesIndex));
        readAccessorInts(model, indicesIndex, dst);
    } else {
        dst.resize(numVertices);

        // Fills dst with increasing values starting at 0
        std::iota(dst.begin(), dst.end(), 0);
    }
}

void
importMeshes(ImportGltfContext& ctx)
{
    ctx.meshes.resize(ctx.gltf->meshes.size());
    ctx.meshUseCount.resize(ctx.gltf->meshes.size(), 0);
    for (size_t i = 0; i < ctx.gltf->meshes.size(); i++) {
        const tinygltf::Mesh& gmesh = ctx.gltf->meshes[i];
        ctx.meshes[i].resize(gmesh.primitives.size());
        for (size_t j = 0; j < gmesh.primitives.size(); j++) {

            // TODO: Combine primitives into a single large mesh if possible. When different
            // primitives have different materials, use a mesh subset to store this information.
            // Be aware of properly combining UV subsets

            const tinygltf::Primitive& primitive = gmesh.primitives[j];
            auto [meshIndex, mesh] = ctx.usd->addMesh();
            ctx.meshes[i][j] = meshIndex;
            mesh.displayName = gmesh.name;
            // When we have multiple GLTF primitives that we turn into meshes, we create names that
            // are derived from the primitive index instead of just duplicating the name.
            if (gmesh.primitives.size() > 1) {
                mesh.displayName = mesh.displayName + "_primitive" + std::to_string(j);
            }
            int positionsIndex = getPrimitiveAttribute(primitive, "POSITION");
            int normalsIndex = getPrimitiveAttribute(primitive, "NORMAL");
            int tangentsIndex = getPrimitiveAttribute(primitive, "TANGENT");
            int uvsIndex = getPrimitiveAttribute(primitive, "TEXCOORD_0");

            int indicesIndex = primitive.indices;

            mesh.points =
              PXR_NS::VtArray<PXR_NS::GfVec3f>(getAccessorElementCount(*ctx.gltf, positionsIndex));
            readAccessorDataToFloat(
              *ctx.gltf, positionsIndex, reinterpret_cast<float*>(mesh.points.data()));

            mesh.normals.values =
              PXR_NS::VtArray<PXR_NS::GfVec3f>(getAccessorElementCount(*ctx.gltf, normalsIndex));
            readAccessorDataToFloat(
              *ctx.gltf, normalsIndex, reinterpret_cast<float*>(mesh.normals.values.data()));
            mesh.normals.interpolation = UsdGeomTokens->vertex;

            mesh.tangents.values =
              PXR_NS::VtArray<PXR_NS::GfVec4f>(getAccessorElementCount(*ctx.gltf, tangentsIndex));
            readAccessorDataToFloat(
              *ctx.gltf, tangentsIndex, reinterpret_cast<float*>(mesh.tangents.values.data()));
            mesh.tangents.interpolation = UsdGeomTokens->vertex;

            mesh.uvs.values =
              PXR_NS::VtArray<PXR_NS::GfVec2f>(getAccessorElementCount(*ctx.gltf, uvsIndex));
            readAccessorDataToFloat(
              *ctx.gltf, uvsIndex, reinterpret_cast<float*>(mesh.uvs.values.data()));
            mesh.uvs.interpolation = UsdGeomTokens->vertex;

            // if there is one uv set, check for more
            if (uvsIndex >= 0 && mesh.uvs.values.size()) {
                // this is an infinite loop but will exit when TEXCOORD_n is not found
                for (int n = 1; true; n++) {
                    int uvsIndex =
                      getPrimitiveAttribute(primitive, "TEXCOORD_" + std::to_string(n));
                    if (uvsIndex < 0)
                        break;

                    // add a new primvar for the additional UV set
                    mesh.extraUVSets.push_back(Primvar<PXR_NS::GfVec2f>());
                    Primvar<PXR_NS::GfVec2f>& uvs = mesh.extraUVSets[n - 1];
                    uvs.values = PXR_NS::VtArray<PXR_NS::GfVec2f>(
                      getAccessorElementCount(*ctx.gltf, uvsIndex));
                    readAccessorDataToFloat(
                      *ctx.gltf, uvsIndex, reinterpret_cast<float*>(uvs.values.data()));
                    uvs.interpolation = UsdGeomTokens->vertex;
                }
            }

            switch (primitive.mode) {
                case TINYGLTF_MODE_TRIANGLES:
                    getIndices(*ctx.gltf, indicesIndex, mesh.points.size(), mesh.indices);

                    if (mesh.indices.size() < 3) {
                        TF_WARN("GLTF TRIANGLE primitive has fewer than 3 indices\n");
                    }
                    if (mesh.indices.size() % 3 != 0) {
                        TF_WARN("GLTF TRIANGLE primitive has a number of indices not divisible "
                                "by 3\n");
                    }

                    break;
                case TINYGLTF_MODE_TRIANGLE_STRIP: {
                    PXR_NS::VtArray<int> stripIndices;
                    getIndices(*ctx.gltf, indicesIndex, mesh.points.size(), stripIndices);

                    if (stripIndices.size() < 3) {
                        TF_WARN("GLTF TRIANGLE_STRIP primitive has fewer than 3 indices\n");
                    } else {
                        mesh.indices.resize(3 * (stripIndices.size() - 2));
                        for (size_t i = 0; i < stripIndices.size() - 2; i++) {
                            mesh.indices[3 * i] = stripIndices[i];
                            mesh.indices[3 * i + 1] = stripIndices[i + 1 + (i % 2)];
                            mesh.indices[3 * i + 2] = stripIndices[i + 2 - (i % 2)];
                        }
                    }

                    break;
                }
                case TINYGLTF_MODE_TRIANGLE_FAN: {
                    PXR_NS::VtArray<int> fanIndices;
                    getIndices(*ctx.gltf, indicesIndex, mesh.points.size(), fanIndices);

                    if (fanIndices.size() < 3) {
                        TF_WARN("GLTF TRIANGLE_FAN primitive has fewer than 3 indices\n");
                    } else {
                        mesh.indices.resize(3 * (fanIndices.size() - 2));
                        for (size_t i = 0; i < fanIndices.size() - 2; i++) {
                            mesh.indices[3 * i] = fanIndices[i + 1];
                            mesh.indices[3 * i + 1] = fanIndices[i + 2];
                            mesh.indices[3 * i + 2] = fanIndices[0];
                        }
                    }

                    break;
                }
                case TINYGLTF_MODE_POINTS:
                case TINYGLTF_MODE_LINE:
                case TINYGLTF_MODE_LINE_LOOP:
                case TINYGLTF_MODE_LINE_STRIP:
                default:
                    getIndices(*ctx.gltf, indicesIndex, mesh.points.size(), mesh.indices);

                    TF_WARN("Encountered GLTF primitive with unsupported mode %d\n",
                            primitive.mode);

                    break;
            }
            mesh.faces = PXR_NS::VtArray<int>(mesh.indices.size() / 3, 3);

            importMeshJointWeights(*ctx.gltf, primitive, mesh);

            VtVec3fArray color;
            VtFloatArray opacity;
            readColor(*ctx.gltf, primitive, color, opacity);
            if (color.size()) {
                auto [colorIndex, colorPV] = ctx.usd->addColorSet(meshIndex);
                colorPV.values = color;
                colorPV.interpolation = UsdGeomTokens->vertex;
            }
            if (opacity.size()) {
                auto [opacityIndex, opacityPV] = ctx.usd->addOpacitySet(meshIndex);
                opacityPV.values = opacity;
                opacityPV.interpolation = UsdGeomTokens->vertex;
            }
            if (primitive.material >= 0) {
                mesh.material = primitive.material;
                mesh.doubleSided = ctx.gltf->materials[primitive.material].doubleSided;
            }
        }
    }
}

// Import skeletons from gltf.
// First traverses all glTF nodes in the scene, to construct names appropriate for UsdSkel API
// consumption (for the Skeleton::joints attribute), of the form:
// rootJoint/childJoint1/childJoint2...
// Then traverses all glTF skins and assembles skeleton data in the Usdata cache.
// This doesn't specify instantiation of any skeletons, which is done by importNodes.
// It's ok that importNodes runs before this one, because the skins and skeletons counts are
// equal.
void
importSkeletons(ImportGltfContext& ctx)
{
    ctx.skeletonNodeNames.resize(ctx.gltf->nodes.size(), "");
    std::function<void(int parentIndex, int nodeIndex)> buildSkeletonNodeNames;
    buildSkeletonNodeNames = [&](int parentIndex, int nodeIndex) {
        const tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];
        std::string name = node.name;
        ctx.skeletonNodeNames[nodeIndex] =
          parentIndex >= 0 ? ctx.skeletonNodeNames[parentIndex] + "/" + name : name;
        for (size_t i = 0; i < node.children.size(); i++) {
            buildSkeletonNodeNames(nodeIndex, node.children[i]);
        }
        return true;
    };
    for (const tinygltf::Scene& scene : ctx.gltf->scenes) {
        for (int rootNodeIndex : scene.nodes) {
            buildSkeletonNodeNames(-1, rootNodeIndex);
        }
    }

    // Then build the skeletons
    for (size_t i = 0; i < ctx.gltf->skins.size(); i++) {
        const tinygltf::Skin& skin = ctx.gltf->skins[i];
        Skeleton& skeleton = ctx.usd->skeletons[i];
        skeleton.displayName = skin.name;
        skeleton.joints = PXR_NS::VtTokenArray(skin.joints.size());
        skeleton.jointNames = PXR_NS::VtTokenArray(skin.joints.size());
        skeleton.restTransforms = PXR_NS::VtMatrix4dArray(skin.joints.size());
        skeleton.bindTransforms = PXR_NS::VtMatrix4dArray(skin.joints.size());
        for (size_t j = 0; j < skin.joints.size(); j++) {
            int nodeIndex = skin.joints[j];
            const tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];
            Node& usdNode = ctx.usd->nodes[ctx.nodeMap[nodeIndex]];
            // Recall all glTF nodes are going to be imported as USD nodes
            // but we still mark this node as a skeleton joint in the cache.
            usdNode.isJoint = true;
            PXR_NS::GfVec3d t =
              node.translation.size()
                ? PXR_NS::GfVec3d(node.translation[0], node.translation[1], node.translation[2])
                : PXR_NS::GfVec3d(0);
            PXR_NS::GfRotation r =
              node.rotation.size()
                ? PXR_NS::GfQuatd(
                    node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])
                : PXR_NS::GfQuatd(0);
            PXR_NS::GfMatrix4d m = PXR_NS::GfMatrix4d(r, t);
            const std::string& name = ctx.skeletonNodeNames[nodeIndex];
            skeleton.joints[j] = PXR_NS::TfToken(name);
            skeleton.jointNames[j] = PXR_NS::TfToken(node.name);
            skeleton.restTransforms[j] = m;
        }
        PXR_NS::VtArray<PXR_NS::GfMatrix4f> inverseBindMatricesFloat(
          getAccessorElementCount(*ctx.gltf, skin.inverseBindMatrices));
        readAccessorData(*ctx.gltf,
                         skin.inverseBindMatrices,
                         reinterpret_cast<uint8_t*>(inverseBindMatricesFloat.data()));
        for (size_t i = 0; i < skin.joints.size(); i++) {
            skeleton.bindTransforms[i] =
              PXR_NS::GfMatrix4d(inverseBindMatricesFloat[i]).GetInverse();
        }
    }
}

template<typename T>
bool
importChannel(const tinygltf::Model& gltf,
              const tinygltf::AnimationChannel& channel,
              const tinygltf::AnimationSampler& sampler,
              const std::string& name,
              TimeValues<T>& values,
              float& minTime,
              float& maxTime)
{
    if (channel.target_path == name) {
        int offset = values.times.size();
        int count = getAccessorElementCount(gltf, sampler.input);
        int count2 = getAccessorElementCount(gltf, sampler.output);
        values.times.resize(offset + count);
        values.values.resize(offset + count2);
        readAccessorDataToFloat(gltf, sampler.input, reinterpret_cast<float*>(values.times.data()));
        readAccessorDataToFloat(
          gltf, sampler.output, reinterpret_cast<float*>(values.values.data()));
        minTime = std::min(minTime, values.times[0]);
        maxTime = std::max(maxTime, values.times[values.times.size() - 1]);
        return true;
    }
    return false;
}

void
importAnimationTracks(ImportGltfContext& ctx)
{
    int animationTrackCount = ctx.gltf->animations.size();
    ctx.usd->animationTracks.resize(animationTrackCount);

    for (int animationTrackIndex = 0; animationTrackIndex < animationTrackCount;
         animationTrackIndex++) {
        const tinygltf::Animation& animation = ctx.gltf->animations[animationTrackIndex];
        AnimationTrack& track = ctx.usd->animationTracks[animationTrackIndex];
        track.displayName = animation.name;
    }
}

void
importNodeAnimations(ImportGltfContext& ctx)
{
    for (int animationTrackIndex = 0; animationTrackIndex < ctx.usd->animationTracks.size();
         animationTrackIndex++) {
        const tinygltf::Animation& animation = ctx.gltf->animations[animationTrackIndex];
        AnimationTrack& track = ctx.usd->animationTracks[animationTrackIndex];

        for (const tinygltf::AnimationChannel& channel : animation.channels) {
            const tinygltf::AnimationSampler& sampler = animation.samplers[channel.sampler];
            Node& node = ctx.usd->nodes[ctx.nodeMap[channel.target_node]];

            // Modify the existing nodeAnimation if we had one, or use a new one if not
            bool hadNodeAnimation = !node.animations.empty();
            NodeAnimation newAnimation;
            NodeAnimation& nodeAnimation =
              hadNodeAnimation ? node.animations[animationTrackIndex] : newAnimation;

            bool hasNodeAnimation = false;
            hasNodeAnimation |= importChannel(*ctx.gltf,
                                              channel,
                                              sampler,
                                              "translation",
                                              nodeAnimation.translations,
                                              track.minTime,
                                              track.maxTime);
            hasNodeAnimation |= importChannel(*ctx.gltf,
                                              channel,
                                              sampler,
                                              "rotation",
                                              nodeAnimation.rotations,
                                              track.minTime,
                                              track.maxTime);
            hasNodeAnimation |= importChannel(*ctx.gltf,
                                              channel,
                                              sampler,
                                              "scale",
                                              nodeAnimation.scales,
                                              track.minTime,
                                              track.maxTime);
            if (channel.target_path == "weights") {
                TF_WARN("Unsupported import of GLTF blend weight animation");
            }

            if (hasNodeAnimation) {
                track.hasTimepoints = true;
                ctx.usd->hasAnimations = true;

                // If we didn't have a node animation before, set it up now
                if (!hadNodeAnimation) {
                    node.animations.resize(ctx.usd->animationTracks.size());
                    node.animations[animationTrackIndex] = std::move(newAnimation);
                }
            }
        }
    }
}

void
importSkeletonAnimations(ImportGltfContext& ctx)
{
    if (ctx.gltf->skins.size() <= 0)
        return;

    // Compute the set of all skeleteon nodes that are animated
    std::unordered_set<int> animatedNodeSet;
    for (size_t animationTrackIndex = 0; animationTrackIndex < ctx.usd->animationTracks.size();
         animationTrackIndex++) {
        const tinygltf::Animation& animation = ctx.gltf->animations[animationTrackIndex];

        // Select those animated nodes that correspond to skeleton nodes
        for (const tinygltf::AnimationChannel& channel : animation.channels) {
            if (!ctx.usd->nodes[ctx.nodeMap[channel.target_node]].isJoint) {
                const tinygltf::Node& node = ctx.gltf->nodes[channel.target_node];
                TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                             "Found non skeleton node %d %s\n",
                             channel.target_node,
                             node.name.c_str());
                continue;
            }
            animatedNodeSet.insert(channel.target_node);
        }
    }

    if (animatedNodeSet.empty()) {
        // We found no animated nodes - early out
        return;
    }

    for (size_t j = 0; j < ctx.gltf->skins.size(); j++) {
        const tinygltf::Skin& skin = ctx.gltf->skins[j];
        Skeleton& skeleton = ctx.usd->skeletons[j];

        // Determine the set of animated nodes affecting this skeleton
        std::vector<int> skelAnimNodes;
        for (size_t q = 0; q < skin.joints.size(); q++) {
            if (animatedNodeSet.count(skin.joints[q])) {
                skelAnimNodes.push_back(skin.joints[q]);
            }
        }

        if (skelAnimNodes.empty()) {
            // No animated nodes affecting this skeleton
            continue;
        }

        // This skeleton is animated by at lesat one animation track. Create SkeletonAnimations for
        // all tracks and poplulate them with the relevant animation data.
        skeleton.skeletonAnimations.resize(ctx.usd->animationTracks.size());
        skeleton.animatedJoints.resize(skelAnimNodes.size());
        for (size_t j = 0; j < skelAnimNodes.size(); j++) {
            std::string name = ctx.skeletonNodeNames[skelAnimNodes[j]];
            skeleton.animatedJoints[j] = PXR_NS::TfToken(name);
        }

        for (size_t animationTrackIndex = 0; animationTrackIndex < ctx.usd->animationTracks.size();
             animationTrackIndex++) {
            const tinygltf::Animation& animation = ctx.gltf->animations[animationTrackIndex];
            AnimationTrack& track = ctx.usd->animationTracks[animationTrackIndex];
            SkeletonAnimation& skeletonAnimation = skeleton.skeletonAnimations[animationTrackIndex];

            // Build a definitive time scale by inserting time points from every times array.
            // TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Assembling animation time");
            std::vector<float> definitiveTimes;
            for (int animNode : skelAnimNodes) {
                const Node& node = ctx.usd->nodes[ctx.nodeMap[animNode]];
                if (node.animations.size() > animationTrackIndex) {
                    const NodeAnimation& nodeAnimation = node.animations[animationTrackIndex];
                    addToTimeMap(definitiveTimes, nodeAnimation.rotations.times);
                    addToTimeMap(definitiveTimes, nodeAnimation.translations.times);
                    addToTimeMap(definitiveTimes, nodeAnimation.scales.times);
                }
            }
            // TODO: when implementing weights animation, might be able to remove this guard
            if (definitiveTimes.size() <= 0) {
                TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                             "Animation %lu %s has no times\n",
                             animationTrackIndex,
                             animation.name.c_str());
                continue;
            }
            track.hasTimepoints = true;
            ctx.usd->hasAnimations = true;
            track.minTime = std::min(track.minTime, definitiveTimes[0]);
            track.maxTime = std::max(track.maxTime, definitiveTimes[definitiveTimes.size() - 1]);

            // Interpolate animated values along the definitive time points
            // TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Interpolating animation");
            std::vector<PXR_NS::VtArray<PXR_NS::GfQuatf>> definitiveRotations(
              skelAnimNodes.size(),
              PXR_NS::VtArray<PXR_NS::GfQuatf>(definitiveTimes.size(), PXR_NS::GfQuatf(0)));
            std::vector<PXR_NS::VtArray<PXR_NS::GfVec3f>> definitiveTranslations(
              skelAnimNodes.size(),
              PXR_NS::VtArray<PXR_NS::GfVec3f>(definitiveTimes.size(), PXR_NS::GfVec3f(0)));
            std::vector<PXR_NS::VtArray<PXR_NS::GfVec3f>> definitiveScales(
              skelAnimNodes.size(),
              PXR_NS::VtArray<PXR_NS::GfVec3f>(definitiveTimes.size(), PXR_NS::GfVec3f(1)));
            for (size_t j = 0; j < skelAnimNodes.size(); j++) {
                const Node& n = ctx.usd->nodes[ctx.nodeMap[skelAnimNodes[j]]];
                const tinygltf::Node& node = ctx.gltf->nodes[skelAnimNodes[j]];
                const NodeAnimation emptyNodeAnimation;
                const NodeAnimation& na = n.animations.size() > animationTrackIndex
                                            ? n.animations[animationTrackIndex]
                                            : emptyNodeAnimation;

                if (na.rotations.values.size() > 1) {
                    interpolateData<PXR_NS::GfQuatf>(definitiveTimes,
                                                     na.rotations.times,
                                                     na.rotations.values,
                                                     definitiveRotations[j]);
                } else {
                    PXR_NS::GfQuatf restRotation =
                      node.rotation.size()
                        ? PXR_NS::GfQuatf(
                            node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])
                        : PXR_NS::GfQuatf(0);
                    definitiveRotations[j].assign(definitiveTimes.size(), restRotation);
                }
                if (na.translations.values.size() > 1) {
                    interpolateData<PXR_NS::GfVec3f>(definitiveTimes,
                                                     na.translations.times,
                                                     na.translations.values,
                                                     definitiveTranslations[j]);
                } else {
                    PXR_NS::GfVec3f restTranslation = node.translation.size()
                                                        ? PXR_NS::GfVec3f(node.translation[0],
                                                                          node.translation[1],
                                                                          node.translation[2])
                                                        : PXR_NS::GfVec3f(0);
                    definitiveTranslations[j].assign(definitiveTimes.size(), restTranslation);
                }
                if (na.scales.values.size() > 1) {
                    interpolateData<PXR_NS::GfVec3f>(
                      definitiveTimes, na.scales.times, na.scales.values, definitiveScales[j]);
                } else {
                    PXR_NS::GfVec3f restScale =
                      node.scale.size()
                        ? PXR_NS::GfVec3f(node.scale[0], node.scale[1], node.scale[2])
                        : PXR_NS::GfVec3f(1);
                    definitiveScales[j].assign(definitiveTimes.size(), restScale);
                }
            }

            skeletonAnimation.times.resize(definitiveTimes.size());
            skeletonAnimation.rotations.resize(
              definitiveTimes.size(), PXR_NS::VtArray<PXR_NS::GfQuatf>(skelAnimNodes.size()));
            skeletonAnimation.translations.resize(
              definitiveTimes.size(), PXR_NS::VtArray<PXR_NS::GfVec3f>(skelAnimNodes.size()));
            skeletonAnimation.scales.resize(definitiveTimes.size(),
                                            PXR_NS::VtArray<PXR_NS::GfVec3h>(skelAnimNodes.size()));
            for (size_t j = 0; j < definitiveTimes.size(); j++) {
                // TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Time[" << k << "] = " << definitiveTimes[j]);
                skeletonAnimation.times[j] = definitiveTimes[j];
                for (size_t k = 0; k < skelAnimNodes.size(); k++) {
                    skeletonAnimation.rotations[j][k] = definitiveRotations[k][j];
                    skeletonAnimation.translations[j][k] =
                      PXR_NS::GfVec3f(definitiveTranslations[k][j]);
                    skeletonAnimation.scales[j][k] = PXR_NS::GfVec3h(definitiveScales[k][j]);
                }
            }
        }
    }
}

void
importLights(ImportGltfContext& ctx)
{
    for (size_t i = 0; i < ctx.gltf->lights.size(); ++i) {
        const tinygltf::Light& gltfLight = ctx.gltf->lights[i];

        // Add general light info

        auto [lightIndex, light] = ctx.usd->addLight();

        light.displayName = gltfLight.name;
        if (gltfLight.color.size() >= 3) {
            light.color[0] = gltfLight.color[0];
            light.color[1] = gltfLight.color[1];
            light.color[2] = gltfLight.color[2];
        }

        // USD uses lights that emit based on their surface area, so will multiply the intensity
        // below based on the light type
        float intensity = gltfLight.intensity;

        // Add type-specific light info

        if (gltfLight.type == "directional") {
            light.type = LightType::Sun;

            intensity /= GLTF_DIRECTIONAL_LIGHT_INTENSITY_MULT;

        } else if (gltfLight.type == "point") {
            light.type = LightType::Sphere;

            // Divide by the surface area of a sphere, 4 pi r^2
            intensity /= (4.0 * M_PI * DEFAULT_POINT_LIGHT_RADIUS * DEFAULT_POINT_LIGHT_RADIUS);
            intensity /= GLTF_POINT_LIGHT_INTENSITY_MULT;

            // glTF lights have no radius, so we use a default value
            light.radius = DEFAULT_POINT_LIGHT_RADIUS;

        } else if (gltfLight.type == "spot") {
            light.type = LightType::Disk;

            // Divide by the area of a disk, pi r^2
            intensity /= (M_PI * DEFAULT_SPOT_LIGHT_RADIUS * DEFAULT_SPOT_LIGHT_RADIUS);
            intensity /= GLTF_SPOT_LIGHT_INTENSITY_MULT;

            // glTF lights have no radius, so we use a default value
            light.radius = DEFAULT_SPOT_LIGHT_RADIUS;

            // glTF inner cone angle is from the center to where falloff begins, and outer cone
            // angle is from the center to where falloff ends. Meanwhile, in USD, angle is from
            // the center to the edge of the cone, and softness is a number from 0 to 1 indicating
            // how close to the center the falloff begins.

            // glTF outer cone angle is equivalent to USD cone angle
            ctx.usd->lights[i].coneAngle = GfRadiansToDegrees(gltfLight.spot.outerConeAngle);

            if (gltfLight.spot.outerConeAngle > 0) {
                // Get the fraction of the cone containing the falloff
                ctx.usd->lights[i].coneFalloff =
                  1 - (gltfLight.spot.innerConeAngle / gltfLight.spot.outerConeAngle);
            } else {
                ctx.usd->lights[i].coneFalloff = 0;
            }
        }

        ctx.usd->lights[i].intensity = intensity;
    }
}

// Import neural graphics primitives from gltf
void
importNgpExtension(const tinygltf::Value& ngp, NgpData& ngpData)
{
    auto importUncompressedFloatArray =
      [&ngp](const char* name, PXR_NS::VtFloatArray& dst, std::size_t d1 = 0, std::size_t d2 = 0) {
          const auto& val = ngp.Get(name);
          if (val.Type() == tinygltf::STRING_TYPE) {
              std::vector<uint8_t> data;
              unpackBase64String(val.Get<std::string>(), false, data);
              dst.resize(data.size() / sizeof(float));

              if (d1 == 0 || d2 == 0) {
                  memcpy(dst.data(), data.data(), data.size());
              } else {
                  unpackMLPWeight(reinterpret_cast<const float*>(data.data()), dst.data(), d1, d2);
              }
          }
      };

    importUncompressedFloatArray("spatial_mlp_l0_weight", ngpData.densityMlpLayer0Weight, 24, 32);
    importUncompressedFloatArray("spatial_mlp_l0_bias", ngpData.densityMlpLayer0Bias);
    importUncompressedFloatArray("spatial_mlp_l1_weight", ngpData.densityMlpLayer1Weight, 16, 24);
    importUncompressedFloatArray("spatial_mlp_l1_bias", ngpData.densityMlpLayer1Bias);
    importUncompressedFloatArray("vdep_mlp_l0_weight", ngpData.colorMlpLayer0Weight, 24, 36);
    importUncompressedFloatArray("vdep_mlp_l0_bias", ngpData.colorMlpLayer0Bias);
    importUncompressedFloatArray("vdep_mlp_l1_weight", ngpData.colorMlpLayer1Weight, 24, 24);
    importUncompressedFloatArray("vdep_mlp_l1_bias", ngpData.colorMlpLayer1Bias);
    importUncompressedFloatArray("vdep_mlp_l2_weight", ngpData.colorMlpLayer2Weight, 4, 24);
    importUncompressedFloatArray("vdep_mlp_l2_bias", ngpData.colorMlpLayer2Bias);

    const auto& densityGridVal = ngp.Get("density");
    const auto& densityGridValMax = ngp.Get("density_max");
    if (densityGridVal.Type() == tinygltf::STRING_TYPE &&
        densityGridValMax.Type() == tinygltf::REAL_TYPE) {
        float densityMax = static_cast<float>(densityGridValMax.Get<double>());
        std::vector<uint8_t> data;
        unpackBase64String(densityGridVal.Get<std::string>(), true, data);
        ngpData.densityGrid.resize(data.size());
        for (size_t i = 0; i < data.size(); ++i) {
            ngpData.densityGrid[i] = static_cast<float>(data[i]) * densityMax / 255.0f;
        }
    }

    const auto& distanceGridVal = ngp.Get("distance_grid");
    const auto& distanceGridValMax = ngp.Get("distance_max");
    if (distanceGridVal.Type() == tinygltf::STRING_TYPE &&
        distanceGridValMax.Type() == tinygltf::REAL_TYPE) {
        float distanceMax = static_cast<float>(distanceGridValMax.Get<double>());
        std::vector<uint8_t> data;
        unpackBase64String(distanceGridVal.Get<std::string>(), true, data);
        ngpData.distanceGrid.resize(data.size());
        for (size_t i = 0; i < data.size(); ++i) {
            float sqrtVal = static_cast<float>(data[i]) / 255.0f;
            ngpData.distanceGrid[i] = sqrtVal * sqrtVal * distanceMax;
        }
    }

    const auto& hashGridVal = ngp.Get("hash_grid");
    if (hashGridVal.Type() == tinygltf::STRING_TYPE) {
        std::vector<uint8_t> data;
        unpackBase64String(hashGridVal.Get<std::string>(), true, data);
        ngpData.hashGrid.resize(data.size() / sizeof(uint16_t));
        float16ToFloat32(reinterpret_cast<const uint16_t*>(data.data()),
                         ngpData.hashGrid.data(),
                         ngpData.hashGrid.size());
    }

    const auto& densityThresholdVal = ngp.Get("sigma_threshold");
    if (densityThresholdVal.Type() == tinygltf::REAL_TYPE) {
        ngpData.densityThreshold = static_cast<float>(densityThresholdVal.Get<double>());
    }

    const auto& hashGridResolutionVal = ngp.Get("hash_grid_res");
    if (hashGridResolutionVal.Type() == tinygltf::ARRAY_TYPE) {
        tinygltf::Value::Array resArray = hashGridResolutionVal.Get<tinygltf::Value::Array>();
        ngpData.hashGridResolution.resize(resArray.size());
        for (size_t i = 0; i < resArray.size(); ++i) {
            ngpData.hashGridResolution[i] = resArray[i].Get<int>();
        }
    }

    // GLTF data is Z-up, needs to be rotated to Y-up
    ngpData.hasTransform = true;
    ngpData.transform =
      GfMatrix4d(GfRotation(GfVec3d(1.0, 0.0, 0.0), -90.0), GfVec3d(0.0, 0.0, 0.0));
}

// Import nodes from tinygltf Model to UsdData.
// We traverse the glTF nodes recursively from root to children and assign each node a usd index
// k. We maintain a mapping from the gltf node index to the usd node index in `nodeMap` for
// reference.
// For nodes with mesh and skin, we add the mesh to the root node of the skeleton held by the
// skin.
bool
importNodes(ImportGltfContext& ctx)
{
    int k = 0;
    int nodeCount = ctx.gltf->nodes.size();
    ctx.nodeMap.resize(nodeCount);    // maps glTF node index to USD node index
    ctx.usd->nodes.resize(nodeCount); // stores USD nodes in order of traversal
    ctx.parentMap.resize(nodeCount);  // maps glTF node index to parent glTF node index

    // Stores gltf nodeIndex
    std::vector<int> skinnedNodes;

    std::function<int(int parentIndex, int nodeIndex)> traverse;
    traverse = [&](int parentIndex, int nodeIndex) -> int {
        const tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];
        int usdNodeIndex = k++;
        Node& n = ctx.usd->nodes[usdNodeIndex];
        ctx.nodeMap[nodeIndex] = usdNodeIndex;
        ctx.parentMap[nodeIndex] = parentIndex;
        n.displayName = node.name;
        n.translation =
          !node.translation.empty()
            ? PXR_NS::GfVec3d(node.translation[0], node.translation[1], node.translation[2])
            : PXR_NS::GfVec3d(0);
        n.rotation = !node.rotation.empty()
                       ? PXR_NS::GfQuatf(
                           node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])
                       : PXR_NS::GfQuatf(0);
        n.scale = !node.scale.empty() ? PXR_NS::GfVec3f(node.scale[0], node.scale[1], node.scale[2])
                                      : PXR_NS::GfVec3f(1);
        if (!node.matrix.empty()) {
            n.hasTransform = true;
            copyMatrix(node.matrix, n.transform);
        }
        if (node.camera >= 0) {
            n.camera = node.camera;
        }
        if (node.light >= 0) {
            n.light = node.light;
        }
        int usdParentIndex = (parentIndex != -1) ? ctx.nodeMap[parentIndex] : -1;
        n.parent = usdParentIndex;
        if (node.mesh >= 0) {
            ctx.meshUseCount[node.mesh]++;
            // If the node has a skin, add the mesh to the root node of the skeleton held by the
            // skin.
            if (node.skin >= 0) {
                // Defer setting up relationships for skinned nodes until all nodes have been
                // traversed
                skinnedNodes.push_back(nodeIndex);
            } else {
                n.staticMeshes = ctx.meshes[node.mesh];
            }
        }
        const auto ngp_iter = node.extensions.find(getNerfExtString());
        if (ngp_iter != node.extensions.end()) {
            const tinygltf::Value& ngp = ngp_iter->second;
            n.ngp = ctx.usd->ngps.size();
            ctx.usd->ngps.push_back(NgpData());
            importNgpExtension(ngp, ctx.usd->ngps[n.ngp]);
        }

        n.children.resize(node.children.size());
        for (size_t i = 0; i < node.children.size(); i++) {
            n.children[i] = traverse(nodeIndex, node.children[i]);
        }
        return usdNodeIndex;
    };

    // We do not preserve the original names of scenes we import, since scenes aren't preserved
    // when we import to USD from glTF, and since we won't export multiple scenes back to glTF
    for (const tinygltf::Scene& scene : ctx.gltf->scenes) {
        for (int rootNodeIndex : scene.nodes) {
            int usdNodeIndex = traverse(-1, rootNodeIndex);
            ctx.usd->rootNodes.push_back(usdNodeIndex);
        }
    }

    // Set up relationships for skinned nodes, now that the traversal is done
    for (int nodeIndex : skinnedNodes) {
        const tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];

        int gltfSkinRootNodexIndex = nodeIndex;
        int gltfSkeletonNodeIndex = ctx.gltf->skins[node.skin].skeleton;
        // If the skin has a skeleton, find the parent node of the skeleton
        if (gltfSkeletonNodeIndex >= 0) {
            int gltfSkeletonNodeParentIndex = ctx.parentMap[gltfSkeletonNodeIndex];

            // Check if the parent of the skeleton exists
            if (gltfSkeletonNodeParentIndex != -1) {
                gltfSkinRootNodexIndex = gltfSkeletonNodeParentIndex;
            }
        } else {
            // If the skin has no skeleton, find the parent node of the skin
            int parentIndex = ctx.parentMap[nodeIndex];
            if (parentIndex != -1) {
                gltfSkinRootNodexIndex = parentIndex;
            }
        }

        int usdSkinRootNodeIndex = ctx.nodeMap[gltfSkinRootNodexIndex];

        Skeleton& skeleton = ctx.usd->skeletons[node.skin];
        skeleton.parent = usdSkinRootNodeIndex;

        auto& skinningTargets = skeleton.meshSkinningTargets;
        for (auto m : ctx.meshes[node.mesh]) {
            if (std::find(skinningTargets.begin(), skinningTargets.end(), m) ==
                skinningTargets.end()) {
                skinningTargets.push_back(m);
            }
        }
    }

    return true;
}

void
checkMeshInstancing(ImportGltfContext& ctx)
{
    // Visit all meshes and check if they are used by more than one node and if so mark them as
    // instanceable
    for (size_t meshIdx = 0; meshIdx < ctx.meshUseCount.size(); ++meshIdx) {
        int useCount = ctx.meshUseCount[meshIdx];
        if (useCount > 1) {
            const std::vector<int>& meshPrimitiveIndices = ctx.meshes[meshIdx];
            for (int primitiveIdx : meshPrimitiveIndices) {
                ctx.usd->meshes[primitiveIdx].instanceable = true;
            }
        }

        if (useCount == 0) {
            const tinygltf::Mesh& gmesh = ctx.gltf->meshes[meshIdx];
            TF_WARN("Mesh %zu (%s) appears to be unused", meshIdx, gmesh.name.c_str());
        }
    }
}

static const std::set<std::string> supportedExtension = {
    // Ratified extensions
    "KHR_draco_mesh_compression",
    "KHR_lights_punctual",
    "KHR_materials_anisotropy",
    "KHR_materials_clearcoat",
    "KHR_materials_emissive_strength",
    "KHR_materials_ior",
    // "KHR_materials_iridescence",
    "KHR_materials_sheen",
    "KHR_materials_specular",
    "KHR_materials_transmission",
    "KHR_materials_unlit",
    // "KHR_materials_variants",
    "KHR_materials_volume",
    // "KHR_mesh_quantization",
    // "KHR_texture_basisu",
    "KHR_texture_transform",
    // "KHR_xmp_json_ld",
    // "EXT_mesh_gpu_instancing",
    // "EXT_meshopt_compression",
    "EXT_texture_webp",

    // Vendor extensions
    "ADOBE_materials_clearcoat_specular",
    "ADOBE_materials_clearcoat_tint",
    getNerfExtString(),

    // Archived extensions
    "KHR_materials_pbrSpecularGlossiness",

    // In-development extensions
    "KHR_materials_diffuse_transmission",
    "KHR_materials_subsurface",
    "KHR_materials_sss" // previous name of KHR_materials_subsurface
};

void
checkExtensions(const std::vector<std::string>& extensionsUsed,
                const std::vector<std::string>& extensionsRequired)
{
    std::set<std::string> unsupportedExtensions;

    if (!extensionsUsed.empty()) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "GTLF extensions used:\n");
    }
    for (const std::string& ext : extensionsUsed) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "  %s\n", ext.c_str());
        if (supportedExtension.count(ext) == 0) {
            unsupportedExtensions.insert(ext);
        }
    }
    for (const std::string& ext : extensionsRequired) {
        if (supportedExtension.count(ext) == 0) {
            unsupportedExtensions.insert(ext);
        }
    }

    if (!unsupportedExtensions.empty()) {
        std::ostringstream ss;
        ss << "Asset uses unsupported glTF extensions:\n";
        for (const std::string& ext : unsupportedExtensions) {
            ss << "  " << ext << "\n";
        }
        std::string warningMsg = ss.str();
        TF_WARN(warningMsg);
    }
}

bool
importGltf(const ImportGltfOptions& options,
           tinygltf::Model& model,
           UsdData& usd,
           const std::string& filename)
{
    checkExtensions(model.extensionsUsed, model.extensionsRequired);

    ImportGltfContext ctx;
    ctx.options = &options;

    // Add filename of imported file and any paths to external buffers
    // to the list of filenames which will be used as metadata
    std::string baseName = TfGetBaseName(filename);
    ctx.filenames.push_back(baseName);
    for (auto buffer : model.buffers) {
        // Filter out uris which are data references (ie the uri starts with "data:")
        if (!buffer.uri.empty() && buffer.uri.compare(0, 5, "data:", 5) != 0) {
            ctx.filenames.push_back(buffer.uri);
        }
    }

    ctx.usd = &usd;
    ctx.gltf = &model;
    usd.doc = "gltf2usd";
    usd.upAxis = UsdGeomTokens->y;
    usd.metersPerUnit = 1.0;

    // glTF defines time in seconds
    usd.timeCodesPerSecond = 1.0;

    if (!importMetadata(ctx)) {
        return false;
    }
    importCameras(ctx);

    if (options.importMaterials) {
        importMaterials(ctx);
    }
    if (options.importGeometry) {
        importLights(ctx);
        importMeshes(ctx);
        // Resize the skeletons array before importing nodes, to allow skinning targets to be
        // added during importNodes
        ctx.usd->skeletons.resize(ctx.gltf->skins.size());
        importNodes(ctx);
        importSkeletons(ctx);
        importAnimationTracks(ctx);
        importNodeAnimations(ctx);
        importSkeletonAnimations(ctx);
        checkMeshInstancing(ctx);
    }

    usd.metadata.SetValueAtPath("filenames", VtValue(ctx.filenames));
    return true;
}
}
