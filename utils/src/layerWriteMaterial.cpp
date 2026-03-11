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
#include <fileformatutils/layerWriteMaterial.h>

#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/sdfMaterialUtils.h>
#include <fileformatutils/sdfUtils.h>

#include <pxr/usd/usdShade/tokens.h>

#include <unordered_map>

using namespace PXR_NS;

namespace adobe::usd {

// The fallback value, if valid, must be a Float4 value. So we do the necessary conversion from
// other expected types
VtValue
_createFallbackValue(const VtValue& value)
{
    if (value.IsEmpty()) {
        return value;
    } else if (value.IsHolding<float>()) {
        float v = value.UncheckedGet<float>();
        return VtValue(GfVec4f(v));
    } else if (value.IsHolding<GfVec2f>()) {
        const GfVec2f& v = value.UncheckedGet<GfVec2f>();
        return VtValue(GfVec4f(v[0], v[1], 0.0f, 1.0f));
    } else if (value.IsHolding<GfVec3f>()) {
        const GfVec3f& v = value.UncheckedGet<GfVec3f>();
        return VtValue(GfVec4f(v[0], v[1], v[2], 1.0f));
    } else if (value.IsHolding<GfVec4f>()) {
        return value;
    } else {
        TF_WARN("VtValue of unsupported type %s for fallback value", value.GetTypeName().c_str());
        return VtValue();
    }
}

// Convert a TfToken to a VtValue, but keep the VtValue empty if the TfToken was empty
VtValue
_checkToken(const TfToken& token)
{
    return token.IsEmpty() ? VtValue() : VtValue(token);
}

SdfPath
_createStReader(SdfAbstractData* sdfData, const SdfPath& parentPath, int uvIndex)
{
    return createShader(sdfData,
                        parentPath,
                        getSTTexCoordReaderToken(uvIndex),
                        AdobeTokens->UsdPrimvarReader_float2,
                        "result",
                        { { "varname", getSTPrimvarAttrToken(uvIndex).GetString() } },
                        {});
}

// If a texture coordinate transform is needed for the given input a transform will be created and
// the result output path will be returned. Otherwise it will forward the default ST reader result
// path.
SdfPath
_createStTransform(SdfAbstractData* sdfData,
                   const SdfPath& parentPath,
                   const std::string& name,
                   const Input& input,
                   const SdfPath& stReaderResultPath)
{
    if (input.hasDefaultTransform()) {
        return stReaderResultPath;
    }

    return createShader(sdfData,
                        parentPath,
                        TfToken(name + "_stTransform"),
                        AdobeTokens->UsdTransform2d,
                        "result",
                        { { "rotation", input.uvRotation },
                          { "scale", input.uvScale },
                          { "translation", input.uvTranslation } },
                        { { "in", stReaderResultPath } });
}

SdfPath
_createTextureReader(SdfAbstractData* sdfData,
                     const SdfPath& parentPath,
                     const TfToken& name,
                     const Input& input,
                     const SdfPath& stResultPath,
                     const std::string& texturePath,
                     const SdfPath& textureConnection)
{
    // Note, we're setting the texture path directly on this texture reader, which means the
    // path is duplicated on each texture reader of the same texture for each of the different
    // sub networks. This is currently needed since some software is not correctly following
    // connections to resolve input values.
    // Once that has improved in the ecosystem we could author the asset path once as an
    // attribute on the material and connect all corresponding texture readers to that attribute
    // value.

    // Only emit scale and bias if they are not the default values. Empty values for
    // scale/bias will be ignored
    VtValue scale, bias;
    if (input.scale != kDefaultTexScale) {
        scale = input.scale;
    }
    if (input.bias != kDefaultTexBias) {
        bias = input.bias;
    }

    InputValues inputValues = { { "fallback", _createFallbackValue(input.value) },
                                { "sourceColorSpace", _checkToken(input.colorspace) },
                                { "wrapS", _checkToken(input.wrapS) },
                                { "wrapT", _checkToken(input.wrapT) },
                                { "minFilter", _checkToken(input.minFilter) },
                                { "magFilter", _checkToken(input.magFilter) },
                                { "scale", scale },
                                { "bias", bias },
                                { "file", VtValue(SdfAssetPath(texturePath)) } };

    InputConnections inputConnections = { { "st", stResultPath }, { "file", textureConnection } };

    return createShader(sdfData,
                        parentPath,
                        name,
                        AdobeTokens->UsdUVTexture,
                        input.channel.GetString(),
                        inputValues,
                        inputConnections);
}

void
_setupInput(WriteSdfContext& ctx,
            const SdfPath& materialPath,
            const SdfPath& parentPath,
            const TfToken& name,
            const Input& input,
            std::unordered_map<int, SdfPath>& stReaderResultPathMap,
            InputValues& inputValues,
            InputConnections& inputConnections,
            const InputToMaterialInputTypeMap& inputRemapping,
            MaterialInputs& materialInputs)
{
    auto remappingIt = inputRemapping.find(name);
    bool hasMapping = remappingIt != inputRemapping.cend();
    if (!hasMapping) {
        TF_CODING_ERROR("Expecting to find remapping for shader input '%s'", name.GetText());
        return;
    }
    const TfToken& materialInputName = remappingIt->second.name;
    const SdfValueTypeName& inputType = remappingIt->second.type;

    if (input.image >= 0) {
        if (input.isZeroTexture()) {
            inputValues.emplace_back(name.GetString(), getTextureZeroVtValue(input.channel));
        } else if ((size_t)input.image >= ctx.usdData->images.size()) {
            TF_CODING_ERROR("Image index %d for %s is larger than images array %zu",
                            input.image,
                            name.GetText(),
                            ctx.usdData->images.size());
            return;
        } else {
            std::string texturePath =
              createTexturePath(ctx.srcAssetFilename, ctx.usdData->images[input.image].uri);
            // For UsdPreviewSurface we can detect color inputs with the type directly. For the ASM
            // shader we need to both check the type to be Float3 and also to have "Color" or
            // "emissive" in the name to differentiate from the normal inputs, which are also
            // Float3.
            const std::string& inputName = materialInputName.GetString();
            bool isColorTexture = inputType == SdfValueTypeNames->Color3f ||
                                  (inputType == SdfValueTypeNames->Float3 &&
                                   (inputName.find("Color") != std::string::npos ||
                                    inputName.find("emissive") != std::string::npos));
            SdfPath textureConnection = addMaterialInputTexture(ctx.sdfData,
                                                                materialPath,
                                                                materialInputName,
                                                                texturePath,
                                                                isColorTexture,
                                                                materialInputs);

            // Create the ST reader on demand when we create the first textured input
            SdfPath stReaderResultPath;
            auto it = stReaderResultPathMap.find(input.uvIndex);
            if (it == stReaderResultPathMap.end()) {
                stReaderResultPath = _createStReader(ctx.sdfData, parentPath, input.uvIndex);
                stReaderResultPathMap[input.uvIndex] = stReaderResultPath;
            } else {
                stReaderResultPath = it->second;
            }

            // This creates a ST transform node if needed, otherwise the default ST
            // result path will be returned.
            SdfPath stResultPath = _createStTransform(
              ctx.sdfData, parentPath, name.GetString(), input, stReaderResultPath);

            SdfPath texResultPath = _createTextureReader(
              ctx.sdfData, parentPath, name, input, stResultPath, texturePath, textureConnection);

            inputConnections.emplace_back(name.GetString(), texResultPath);
        }
    } else if (!input.value.IsEmpty()) {
        SdfPath connection = addMaterialInputValue(
          ctx.sdfData, materialPath, materialInputName, inputType, input.value, materialInputs);
        inputConnections.emplace_back(name.GetString(), connection);
        const MinMaxVtValuePair* range =
          ShaderRegistry::getInstance().getMaterialInputRange(materialInputName);
        if (range)
            setRangeMetadata(ctx.sdfData, connection, *range);
    }
}

Input
_createEmissiveColorInput(const OpenPbrMaterial& material)
{
    if (material.emission_luminance.isEmpty() || material.emission_color.isZeroInput()) {
        return {};
    }

    // Note, we do not handle the case where emission_luminance is driven by a texture
    float luminanceValue = 0.0f;
    if (material.emission_luminance.image >= 0) {
        TF_WARN("emission_luminance driven by a texture. Can't be folded into emissiveColor.");
        luminanceValue = 1.0f;
    } else if (material.emission_luminance.value.IsHolding<float>()) {
        luminanceValue = material.emission_luminance.value.UncheckedGet<float>();
        // Multiply by a factor to convert OpenPBR's emission_luminance.
        // Without this, emission might be very blown out.
        luminanceValue *= kOpenPbrToAsmEmissionFactor;
    }

    Input emissiveColor = material.emission_color;
    if (emissiveColor.isEmpty()) {
        emissiveColor.value = GfVec3f(luminanceValue, luminanceValue, luminanceValue);
    } else if (emissiveColor.image == -1) {
        // Fold the luminance multiplier into the constant color value
        GfVec3f colorValue = GfVec3f(1.0f);
        if (emissiveColor.value.IsHolding<GfVec3f>()) {
            colorValue = emissiveColor.value.UncheckedGet<GfVec3f>();
        }
        emissiveColor.value = luminanceValue * colorValue;
    } else {
        // Fold the luminance multiplier into the texture scale
        emissiveColor.scale *= luminanceValue;
    }

    return emissiveColor;
}

void
writeUsdPreviewSurface(WriteSdfContext& ctx,
                       const SdfPath& materialPath,
                       const OpenPbrMaterial& material,
                       MaterialInputs& materialInputs)
{
    SdfPath p;

    // This will create a NodeGraph parent prim for all the shading nodes in this network
    SdfPath parentPath = createPrimSpec(
      ctx.sdfData, materialPath, AdobeTokens->UsdPreviewSurface, UsdShadeTokens->NodeGraph);

    TF_DEBUG_MSG(
      FILE_FORMAT_UTIL, "layer::write UsdPreviewSurface network %s\n", parentPath.GetText());

    InputValues inputValues;
    InputConnections inputConnections;
    std::unordered_map<int, SdfPath> stReaderResultPathMap;
    const InputToMaterialInputTypeMap& remapping =
      ShaderRegistry::getInstance().getUsdPreviewSurfaceInputRemapping();
    auto writeInput = [&](const TfToken& name, const Input& input) {
        if (!input.isEmpty()) {
            _setupInput(ctx,
                        materialPath,
                        parentPath,
                        name,
                        input,
                        stReaderResultPathMap,
                        inputValues,
                        inputConnections,
                        remapping,
                        materialInputs);
        }
    };

    writeInput(UsdPreviewSurfaceTokens->diffuseColor, material.base_color);
    writeInput(UsdPreviewSurfaceTokens->emissiveColor, _createEmissiveColorInput(material));
    if (material.useSpecularWorkflow) {
        writeInput(UsdPreviewSurfaceTokens->useSpecularWorkflow, Input{ VtValue(1) });
    }
    writeInput(UsdPreviewSurfaceTokens->specularColor, material.specular_color);
    writeInput(UsdPreviewSurfaceTokens->metallic, material.base_metalness);
    writeInput(UsdPreviewSurfaceTokens->roughness, material.specular_roughness);
    writeInput(UsdPreviewSurfaceTokens->clearcoat, material.coat_weight);
    writeInput(UsdPreviewSurfaceTokens->clearcoatRoughness, material.coat_roughness);
    writeInput(UsdPreviewSurfaceTokens->opacity, material.geometry_opacity);
    // UsdPreviewSurfaceTokens->opacityMode (no source data)
    if (material.opacityThreshold > 0.0f) {
        writeInput(UsdPreviewSurfaceTokens->opacityThreshold,
                   Input{ VtValue(material.opacityThreshold) });
    }
    writeInput(UsdPreviewSurfaceTokens->ior, material.specular_ior);
    writeInput(UsdPreviewSurfaceTokens->normal, material.geometry_normal);
    writeInput(UsdPreviewSurfaceTokens->displacement, material.displacement);
    writeInput(UsdPreviewSurfaceTokens->occlusion, material.occlusion);
    // If we don't have opacity, but we do have transmission, we wire it into opacity
    if (material.geometry_opacity.isEmpty() && !material.transmission_weight.isEmpty()) {
        writeInput(UsdPreviewSurfaceTokens->opacity, invertInput(material.transmission_weight));
    }

    // Create UsdPreviewSurface shader
    auto outputPaths = createShader(ctx.sdfData,
                                    parentPath,
                                    AdobeTokens->UsdPreviewSurface,
                                    AdobeTokens->UsdPreviewSurface,
                                    StringVector{ "surface", "displacement" },
                                    inputValues,
                                    inputConnections);

    if (outputPaths.size() < 1) {
        TF_WARN("Failed to create surface shader output: No output paths available.");
    } else {
        createShaderOutput(
          ctx.sdfData, materialPath, "surface", SdfValueTypeNames->Token, outputPaths[0]);
    }
    if (outputPaths.size() < 2) {
        TF_WARN(
          "Failed to create displacement shader output: Insufficient output paths available.");
    } else {
        createShaderOutput(
          ctx.sdfData, materialPath, "displacement", SdfValueTypeNames->Token, outputPaths[1]);
    }
}

Input
_createEmissiveIntensityInput(const Input& emission_luminance)
{
    if (emission_luminance.isEmpty()) {
        return {};
    }

    // Note, we do not handle the case where emission_luminance is driven by a texture
    float luminanceValue = 0.0f;
    if (emission_luminance.image >= 0) {
        TF_WARN("emission_luminance driven by a texture. Can't be folded into emissiveColor.");
        luminanceValue = 1.0f;
    } else if (emission_luminance.value.IsHolding<float>()) {
        luminanceValue = emission_luminance.value.UncheckedGet<float>();
        // Multiply by a factor to convert OpenPBR's emission_luminance.
        // Without this, emission might be very blown out.
        luminanceValue *= kOpenPbrToAsmEmissionFactor;
    }

    return Input{ VtValue(luminanceValue) };
}

void
writeAsmMaterial(WriteSdfContext& ctx,
                 const SdfPath& materialPath,
                 const OpenPbrMaterial& material,
                 MaterialInputs& materialInputs)
{
    SdfPath p;

    // This will create a NodeGraph parent prim for all the shading nodes in this network
    SdfPath parentPath =
      createPrimSpec(ctx.sdfData, materialPath, AdobeTokens->ASM, UsdShadeTokens->NodeGraph);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "layer::write ASM network %s\n", parentPath.GetText());

    InputValues inputValues;
    InputConnections inputConnections;
    std::unordered_map<int, SdfPath> stReaderResultPathMap;
    const InputToMaterialInputTypeMap& remapping =
      ShaderRegistry::getInstance().getAsmInputRemapping();
    auto writeInput = [&](const TfToken& name, const Input& input) {
        if (!input.isEmpty()) {
            _setupInput(ctx,
                        materialPath,
                        parentPath,
                        name,
                        input,
                        stReaderResultPathMap,
                        inputValues,
                        inputConnections,
                        remapping,
                        materialInputs);
        }
    };

    writeInput(AsmTokens->baseColor, material.base_color);
    writeInput(AsmTokens->roughness, material.specular_roughness);
    writeInput(AsmTokens->metallic, material.base_metalness);
    writeInput(AsmTokens->opacity, material.geometry_opacity);
    writeInput(AsmTokens->specularLevel, material.specular_weight);
    writeInput(AsmTokens->specularEdgeColor, material.specular_color);
    writeInput(AsmTokens->normal, material.geometry_normal);
    if (material.normalScale != 1.0f) {
        writeInput(AsmTokens->normalScale, Input{ VtValue(material.normalScale) });
    }

    // combineNormalAndHeight = false (flag) (no source info)

    writeInput(AsmTokens->height, material.displacement);
    // heightScale (no source info)
    // heightLevel (no source info)
    writeInput(AsmTokens->anisotropyLevel, material.specular_roughness_anisotropy);
    // Note, this is just a pass through. OpenPBR does not support an anisotropy angle input
    writeInput(AsmTokens->anisotropyAngle, material.anisotropyAngle);
    writeInput(AsmTokens->emissiveIntensity,
               _createEmissiveIntensityInput(material.emission_luminance));
    writeInput(AsmTokens->emissive, material.emission_color);
    writeInput(AsmTokens->sheenOpacity, material.fuzz_weight);
    writeInput(AsmTokens->sheenColor, material.fuzz_color);
    writeInput(AsmTokens->sheenRoughness, material.fuzz_roughness);
    writeInput(AsmTokens->translucency, material.transmission_weight);
    writeInput(AsmTokens->IOR, material.specular_ior);
    // XXX This is only correct when transmission_dispersion_abbe_number is at the default of 20
    writeInput(AsmTokens->dispersion, material.transmission_dispersion_scale);
    writeInput(AsmTokens->absorptionColor, material.transmission_color);
    writeInput(AsmTokens->absorptionDistance, material.transmission_depth);
    // XXX subsurface_weight could be a textured floating point value. We currently don't have a
    // way to express that with ASM
    if (!material.subsurface_weight.isEmpty()) {
        inputValues.emplace_back("scatter", true);
    }
    writeInput(AsmTokens->scatteringColor, material.subsurface_color);
    writeInput(AsmTokens->scatteringDistance, material.subsurface_radius);
    // XXX a precise value conversion is rather complicated
    writeInput(AsmTokens->scatteringDistanceScale, material.subsurface_radius_scale);
    // scatteringRedShift (no source info)
    // scatteringRayleigh (no source info)
    writeInput(AsmTokens->coatOpacity, material.coat_weight);
    writeInput(AsmTokens->coatColor, material.coat_color);
    writeInput(AsmTokens->coatRoughness, material.coat_roughness);
    writeInput(AsmTokens->coatIOR, material.coat_ior);
    // Note, this is just a pass through. OpenPBR does not support a coatSpecularLevel input
    writeInput(AsmTokens->coatSpecularLevel, material.coatSpecularLevel);
    writeInput(AsmTokens->coatNormal, material.geometry_coat_normal);
    // coatNormalScale (the scale is part of the coatNormal `scale` or `value`)

    writeInput(AsmTokens->ambientOcclusion, material.occlusion);
    // Note, this is just a pass through. OpenPBR does not support a volumeThickness input
    writeInput(AsmTokens->volumeThickness, material.volumeThickness);
    // volumeThicknessScale (the scale is part of the volumeThickness `scale` or `value`)

    // Create Adobe Standard Material shader
    SdfPath outputPath = createShader(ctx.sdfData,
                                      parentPath,
                                      AdobeTokens->ASM,
                                      AdobeTokens->adobeStandardMaterial,
                                      "surface",
                                      inputValues,
                                      inputConnections);
    createShaderOutput(
      ctx.sdfData, materialPath, "adobe:surface", SdfValueTypeNames->Token, outputPath);

    SdfPath surfaceShaderPath = parentPath.AppendChild(AdobeTokens->ASM);
    createExtraConstantAttribute(ctx.sdfData, material, surfaceShaderPath);
}
}
