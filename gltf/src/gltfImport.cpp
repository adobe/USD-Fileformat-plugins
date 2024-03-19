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
#include "gltfSpecGloss.h"
#include "importGltfContext.h"
#include "neuralAssetsHelper.h"
#include <common.h>
#include <images.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>


#include <algorithm>

using namespace PXR_NS;

namespace adobe::usd {

// Metadata on glTF is found in various fields of the asset entity.
// Metadata on USD will be stored uniformily in the CustomLayerData dictionary.
void
importMetadata(ImportGltfContext& ctx)
{
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
}

void
importCameras(ImportGltfContext& ctx)
{
    ctx.usd->cameras.resize(ctx.gltf->cameras.size());
    for (size_t i = 0; i < ctx.gltf->cameras.size(); i++) {
        const tinygltf::Camera& gCamera = ctx.gltf->cameras[i];
        Camera& usdCamera = ctx.usd->cameras[i];
        GfCamera& uCamera = usdCamera.camera;
        usdCamera.name = gCamera.name;
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

// Note, if a single texture channel is read from a RGB texture, like in the case of of reading the
// roughness channel from a metalRoughness texture, the texture reader needs to be marked as
// reading from a "raw" color space instead of sRGB. The same is true for reading normal maps
bool
importTexture(const tinygltf::Model* gltf,
              int imageIndex,
              int textureIndex,
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
    } else {
        // The GLTF spec defaults to 'repeat' and we need to explicitly set that, since the default
        // in USD is 'black' (technically 'useMetadata')
        input.wrapS = AdobeTokens->repeat;
        input.wrapT = AdobeTokens->repeat;
    }
    input.image = imageIndex;
    input.uvIndex = 0;
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
    // (1.0 - V) flip which is applied here. Previously, we were flipping the
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
    if (rotation.IsNumber()) {
        float rot = rotation.GetNumberAsDouble() * rad2deg;
        if (rot != 0.0f) {
            input.transformRotation = rot;
        }
    }

    // As mentioned above, the V flip needs to be applied here. This is done
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
        importTexture(ctx.gltf, imageIndex, texture.index, input, channels, AdobeTokens->raw);
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
        importTexture(
          ctx.gltf, imageIndex, texture.index, input, AdobeTokens->rgb, AdobeTokens->sRGB);
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
        importTexture(
          ctx.gltf, imageIndex, texture.index, input, AdobeTokens->rgb, AdobeTokens->raw);
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

struct Anisotropy
{
    double strength = 0.0;
    double rotation = 0.0;
    tinygltf::TextureInfo texture; // rg are a 2D direction, b is a strength multiplier
};

bool
importAnisotropy(const tinygltf::ExtensionMap& extensions, Anisotropy* anisotropy)
{
    auto extIt = extensions.find("KHR_materials_anisotropy");
    if (extIt != extensions.end()) {
        const tinygltf::Value& anisoExt = extIt->second;
        readDoubleValue(anisoExt.Get("anisotropyStrength"), anisotropy->strength);
        readDoubleValue(anisoExt.Get("anisotropyRotation"), anisotropy->rotation);
        readTextureInfo(anisoExt.Get("anisotropyTexture"), anisotropy->texture);
        return true;
    }

    return false;
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

int
_getTextureIndex(const tinygltf::Value& value)
{
    if (value.IsObject()) {
        const tinygltf::Value& indexVal = value.Get("index");
        if (indexVal.IsInt()) {
            return indexVal.GetNumberAsInt();
        }
    }
    return -1;
}

void
importMaterials(ImportGltfContext& ctx)
{
    // map used to track created textures converted from specular glossiness to avoid duplication
    std::unordered_map<std::string, int> specGlossTextureCache;

    ctx.usd->materials.resize(ctx.gltf->materials.size());
    for (size_t i = 0; i < ctx.gltf->materials.size(); i++) {
        const tinygltf::Material& gm = ctx.gltf->materials[i];
        Material& m = ctx.usd->materials[i];
        m.name = gm.name.empty() ? "Material" + std::to_string(i) : gm.name;

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

            int diffTextureIndex = _getTextureIndex(diffuseTextureVal);
            if (diffTextureIndex >= 0) {
                int imageIndex = importImage(ctx, diffTextureIndex, m.name, "diffuse");
                importTexture(ctx.gltf,
                              imageIndex,
                              diffTextureIndex,
                              diffuseColor,
                              AdobeTokens->rgb,
                              AdobeTokens->sRGB);
                importTextureTransform(gm.extensions, diffuseColor);

                if (gm.alphaMode == "BLEND" || gm.alphaMode == "MASK") {
                    opacity = diffuseColor;
                    importTexture(ctx.gltf,
                                  imageIndex,
                                  diffTextureIndex,
                                  opacity,
                                  AdobeTokens->a,
                                  AdobeTokens->raw);
                    importScale1(opacity, diffuseFactor[3]);
                }
            }

            int specGlossTextureIndex = _getTextureIndex(specGlossTextureVal);
            if (specGlossTextureIndex >= 0) {
                int imageIndex = importImage(ctx, specGlossTextureIndex, m.name, "specGloss");
                importTexture(ctx.gltf,
                              imageIndex,
                              specGlossTextureIndex,
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
            if (diffuseTexture >= 0) {
                int imageIndex = importImage(ctx, diffuseTexture, m.name, "diffuse");
                importTexture(ctx.gltf,
                              imageIndex,
                              diffuseTexture,
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
            if (mrTexture >= 0) {
                int imageIndex = importImage(ctx, mrTexture, m.name, "metallicRoughness");
                importTexture(
                  ctx.gltf, imageIndex, mrTexture, m.roughness, AdobeTokens->g, AdobeTokens->raw);
                importTexture(
                  ctx.gltf, imageIndex, mrTexture, m.metallic, AdobeTokens->b, AdobeTokens->raw);

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
                            m.name,
                            "specularLevel",
                            m.specularLevel,
                            specular.texture,
                            AdobeTokens->a,
                            &specular.factor,
                            1.0);
                importColorInput(ctx,
                                 m.name,
                                 "specularColor",
                                 m.specularColor,
                                 specular.colorTexture,
                                 specular.colorFactor,
                                 1.0);
            }

            Anisotropy anisotropy;
            if (importAnisotropy(gm.extensions, &anisotropy)) {
                // Note from the GTLF spec regarding the rotation:
                //  The rotation of the anisotropy in tangent, bitangent space, measured in radians
                //  counter-clockwise from the tangent. When anisotropyTexture is present,
                //  anisotropyRotation provides additional rotation to the vectors in the texture.
                //
                // Note from the ASM 4.0 spec regarding the angle:
                //  Counterclockwise rotation of anisotropy of surface layer from the tangent
                //  direction, normalized from 0° to 360°. Note that the appearance of the specular
                //  highlight is identical between 0–0.5 and 0.5–1; this allows the preservation of
                //  value when converting to/from other models that support directional anisotropy.
                constexpr double PI = 3.14159265358979311600;
                constexpr double oneOverTwoPI = 0.15915494309189535;

                if (anisotropy.texture.index >= 0) {
                    int imageIndex =
                      importImage(ctx, anisotropy.texture.index, m.name, "anisotropy");
                    importTexture(ctx.gltf,
                                  imageIndex,
                                  anisotropy.texture.index,
                                  m.anisotropyLevel,
                                  AdobeTokens->b,
                                  AdobeTokens->raw);
                    importTextureTransform(gm.extensions, m.anisotropyLevel);
                    // XXX ASM uses a different strength, which is unfortunately roughness
                    // dependent.
                    // asmAnisoLevel = sqrt((1.0 - roughness * roughness) * strength * strength)
                    importScale1(m.anisotropyLevel, anisotropy.strength);

                    // XXX The GLTF anisotropy texture uses a 2D vector encoding for the direction
                    // of the anisotropy in the R and G channels. There is an implementation for the
                    // conversion for single angle that ASM uses in Stager. We need to port this at
                    // some point
                    // vec dir = (red * 2.0 - 1.0, green * 2.0 - 1.0)
                    // rotation = atan2f(dir.y, dir.x) + const_rotation
                    TF_WARN(
                      "Material %s uses anisotropy texture which we can't convert to an angle. "
                      "The directionality will be lost",
                      m.name.c_str());
                    // Convert to ASM anisotropy angle
                    double angle = (anisotropy.rotation + PI) * oneOverTwoPI;
                    importValue1(m.anisotropyAngle, angle);
                } else {
                    TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                                 "ANISOTROPY %lf %lf\n",
                                 anisotropy.strength,
                                 anisotropy.rotation);
                    if (anisotropy.strength != 0.0) {
                        // XXX ASM uses a different strength, which is unfortunately roughness
                        // dependent. If roughness is a texture, this also becomes a texture
                        // asmAnisoLevel = sqrt((1.0 - roughness * roughness) * strength * strength)
                        importValue1(m.anisotropyLevel, anisotropy.strength);
                    }
                    if (anisotropy.rotation != 0.0) {
                        // Convert to ASM anisotropy angle
                        double angle = (anisotropy.rotation + PI) * oneOverTwoPI;
                        importValue1(m.anisotropyAngle, angle);
                    }
                }
            }

            Clearcoat clearcoat;
            if (importClearcoat(gm.extensions, &clearcoat)) {
                importInput(ctx,
                            m.name,
                            "clearcoat",
                            m.clearcoat,
                            clearcoat.texture,
                            AdobeTokens->r,
                            &clearcoat.factor);
                importInput(ctx,
                            m.name,
                            "clearcoatRoughness",
                            m.clearcoatRoughness,
                            clearcoat.roughnessTexture,
                            AdobeTokens->g,
                            &clearcoat.roughnessFactor);
                importNormalInput(
                  ctx, m.name, "clearcoatNormal", m.clearcoatNormal, clearcoat.normalTexture);
            }

            AdobeClearcoatSpecular clearcoatSpecular;
            if (importAdobeClearcoatSpecular(gm.extensions, &clearcoatSpecular)) {
                importValue1(m.clearcoatIor, clearcoatSpecular.ior);
                importInput(ctx,
                            m.name,
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
                                 m.name,
                                 "clearcoatColor",
                                 m.clearcoatColor,
                                 clearcoatTint.texture,
                                 clearcoatTint.factor,
                                 1.0);
            }

            Sheen sheen;
            if (importSheen(gm.extensions, &sheen)) {
                importColorInput(
                  ctx, m.name, "sheenColor", m.sheenColor, sheen.colorTexture, sheen.colorFactor);
                importInput(ctx,
                            m.name,
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
                            m.name,
                            "transmission",
                            m.transmission,
                            transmission.texture,
                            AdobeTokens->r,
                            &transmission.factor);
                hasTransmission = true;
                // Note, the GLTF material model uses the baseColor to tint transmission through a
                // surface. To emulate that behavior with ASM 4.0 we try to map the baseColor to
                // the clearcoatColor and activate the clearcoat. This becomes complicated if
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
                                    m.name.c_str());
                        }
                    } else {
                        TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                                     "Can't touch clearcoat lobe to enable "
                                     "transmission tinting on material %s\n",
                                     m.name.c_str());
                    }
                }
            }

            DiffuseTransmission diffuseTransmission;
            if (importDiffuseTransmission(gm.extensions, &diffuseTransmission)) {
                // Note, the ASM 4.0 model does not have a diffuse transmission lobe, so we're
                // approximating this effect by mapping it to general micro-facet transmission and
                // volume absorption. Ideally we would make the micro-facet roughness very high to
                // approach a diffuse transmission, but this would mess with general specular, so
                // we're not changing roughness.
                if (!hasTransmission) {
                    importInput(ctx,
                                m.name,
                                "transmission",
                                m.transmission,
                                diffuseTransmission.texture,
                                AdobeTokens->a,
                                &diffuseTransmission.factor);
                    importColorInput(ctx,
                                     m.name,
                                     "absorptionColor",
                                     m.absorptionColor,
                                     diffuseTransmission.colorTexture,
                                     diffuseTransmission.colorFactor);
                } else {
                    TF_WARN("Material %s has both KHR_materials_transmission and "
                            "KHR_materials_diffuse_transmission. Ignoring the latter.",
                            m.name.c_str());
                }
            }

            Volume volume;
            if (importVolume(gm.extensions, &volume) && volume.thicknessFactor > 0.0) {
                importInput(ctx,
                            m.name,
                            "thickness",
                            m.thickness,
                            volume.thicknessTexture,
                            AdobeTokens->g,
                            &volume.thicknessFactor);
                importValue1(m.absorptionDistance, volume.attenuationDistance);
                // absorptionColor from the extension is a constant and we use it as a multiplier
                // on the existing absorptionColor, which is often the same as diffuse
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
        double emissiveStrength = 1.0;
        importEmissionStrength(gm.extensions, &emissiveStrength);
        if (gm.emissiveTexture.index >= 0) {
            int imageIndex = importImage(ctx, gm.emissiveTexture.index, m.name, "emissive");
            importTexture(ctx.gltf,
                          imageIndex,
                          gm.emissiveTexture.index,
                          m.emissiveColor,
                          AdobeTokens->rgb,
                          AdobeTokens->sRGB);
            importScale3(m.emissiveColor, gm.emissiveFactor.data(), emissiveStrength);
            importTextureTransform(gm.emissiveTexture.extensions, m.emissiveColor);
        } else if (gm.emissiveFactor.size() == 3 &&
                   (gm.emissiveFactor[0] > 0 || gm.emissiveFactor[1] > 0 ||
                    gm.emissiveFactor[2] > 0)) {
            importValue3(m.emissiveColor, gm.emissiveFactor.data(), emissiveStrength);
        }
        if (gm.alphaMode == "MASK") {
            importValue1(m.opacityThreshold, gm.alphaCutoff);
        }
        if (gm.normalTexture.index >= 0) {
            int imageIndex = importImage(ctx, gm.normalTexture.index, m.name, "normal");

            // Normal maps should not get the sRGB treatment and hence should be read as "raw"
            importTexture(ctx.gltf,
                          imageIndex,
                          gm.normalTexture.index,
                          m.normal,
                          AdobeTokens->rgb,
                          AdobeTokens->raw);
            importTextureTransform(gm.normalTexture.extensions, m.normal);
            // Note, while the normalScale usually works, the official usdchecker will flag
            // scale and bias that are not 2 and -1 for normal map texture readers
            // https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usdUtils/complianceChecker.py#L568
            m.normal.scale = GfVec4f(2 * gm.normalTexture.scale,
                                     2 * gm.normalTexture.scale,
                                     2 * gm.normalTexture.scale,
                                     1);
            m.normal.bias = GfVec4f(-1 * gm.normalTexture.scale,
                                    -1 * gm.normalTexture.scale,
                                    -1 * gm.normalTexture.scale,
                                    0);
        }
        if (gm.occlusionTexture.index >= 0) {
            int imageIndex = importImage(ctx, gm.occlusionTexture.index, m.name, "occlusion");
            importTexture(ctx.gltf,
                          imageIndex,
                          gm.occlusionTexture.index,
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
                    mesh.name.c_str());
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

void
importMeshes(ImportGltfContext& ctx)
{
    ctx.meshes.resize(ctx.gltf->meshes.size());
    for (size_t i = 0; i < ctx.gltf->meshes.size(); i++) {
        const tinygltf::Mesh& gmesh = ctx.gltf->meshes[i];
        ctx.meshes[i].resize(gmesh.primitives.size());
        for (size_t j = 0; j < gmesh.primitives.size(); j++) {
            const tinygltf::Primitive& primitive = gmesh.primitives[j];
            auto [meshIndex, mesh] = ctx.usd->addMesh();
            ctx.meshes[i][j] = meshIndex;
            mesh.name = gmesh.name;
            mesh.instanceable = true;
            int positionsIndex = getPrimitiveAttribute(primitive, "POSITION");
            int normalsIndex = getPrimitiveAttribute(primitive, "NORMAL");
            int tangentsIndex = getPrimitiveAttribute(primitive, "TANGENT");
            int uvsIndex = getPrimitiveAttribute(primitive, "TEXCOORD_0");

            int indicesIndex = primitive.indices;
            mesh.points =
              PXR_NS::VtArray<PXR_NS::GfVec3f>(getAccessorElementCount(*ctx.gltf, positionsIndex));
            mesh.normals.values =
              PXR_NS::VtArray<PXR_NS::GfVec3f>(getAccessorElementCount(*ctx.gltf, normalsIndex));
            mesh.tangents.values =
              PXR_NS::VtArray<PXR_NS::GfVec4f>(getAccessorElementCount(*ctx.gltf, tangentsIndex));
            mesh.uvs.values =
              PXR_NS::VtArray<PXR_NS::GfVec2f>(getAccessorElementCount(*ctx.gltf, uvsIndex));

            mesh.indices = PXR_NS::VtArray<int>(getAccessorElementCount(*ctx.gltf, indicesIndex));
            readAccessorDataToFloat(
              *ctx.gltf, positionsIndex, reinterpret_cast<float*>(mesh.points.data()));
            readAccessorDataToFloat(
              *ctx.gltf, normalsIndex, reinterpret_cast<float*>(mesh.normals.values.data()));
            readAccessorDataToFloat(
              *ctx.gltf, tangentsIndex, reinterpret_cast<float*>(mesh.tangents.values.data()));
            readAccessorDataToFloat(
              *ctx.gltf, uvsIndex, reinterpret_cast<float*>(mesh.uvs.values.data()));

            // Artifically create indices if none are found, assuming points define sequential
            // triangles
            if (indicesIndex >= 0) {
                mesh.faces = PXR_NS::VtArray<int>(mesh.indices.size() / 3, 3);
                readAccessorInts(*ctx.gltf, indicesIndex, mesh.indices);
            } else {
                mesh.indices.resize(mesh.points.size());
                mesh.faces = PXR_NS::VtArray<int>(mesh.indices.size() / 3, 3);
                for (size_t i = 0; i < mesh.indices.size(); i++) {
                    mesh.indices[i] = i;
                }
            }

            importMeshJointWeights(*ctx.gltf, primitive, mesh);

            mesh.normals.interpolation = UsdGeomTokens->vertex;
            mesh.tangents.interpolation = UsdGeomTokens->vertex;
            mesh.uvs.interpolation = UsdGeomTokens->vertex;

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
// n0/n1/n2...
// Then traverses all glTF skins and assembles skeleton data in the Usdata cache.
// This doesn't specify instantiation of any skeletons, which is done by importNodes.
// It's ok that importNodes runs before this one, because the skins and skeletons counts are equal.
void
importSkeletons(ImportGltfContext& ctx)
{
    ctx.skeletonNodeNames.resize(ctx.gltf->nodes.size(), "");
    std::function<void(int parentIndex, int nodeIndex)> buildSkeletonNodeNames;
    buildSkeletonNodeNames = [&](int parentIndex, int nodeIndex) {
        const tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];
        std::string name = "n" + std::to_string(nodeIndex);
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
    ctx.usd->skeletons.resize(ctx.gltf->skins.size());
    for (size_t i = 0; i < ctx.gltf->skins.size(); i++) {
        const tinygltf::Skin& skin = ctx.gltf->skins[i];
        Skeleton& skeleton = ctx.usd->skeletons[i];
        skeleton.name = skin.name;
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
importNodeAnimations(ImportGltfContext& ctx)
{
    if (ctx.gltf->animations.size() > 1) {
        TF_WARN("GTLF import currently only supports a single animation. "
                "Importing the first animation '%s'",
                ctx.gltf->animations[0].name.c_str());
    }

    bool hasAnimations = false;
    for (const tinygltf::Animation& animation : ctx.gltf->animations) {
        for (const tinygltf::AnimationChannel& channel : animation.channels) {
            const tinygltf::AnimationSampler& sampler = animation.samplers[channel.sampler];
            Node& node = ctx.usd->nodes[ctx.nodeMap[channel.target_node]];
            hasAnimations |= importChannel(*ctx.gltf,
                                           channel,
                                           sampler,
                                           "translation",
                                           node.translations,
                                           ctx.usd->minTime,
                                           ctx.usd->maxTime);
            hasAnimations |= importChannel(*ctx.gltf,
                                           channel,
                                           sampler,
                                           "rotation",
                                           node.rotations,
                                           ctx.usd->minTime,
                                           ctx.usd->maxTime);
            hasAnimations |= importChannel(*ctx.gltf,
                                           channel,
                                           sampler,
                                           "scale",
                                           node.scales,
                                           ctx.usd->minTime,
                                           ctx.usd->maxTime);
            if (channel.target_path == "weights") {
                TF_WARN("Unsupported import of GLTF blend weight animation");
            }
        }
        // XXX We only support a single animation at this point
        break;
    }
    ctx.usd->hasAnimations = hasAnimations;
}

void
importAnimations(ImportGltfContext& ctx)
{
    if (ctx.gltf->skins.size() <= 0)
        return;

    for (size_t i = 0; i < ctx.gltf->animations.size(); i++) {
        const tinygltf::Animation& animation = ctx.gltf->animations[i];

        // Select those animated nodes that correspond to skeleton nodes
        std::unordered_set<int> nodeSet;
        std::vector<int> animNodes;
        for (const tinygltf::AnimationChannel& channel : animation.channels) {
            if (!ctx.usd->nodes[ctx.nodeMap[channel.target_node]].isJoint) {
                const tinygltf::Node& node = ctx.gltf->nodes[channel.target_node];
                TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                             "Found non skeleton node %d %s\n",
                             channel.target_node,
                             node.name.c_str());
                continue;
            }
            nodeSet.insert(channel.target_node);
        }
        animNodes.assign(nodeSet.begin(), nodeSet.end());

        // Bind animated nodes to skeletons
        for (size_t j = 0; j < ctx.gltf->skins.size(); j++) {
            const tinygltf::Skin& skin = ctx.gltf->skins[j];
            for (size_t q = 0; q < skin.joints.size(); q++) {
                const auto& it = std::find(animNodes.begin(), animNodes.end(), skin.joints[q]);
                if (it != animNodes.end()) {
                    ctx.usd->skeletons[j].animations.push_back(i);
                    break;
                }
            }
        }

        // Build a definitive time scale by inserting time points from every times array.
        // TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Assembling animation time");
        std::vector<float> definitiveTimes;
        for (int animNode : animNodes) {
            const Node& node = ctx.usd->nodes[ctx.nodeMap[animNode]];
            addToTimeMap(definitiveTimes, node.rotations.times);
            addToTimeMap(definitiveTimes, node.translations.times);
            addToTimeMap(definitiveTimes, node.scales.times);
        }
        // TODO: when implementing weights animation, might be able to remove this guard
        if (definitiveTimes.size() <= 0) {
            TF_DEBUG_MSG(
              FILE_FORMAT_GLTF, "Animation %lu %s has no times\n", i, animation.name.c_str());
            continue;
        }
        ctx.usd->hasAnimations = true;
        ctx.usd->minTime = std::min(ctx.usd->minTime, definitiveTimes[0]);
        ctx.usd->maxTime = std::max(ctx.usd->maxTime, definitiveTimes[definitiveTimes.size() - 1]);

        // Interpolate animated values along the definitive time points
        // TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Interpolating animation");
        std::vector<PXR_NS::VtArray<PXR_NS::GfQuatf>> definitiveRotations(
          animNodes.size(),
          PXR_NS::VtArray<PXR_NS::GfQuatf>(definitiveTimes.size(), PXR_NS::GfQuatf(0)));
        std::vector<PXR_NS::VtArray<PXR_NS::GfVec3f>> definitiveTranslations(
          animNodes.size(),
          PXR_NS::VtArray<PXR_NS::GfVec3f>(definitiveTimes.size(), PXR_NS::GfVec3f(0)));
        std::vector<PXR_NS::VtArray<PXR_NS::GfVec3f>> definitiveScales(
          animNodes.size(),
          PXR_NS::VtArray<PXR_NS::GfVec3f>(definitiveTimes.size(), PXR_NS::GfVec3f(1)));
        for (size_t j = 0; j < animNodes.size(); j++) {
            const Node& n = ctx.usd->nodes[ctx.nodeMap[animNodes[j]]];
            const tinygltf::Node& node = ctx.gltf->nodes[animNodes[j]];
            if (n.rotations.values.size() > 1) {
                interpolateData<PXR_NS::GfQuatf>(
                  definitiveTimes, n.rotations.times, n.rotations.values, definitiveRotations[j]);
            } else {
                PXR_NS::GfQuatf restRotation =
                  node.rotation.size()
                    ? PXR_NS::GfQuatf(
                        node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])
                    : PXR_NS::GfQuatf(0);
                definitiveRotations[j].assign(definitiveTimes.size(), restRotation);
            }
            if (n.translations.values.size() > 1) {
                interpolateData<PXR_NS::GfVec3f>(definitiveTimes,
                                                 n.translations.times,
                                                 n.translations.values,
                                                 definitiveTranslations[j]);
            } else {
                PXR_NS::GfVec3f restTranslation =
                  node.translation.size()
                    ? PXR_NS::GfVec3f(node.translation[0], node.translation[1], node.translation[2])
                    : PXR_NS::GfVec3f(0);
                definitiveTranslations[j].assign(definitiveTimes.size(), restTranslation);
            }
            if (n.scales.values.size() > 1) {
                interpolateData<PXR_NS::GfVec3f>(
                  definitiveTimes, n.scales.times, n.scales.values, definitiveScales[j]);
            } else {
                PXR_NS::GfVec3f restScale =
                  node.scale.size() ? PXR_NS::GfVec3f(node.scale[0], node.scale[1], node.scale[2])
                                    : PXR_NS::GfVec3f(1);
                definitiveScales[j].assign(definitiveTimes.size(), restScale);
            }
        }

        // Assemble animated values at time slices
        // TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Assembling animation %d %s - %zu times\n",
        //              i, animation.name.c_str(), definitiveTimes.size());
        // Add animations one at a time, since we have an early out at the bottom of this loop
        ctx.usd->animations.push_back(Animation());
        Animation& anim = ctx.usd->animations.back();
        anim.name = animation.name;
        anim.joints.resize(animNodes.size());
        anim.times.resize(definitiveTimes.size());
        anim.rotations.resize(definitiveTimes.size(),
                              PXR_NS::VtArray<PXR_NS::GfQuatf>(animNodes.size()));
        anim.translations.resize(definitiveTimes.size(),
                                 PXR_NS::VtArray<PXR_NS::GfVec3f>(animNodes.size()));
        anim.scales.resize(definitiveTimes.size(),
                           PXR_NS::VtArray<PXR_NS::GfVec3h>(animNodes.size()));
        for (size_t j = 0; j < animNodes.size(); j++) {
            std::string name = ctx.skeletonNodeNames[animNodes[j]];
            anim.joints[j] = PXR_NS::TfToken(name);
        }
        for (size_t j = 0; j < definitiveTimes.size(); j++) {
            // TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Time[" << k << "] = " << definitiveTimes[j]);
            anim.times[j] = definitiveTimes[j];
            for (size_t k = 0; k < animNodes.size(); k++) {
                anim.rotations[j][k] = definitiveRotations[k][j];
                anim.translations[j][k] = PXR_NS::GfVec3f(definitiveTranslations[k][j]);
                anim.scales[j][k] = PXR_NS::GfVec3h(definitiveScales[k][j]);
            }
        }
        // XXX We only support a single animation at this point
        break;
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

// Import nodes from gltf.
// We need to store nodes from parent to children in UsdData.
// Therefore we traverse from root nodes recursively, and write nodes
// in the UsdData nodes array with the incrementing index k.
// But we keep a record of the new node mapping in `nodeMap` for reference.
bool
importNodes(ImportGltfContext& ctx)
{
    int k = 0;
    ctx.nodeMap.resize(ctx.gltf->nodes.size());
    ctx.usd->nodes.resize(ctx.gltf->nodes.size());
    std::function<int(int parentIndex, int nodeIndex)> traverse;
    traverse = [&](int parentIndex, int nodeIndex) -> int {
        int usdParentIndex = parentIndex != -1 ? ctx.nodeMap[parentIndex] : -1;
        const tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];
        int usdNodeIndex = k++;
        Node& n = ctx.usd->nodes[usdNodeIndex];
        ctx.nodeMap[nodeIndex] = usdNodeIndex;
        n.name = node.name;
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
        n.parent = usdParentIndex;
        if (node.mesh >= 0) {
            if (node.skin >= 0) {
                // We make a clear distinction when we add regular or skeletal meshes.
                // Because although we encountered a skin and mesh in this node,
                // we want instantiation of the skeletal mesh to live under the
                // parent (if it exists) or the node itself. The motivation is that the
                // glTF format generally wants meshes to live at the root. However, USD
                // prefers placing the mesh next to the skeleton under a SkelRoot prim.
                // XXX There is an outstanding issue with proper parenting of meshes and
                // skeletons which needs to be resolved.
                int skinRootNodeIndex = parentIndex != -1 ? parentIndex : nodeIndex;
                auto& meshList = ctx.usd->nodes[skinRootNodeIndex].skinnedMeshes[node.skin];
                for (auto m : ctx.meshes[node.mesh]) {
                    if (std::find(meshList.begin(), meshList.end(), m) == meshList.end()) {
                        meshList.push_back(m);
                    }
                }
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

    for (const tinygltf::Scene& scene : ctx.gltf->scenes) {
        for (int rootNodeIndex : scene.nodes) {
            int usdNodeIndex = traverse(-1, rootNodeIndex);
            ctx.usd->rootNodes.push_back(usdNodeIndex);
        }
    }
    return true;
}

static const std::set<std::string> supportedExtension = {
    // Ratified extensions
    "KHR_draco_mesh_compression",
    // "KHR_lights_punctual",
    "KHR_materials_anisotropy",
    "KHR_materials_clearcoat",
    "KHR_materials_emissive_strength",
    "KHR_materials_ior",
    // "KHR_materials_iridescence",
    "KHR_materials_sheen",
    "KHR_materials_specular",
    "KHR_materials_transmission",
    // "KHR_materials_unlit",
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
importGltf(const ImportGltfOptions& options, tinygltf::Model& model, UsdData& usd, const std::string& filename)
{
    checkExtensions(model.extensionsUsed, model.extensionsRequired);

    ImportGltfContext ctx;

    // Add filename of imported file and any paths to external buffers
    // to the list of filenames which will be used as metadata
    std::string baseName = TfGetBaseName(filename);
    ctx.filenames.push_back(baseName);
    for(auto buffer : model.buffers) {
        // Filter out uris which are data references (ie the uri starts with "data:")
        if(!buffer.uri.empty() && buffer.uri.compare(0, 5, "data:", 5) != 0) {
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

    importMetadata(ctx);
    importCameras(ctx);

    if (options.importMaterials) {
        importMaterials(ctx);
    }
    if (options.importGeometry) {
        importMeshes(ctx);
        importNodes(ctx);
        importSkeletons(ctx);
        importNodeAnimations(ctx);
        importAnimations(ctx);
    }

    usd.metadata.SetValueAtPath("filenames", VtValue(ctx.filenames));
    return true;
}

}
