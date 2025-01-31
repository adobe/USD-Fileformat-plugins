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
#include "sbsarAsm.h"
#include "usdGenerationHelpers.h"
#include <sbsarDebug.h>

// File format utils
#include <fileformatutils/common.h>
#include <fileformatutils/sdfMaterialUtils.h>
#include <fileformatutils/sdfUtils.h>

#include <pxr/usd/usdShade/tokens.h>

using namespace SubstanceAir;
PXR_NAMESPACE_USING_DIRECTIVE

namespace {

using namespace adobe::usd;
using namespace adobe::usd::sbsar;

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (TexCoordReader)
    (UvTransform)
    (AdobeStandardMaterial)
);
// clang-format on

struct BindInfo
{
    std::string name;
    SdfValueTypeName sdfType;
    std::string outputName;
    TfToken colorSpace;
};

static std::map<std::string, BindInfo> _materialMapBindings = {
    { "baseColor", { "baseColor", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
    { "absorptionColor",
      { "absorptionColor", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
    { "normal", { "normal", SdfValueTypeNames->Float3, "rgb", AdobeTokens->raw } },
    { "roughness", { "roughness", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "metallic", { "metallic", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "height", { "height", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "opacity", { "opacity", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "specularLevel", { "specularLevel", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "specularEdgeColor",
      { "specularEdgeColor", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
    { "anisotropyLevel", { "anisotropyLevel", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "anisotropyAngle", { "anisotropyAngle", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "sheenOpacity", { "sheenOpacity", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "sheenColor", { "sheenColor", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
    { "sheenRoughness", { "sheenRoughness", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "coatOpacity", { "coatOpacity", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "coatColor", { "coatColor", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
    { "coatNormal", { "coatNormal", SdfValueTypeNames->Float3, "rgb", AdobeTokens->raw } },
    { "coatRoughness", { "coatRoughness", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "coatSpecularLevel",
      { "coatSpecularLevel", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "translucency", { "translucency", SdfValueTypeNames->Float, "r", AdobeTokens->raw } },
    { "scatteringColor",
      { "scatteringColor", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
    { "scatteringDistanceScale",
      { "scatteringDistanceScale", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
    { "emissive", { "emissive", SdfValueTypeNames->Color3f, "rgb", AdobeTokens->sRGB } },
};

SdfPath
bindTexture(SdfAbstractData* sdfData,
            const SdfPath& parentPath,
            const BindInfo& bindInfo,
            const SdfPath& uvOutputAttrPath,
            const SdfPath& textureAssetAttrPath,
            const SdfPath& fallbackAttrPath,
            const SdfPath& scaleAttrPath,
            const SdfPath& biasAttrPath)
{
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("bindTexture: Binding texture channel %s\n", bindInfo.name.c_str());

    SdfPath resultPath = createShader(sdfData,
                                      parentPath,
                                      TfToken("file" + bindInfo.name),
                                      AdobeTokens->UsdUVTexture,
                                      bindInfo.outputName,
                                      { { "sourceColorSpace", bindInfo.colorSpace },
                                        { "wrapS", AdobeTokens->repeat },
                                        { "wrapT", AdobeTokens->repeat } },
                                      { { "st", uvOutputAttrPath },
                                        { "file", textureAssetAttrPath },
                                        { "fallback", fallbackAttrPath },
                                        { "scale", scaleAttrPath },
                                        { "bias", biasAttrPath } });

    return resultPath;
}

bool
addUsdAsmShaderImpl(SdfAbstractData* sdfData,
                    const SdfPath& materialPath,
                    const GraphDesc& graphDesc,
                    const std::map<std::string, BindInfo>& mapBindings)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("addUsdAsmShaderImpl: Adding ASM Implementation\n");

    // Create a NodeGraph for the ASM implementation
    SdfPath scopePath =
      createPrimSpec(sdfData, materialPath, AdobeTokens->ASM, UsdShadeTokens->NodeGraph);

    SdfPath uvChannelNamePath = inputPath(materialPath, uv_channel_name);

    // Create Texcoord Reader
    SdfPath txOutputPath = createShader(sdfData,
                                        scopePath,
                                        _tokens->TexCoordReader,
                                        AdobeTokens->UsdPrimvarReader_float2,
                                        "result",
                                        {},
                                        { { "varname", uvChannelNamePath } });
#ifdef USDSBSAR_ENABLE_TEXTURE_TRANSFORM
    SdfPath uvScaleInputPath = inputPath(materialPath, uv_scale_input);
    SdfPath uvRotationInputPath = inputPath(materialPath, uv_rotation_input);
    SdfPath uvTranslationInputPath = inputPath(materialPath, uv_translation_input);

    // Create UV Transform
    SdfPath uvOutputPath = createShader(sdfData,
                                        scopePath,
                                        _tokens->UvTransform,
                                        AdobeTokens->UsdTransform2d,
                                        "result",
                                        {},
                                        { { "in", txOutputPath },
                                          { "scale", uvScaleInputPath },
                                          { "rotation", uvRotationInputPath },
                                          { "translation", uvTranslationInputPath } });

#else  // NOT USDSBSAR_ENABLE_TEXTURE_TRANSFORM
    SdfPath uvOutputPath = txOutputPath;
#endif // USDSBSAR_ENABLE_TEXTURE_TRANSFORM

    // Create texture sampling nodes
    InputConnections inputConnections;
    InputValues inputValues;

    for (auto& usage : mapped_usages) {
        TF_DEBUG(FILE_FORMAT_SBSAR)
          .Msg("addUsdAsmShaderImpl: Looking for usage : '%s'\n", usage.c_str());
        if (hasUsage(usage, graphDesc)) {
            auto it = mapBindings.find(usage);
            if (it != mapBindings.end()) {
                const BindInfo& bindInfo = it->second;

                // Get the path of the texture attribute on the Material prim
                std::string texAssetName = getTextureAssetName(usage);
                SdfPath textureAssetAttrPath = inputPath(materialPath, texAssetName);

                // Add default value if present
                SdfPath fallbackAttrPath;
                auto defaultIt = default_channels.find(usage);
                if (defaultIt != default_channels.end()) {
                    auto defaultName = getDefaultValueNames(usage);
                    fallbackAttrPath = inputPath(materialPath, defaultName.first);
                }

                SdfPath scaleAttrPath, biasAttrPath;
                if (isNormal(usage)) {
                    const auto [scaleName, biasName] = getNormalMapScaleAndBiasNames(usage);
                    scaleAttrPath = inputPath(materialPath, scaleName);
                    biasAttrPath = inputPath(materialPath, biasName);
                }

                // Create the texture reader
                SdfPath texResultPath = bindTexture(sdfData,
                                                    scopePath,
                                                    bindInfo,
                                                    uvOutputPath,
                                                    textureAssetAttrPath,
                                                    fallbackAttrPath,
                                                    scaleAttrPath,
                                                    biasAttrPath);

                if (usage == "emissive") {
                    inputValues.emplace_back("emissiveIntensity", 1.0f);
                }
                inputConnections.emplace_back(bindInfo.name, texResultPath);
            }
        }
    }

    // Connect to uniform values
    for (auto& usage : uniform_usages) {
        if (hasUsage(usage, graphDesc)) {
            SdfPath textureAssetAttrPath = inputPath(materialPath, usage);
            inputConnections.emplace_back(usage, textureAssetAttrPath);
        }
    }

    // Create ASM shader
    SdfPath surfaceOutputPath = createShader(sdfData,
                                             scopePath,
                                             _tokens->AdobeStandardMaterial,
                                             AdobeTokens->adobeStandardMaterial,
                                             "surface",
                                             inputValues,
                                             inputConnections);
    createShaderOutput(
      sdfData, materialPath, "adobe:surface", SdfValueTypeNames->Token, surfaceOutputPath);

    return true;
}

}

namespace adobe::usd::sbsar {

bool
addAsmShader(SdfAbstractData* sdfData, const SdfPath& materialPath, const GraphDesc& graphDesc)
{
    return addUsdAsmShaderImpl(sdfData, materialPath, graphDesc, _materialMapBindings);
}

}
