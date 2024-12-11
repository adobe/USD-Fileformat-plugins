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

SdfPath
_createStReader(SdfAbstractData* sdfData, const SdfPath& parentPath, int uvIndex)
{
    return createShader(sdfData,
                        parentPath,
                        getSTTexCoordReaderToken(uvIndex),
                        AdobeTokens->UsdPrimvarReader_float2,
                        "result",
                        { { "varname", getSTPrimvarAttrToken(uvIndex) } },
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
    if (input.transformRotation.IsEmpty() && input.transformScale.IsEmpty() &&
        input.transformTranslation.IsEmpty()) {
        return stReaderResultPath;
    }

    return createShader(sdfData,
                        parentPath,
                        TfToken(name + "_stTransform"),
                        AdobeTokens->UsdTransform2d,
                        "result",
                        { { "rotation", input.transformRotation },
                          { "scale", input.transformScale },
                          { "translation", input.transformTranslation } },
                        { { "in", stReaderResultPath } });
}

SdfPath
_createTextureReader(SdfAbstractData* sdfData,
                     const SdfPath& parentPath,
                     const TfToken& name,
                     const Input& input,
                     const SdfPath& stResultPath,
                     const SdfPath& textureConnection)
{
    // Note, we're setting the texture path directly on this texture reader, which means the
    // path is duplicated on each texture reader of the same texture for each of the different
    // sub networks. This is currently needed since some software is not correctly following
    // connections to resolve input values.
    // Once that has improved in the ecosystem we could author the asset path once as an
    // attribute on the material and connect all corresponding texture readers to that attribute
    // value.

    // Make sure the colorSpace is an empty VtValue if the TfToken for colorspace is empty
    VtValue colorSpace = input.colorspace.IsEmpty() ? VtValue() : VtValue(input.colorspace);

    InputValues inputValues = { { "fallback", _createFallbackValue(input.value) },
                                { "sourceColorSpace", colorSpace },
                                { "wrapS", input.wrapS },
                                { "wrapT", input.wrapT },
                                { "minFilter", input.minFilter },
                                { "magFilter", input.magFilter },
                                { "scale", input.scale },
                                { "bias", input.bias } };
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
            SdfPath textureConnection = addMaterialInputTexture(
              ctx.sdfData, materialPath, materialInputName, texturePath, materialInputs);

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
              ctx.sdfData, parentPath, name, input, stResultPath, textureConnection);

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

void
writeUsdPreviewSurface(WriteSdfContext& ctx,
                       const SdfPath& materialPath,
                       const Material& material,
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
        if (!input.isEmpty())
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
    };

    writeInput(AdobeTokens->useSpecularWorkflow, material.useSpecularWorkflow);
    writeInput(AdobeTokens->diffuseColor, material.diffuseColor);
    writeInput(AdobeTokens->emissiveColor, material.emissiveColor);
    writeInput(AdobeTokens->specularColor, material.specularColor);
    writeInput(AdobeTokens->normal, material.normal);
    writeInput(AdobeTokens->metallic, material.metallic);
    writeInput(AdobeTokens->roughness, material.roughness);
    writeInput(AdobeTokens->clearcoat, material.clearcoat);
    writeInput(AdobeTokens->clearcoatRoughness, material.clearcoatRoughness);
    writeInput(AdobeTokens->opacity, material.opacity);
    writeInput(AdobeTokens->opacityThreshold, material.opacityThreshold);
    writeInput(AdobeTokens->displacement, material.displacement);
    writeInput(AdobeTokens->occlusion, material.occlusion);
    writeInput(AdobeTokens->ior, material.ior);
    // If we don't have opacity, but we do have transmission, we wire it into opacity
    if (material.opacity.isEmpty() && !material.transmission.isEmpty()) {
        writeInput(AdobeTokens->opacity, invertInput(material.transmission));
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

void
writeAsmMaterial(WriteSdfContext& ctx,
                 const SdfPath& materialPath,
                 const Material& material,
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
    };

    // Currently unused inputs
    // Input useSpecularWorkflow;

    writeInput(AdobeTokens->baseColor, material.diffuseColor);
    writeInput(AdobeTokens->roughness, material.roughness);
    writeInput(AdobeTokens->metallic, material.metallic);
    writeInput(AdobeTokens->opacity, material.opacity);

    // Note, ASM does not support an opacityThreshold. But without storing it here, the
    // information is lost and can't be round tripped. So we store it, even though we know it
    // won't affect the result of the material
    writeInput(AdobeTokens->opacityThreshold, material.opacityThreshold);
    writeInput(AdobeTokens->specularLevel, material.specularLevel);
    // XXX should this be gated by material.useSpecularWorkflow?
    writeInput(AdobeTokens->specularEdgeColor, material.specularColor);
    writeInput(AdobeTokens->normal, material.normal);
    writeInput(AdobeTokens->normalScale, material.normalScale);
    // combineNormalAndHeight = false (flag) (no source info)
    writeInput(AdobeTokens->height, material.displacement);
    // heightScale (no source info)
    // heightLevel (no source info)
    writeInput(AdobeTokens->anisotropyLevel, material.anisotropyLevel);
    writeInput(AdobeTokens->anisotropyAngle, material.anisotropyAngle);

    // Turn on emission if we have a valid input
    if (!material.emissiveColor.isEmpty()) {
        // The intensity is part of the emissive `scale` or `value` of the emissiveColor input
        inputValues.emplace_back("emissiveIntensity", 1.0f);
    }
    writeInput(AdobeTokens->emissive, material.emissiveColor);
    if (!material.sheenColor.isEmpty()) {
        // XXX We currently turn the sheen fully on if the asset has a sheen color specified
        inputValues.emplace_back("sheenOpacity", 1.0f);
    }
    writeInput(AdobeTokens->sheenColor, material.sheenColor);
    writeInput(AdobeTokens->sheenRoughness, material.sheenRoughness);
    writeInput(AdobeTokens->translucency, material.transmission);
    writeInput(AdobeTokens->IOR, material.ior);
    // dispersion (no source info)
    writeInput(AdobeTokens->absorptionColor, material.absorptionColor);
    writeInput(AdobeTokens->absorptionDistance, material.absorptionDistance);
    if (!material.scatteringColor.isEmpty() || !material.scatteringDistance.isEmpty()) {
        inputValues.emplace_back("scatter", true);
    }
    writeInput(AdobeTokens->scatteringColor, material.scatteringColor);
    writeInput(AdobeTokens->scatteringDistance, material.scatteringDistance);
    // scatteringDistanceScale (the scale is part of the scatteringDistance `scale` or `value`)
    // scatteringRedShift (no source info)
    // scatteringRayleigh (no source info)
    writeInput(AdobeTokens->coatOpacity, material.clearcoat);
    writeInput(AdobeTokens->coatColor, material.clearcoatColor);
    writeInput(AdobeTokens->coatRoughness, material.clearcoatRoughness);
    writeInput(AdobeTokens->coatIOR, material.clearcoatIor);
    writeInput(AdobeTokens->coatSpecularLevel, material.clearcoatSpecular);
    writeInput(AdobeTokens->coatNormal, material.clearcoatNormal);
    // coatNormalScale (the scale is part of the coatNormal `scale` or `value`)
    writeInput(AdobeTokens->ambientOcclusion, material.occlusion);
    writeInput(AdobeTokens->volumeThickness, material.volumeThickness);
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

    if (material.isUnlit) {
        SdfPath p = createAttributeSpec(ctx.sdfData,
                                        parentPath.AppendChild(AdobeTokens->ASM),
                                        AdobeTokens->unlit,
                                        SdfValueTypeNames->Bool);
        setAttributeMetadata(ctx.sdfData, p, SdfFieldKeys->Custom, VtValue(true));
        setAttributeDefaultValue(ctx.sdfData, p, true);
    }

    if (material.clearcoatModelsTransmissionTint) {
        // Author a custom attribute to leave an indicator where the clearcoat came from
        SdfPath p = createAttributeSpec(ctx.sdfData,
                                        parentPath.AppendChild(AdobeTokens->ASM),
                                        AdobeTokens->clearcoatModelsTransmissionTint,
                                        SdfValueTypeNames->Bool);
        setAttributeMetadata(ctx.sdfData, p, SdfFieldKeys->Custom, VtValue(true));
        setAttributeDefaultValue(ctx.sdfData, p, true);
    }
}
}
