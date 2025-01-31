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
#include "sbsarMtlx.h"
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
    (Mtlx)
    (UvRotate)
    (UvScale)
    (UvTranslate)
    (WsNormal)
);
// clang-format on

struct BindInfo
{
    std::string name;
    SdfValueTypeName sdfType;
    std::string outputName;
    std::string color_space;
};

static std::map<std::string, BindInfo> _opaqueMapBindings = {
    { "baseColor", { "base_color", SdfValueTypeNames->Color3f, "out", "sRGB" } },
    { "ambientOcclusion", { "ambient_occlusion", SdfValueTypeNames->Float, "out", "raw" } },
    { "roughness", { "roughness", SdfValueTypeNames->Float, "out", "raw" } },
    { "metallic", { "metallic", SdfValueTypeNames->Float, "out", "raw" } },
    { "normal", { "normal", SdfValueTypeNames->Float3, "out", "raw" } },
    { "opacity", { "opacity", SdfValueTypeNames->Float, "out", "raw" } },
    { "emissive", { "emission_color", SdfValueTypeNames->Color3f, "out", "sRGB" } }
};

static std::map<std::string, BindInfo> _refractiveMapBindings = {
    { "baseColor", { "base_color", SdfValueTypeNames->Color3f, "out", "sRGB" } },
    { "ambientOcclusion", { "ambient_occlusion", SdfValueTypeNames->Float, "out", "raw" } },
    { "roughness", { "roughness", SdfValueTypeNames->Float, "out", "raw" } },
    { "metallic", { "metallic", SdfValueTypeNames->Float, "out", "raw" } },
    { "normal", { "normal", SdfValueTypeNames->Float3, "out", "raw" } },
    { "refraction", { "opacity", SdfValueTypeNames->Float, "out", "raw" } },
    { "emissive", { "emission_color", SdfValueTypeNames->Color3f, "out", "sRGB" } }
};

SdfPath
bindTexture(SdfAbstractData* sdfData,
            const SdfPath& parentPath,
            const BindInfo& bindInfo,
            const SdfPath& uvOutputAttrPath,
            const SdfPath& textureAssetAttrPath)
{
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("bindTexture: Binding texture channel %s\n", bindInfo.name.c_str());

    TfToken shaderType;
    if (bindInfo.sdfType == SdfValueTypeNames->Color3f) {
        shaderType = MtlXTokens->ND_image_color3;
    } else if (bindInfo.sdfType == SdfValueTypeNames->Float3) {
        shaderType = MtlXTokens->ND_image_vector3;
    } else if (bindInfo.sdfType == SdfValueTypeNames->Float) {
        shaderType = MtlXTokens->ND_image_float;
    } else {
        TF_CODING_ERROR("Unsupported texture type %s", bindInfo.sdfType.GetAsToken().GetText());
        return {};
    }

    // Note, there is currently no support for the color space choice. Also no support for a
    // fallback value. Bias and scale are also not supported.
    SdfPath resultPath = createShader(
      sdfData,
      parentPath,
      TfToken("file" + bindInfo.name),
      shaderType,
      "out",
      { { "uaddressmode", std::string("periodic") }, { "vaddressmode", std::string("periodic") } },
      { { "texcoord", uvOutputAttrPath }, { "file", textureAssetAttrPath } });

    return resultPath;
}

bool
addUsdMtlxShaderImpl(SdfAbstractData* sdfData,
                     const SdfPath& materialPath,
                     const GraphDesc& graphDesc,
                     const std::map<std::string, BindInfo>& mapBindings)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("addUsdMtlxShaderImpl: Adding MaterialX Implementation\n");

    // Create a scope for the UsdPreviewSurface implementation
    SdfPath scopePath =
      createPrimSpec(sdfData, materialPath, _tokens->Mtlx, UsdShadeTokens->NodeGraph);

    // Create Texcoord Reader
    SdfPath txOutputPath = createShader(
      sdfData, scopePath, _tokens->TexCoordReader, MtlXTokens->ND_texcoord_vector2, "out");

#ifdef USDSBSAR_ENABLE_TEXTURE_TRANSFORM
    SdfPath uvScaleInputPath = inputPath(materialPath, uv_scale_input);
    SdfPath uvRotationInputPath = inputPath(materialPath, uv_rotation_input);
    SdfPath uvTranslationInputPath = inputPath(materialPath, uv_translation_input);

    // Create UV transform by applying rotation, scale and transform, in that order
    SdfPath rotOutputPath =
      createShader(sdfData,
                   scopePath,
                   _tokens->UvRotate,
                   MtlXTokens->ND_rotate2d_vector2,
                   "out",
                   {},
                   { { "amount", uvRotationInputPath }, { "in", txOutputPath } });

    SdfPath scaleOutputPath =
      createShader(sdfData,
                   scopePath,
                   _tokens->UvScale,
                   MtlXTokens->ND_multiply_vector2,
                   "out",
                   {},
                   { { "in1", uvScaleInputPath }, { "in2", rotOutputPath } });

    SdfPath uvOutputPath =
      createShader(sdfData,
                   scopePath,
                   _tokens->UvTranslate,
                   MtlXTokens->ND_add_vector2,
                   "out",
                   {},
                   { { "in1", uvTranslationInputPath }, { "in2", scaleOutputPath } });

#else  // NOT USDSBSAR_ENABLE_TEXTURE_TRANSFORM
    SdfPath uvOutputPath = txOutputPath;
#endif // USDSBSAR_ENABLE_TEXTURE_TRANSFORM

    // Create texture sampling nodes
    InputConnections inputConnections;
    for (auto& usage : mapped_usages) {
        if (hasUsage(usage, graphDesc)) {
            auto it = mapBindings.find(usage);
            if (it != mapBindings.end()) {
                const BindInfo& bindInfo = it->second;

                // Get the path of the texture attribute on the Material prim
                std::string texAssetName = getTextureAssetName(usage);
                SdfPath textureAssetAttrPath = inputPath(materialPath, texAssetName);

                // Create the texture reader
                SdfPath texResultPath =
                  bindTexture(sdfData, scopePath, bindInfo, uvOutputPath, textureAssetAttrPath);

                if (isNormal(usage)) {
                    // Normal maps are disabled in MaterialX now since they
                    // behave strangely in USD view

                    // Route normal map through a normal map node
                    // TODO: When we reactivate this we need to make sure we can handle DirectX and
                    // OpenGL style normal maps. By default we can assume DirectX style maps, but
                    // we have a setup that uses scale and bias for the other networks to control
                    // how the texture maps are decoded to support both.
                    // SdfPath wsNormalPath = createShader(sdfData,
                    //                                     scopePath,
                    //                                     _tokens->WsNormal,
                    //                                     MtlXTokens->ND_normalmap,
                    //                                     "out",
                    //                                     {},
                    //                                     { { "in", texResultPath } });

                    // inputConnections.emplace_back(bindInfo.name, wsNormalPath);
                } else {
                    inputConnections.emplace_back(bindInfo.name, texResultPath);
                }
            }
        }
    }

    // Create MaterialX shader for Adobe Standard Material
    SdfPath surfaceOutputPath = createShader(sdfData,
                                             scopePath,
                                             MtlXTokens->ND_adobe_standard_material,
                                             MtlXTokens->ND_adobe_standard_material,
                                             "surface",
                                             {},
                                             inputConnections);
    createShaderOutput(
      sdfData, materialPath, "mtlx:surface", SdfValueTypeNames->Token, surfaceOutputPath);

    return true;
}

}

namespace adobe::usd::sbsar {

bool
addMtlxShader(SdfAbstractData* sdfData,
              const SdfPath& materialPath,
              const SubstanceAir::GraphDesc& graphDesc)
{
    return addUsdMtlxShaderImpl(sdfData, materialPath, graphDesc, _opaqueMapBindings);
}

bool
addMtlxShaderRefractive(SdfAbstractData* sdfData,
                        const SdfPath& materialPath,
                        const SubstanceAir::GraphDesc& graphDesc)
{
    return addUsdMtlxShaderImpl(sdfData, materialPath, graphDesc, _refractiveMapBindings);
}

}
