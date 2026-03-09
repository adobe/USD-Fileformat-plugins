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
#include "sbsarOpenPBR.h"
#include "usdGenerationHelpers.h"
#include <sbsarDebug.h>

// File format utils
#include <fileformatutils/common.h>
#include <fileformatutils/layerWriteOpenPBR.h>
#include <fileformatutils/sdfMaterialUtils.h>
#include <fileformatutils/sdfUtils.h>
#include <fileformatutils/usdData.h>

#include <pxr/usd/usdShade/tokens.h>

using namespace SubstanceAir;
PXR_NAMESPACE_USING_DIRECTIVE

namespace {

using namespace adobe::usd;
using namespace adobe::usd::sbsar;

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (TexCoordReader)
    (OpenPBR)
    (UvTransform)
    (WsNormal)
    (Surface)
    (Displacement)
    (HeightLevel)
);
// clang-format on

struct BindInfo
{
    TfToken name;
    SdfValueTypeName sdfType;
    std::string outputName;
    TfToken colorSpace;
};

// This is a mapping from SBSAR usage to OpenPBR inputs
// Notes:
// * OpenPBR does not directly support ambient occlusion
// * IOR is not a texturable output and we don't have a mapping for uniform values yet
// * "anisotropyAngle" would be expressed via geometry_tangent
// * Not clear how "coatSpecularLevel" factors in
// * "height" for displacement is not handled here
//   * ND_displacement_float
//     * displacement - float
//     * scale - float
//     * out - displacementshader
// * "refraction" is not supported
// * The colors, at least for base color seem to be off in OpenPBR/MaterialX in Eclair
//   * Maybe we need an explicit color conversion. The colorSpace is currently not considered
static std::map<std::string, BindInfo> _materialMapBindings = {
    // * Base
    // base_weight (no source info)
    { "baseColor",
      { OpenPbrTokens->base_color, SdfValueTypeNames->Color3f, "out", AdobeTokens->sRGB } },
    // base_diffuse_roughness (no source info) see above
    { "metallic",
      { OpenPbrTokens->base_metalness, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },

    // * Specular
    { "specularLevel",
      { OpenPbrTokens->specular_weight, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "specularEdgeColor",
      { OpenPbrTokens->specular_color, SdfValueTypeNames->Color3f, "out", AdobeTokens->sRGB } },
    { "roughness",
      { OpenPbrTokens->specular_roughness, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    // specular_ior (no source info)
    // XXX does this work?
    //{ "IOR", { OpenPbrTokens->specular_ior, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "anisotropyLevel",
      { OpenPbrTokens->specular_roughness_anisotropy,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw } },

    // * Transmission
    { "translucency",
      { OpenPbrTokens->transmission_weight, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "absorptionColor",
      { OpenPbrTokens->transmission_color, SdfValueTypeNames->Color3f, "out", AdobeTokens->sRGB } },
    // transmission_depth (no source info) (absorption distance?)
    // transmission_scatter (no source info)
    // transmission_scatter_anisotropy (no source info)
    // transmission_dispersion_scale (no source info)
    // transmission_dispersion_abbe_number (no source info)

    // * Subsurface
    // subsurface_weight (no source info) (is set to 1 if we have scatterng color or distance scale)
    { "scatteringColor",
      { OpenPbrTokens->transmission_scatter,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB } },
    { "scatteringDistanceScale",
      { OpenPbrTokens->subsurface_radius_scale,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB } },
    // subsurface_radius_scale (no source info) (maps to ASM scatteringDistanceScale)
    // subsurface_anisotropy (no source info)
    // subsurface_scatter_anisotropy (no source info)

    // * Fuzz
    { "sheenOpacity",
      { OpenPbrTokens->fuzz_weight, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "sheenColor",
      { OpenPbrTokens->fuzz_color, SdfValueTypeNames->Color3f, "out", AdobeTokens->sRGB } },
    { "sheenRoughness",
      { OpenPbrTokens->fuzz_roughness, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },

    // * Coat
    { "coatOpacity",
      { OpenPbrTokens->coat_weight, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "coatColor",
      { OpenPbrTokens->coat_color, SdfValueTypeNames->Color3f, "out", AdobeTokens->sRGB } },
    { "coatRoughness",
      { OpenPbrTokens->coat_roughness, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    // coat_roughness_anisotropy (no source info)
    // coat_ior (no source info)
    // coat_darkening (no source info)

    // * Thin film
    // thin_film_weight (no source info)
    // thin_film_thickness (no source info)
    // thin_film_ior (no source info)

    // * Emission
    // emission_luminance (no source info) (is set to 1000 if we have "emissive" input)
    { "emissive",
      { OpenPbrTokens->emission_color, SdfValueTypeNames->Color3f, "out", AdobeTokens->sRGB } },

    // * Displacement
    // height, heightLevel and heightScale are sbs inputs that have the same names as ASM inputs
    // but not OpenPBR native. We keep the ASM naming of the material inputs which are connected
    // to a seperate displacement shader.
    { "height", { TfToken("height"), SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "heightLevel",
      { TfToken("heightLevel"), SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "heightScale",
      { TfToken("heightScale"), SdfValueTypeNames->Float, "out", AdobeTokens->raw } },

    // * Geometry
    { "opacity",
      { OpenPbrTokens->geometry_opacity, SdfValueTypeNames->Float, "out", AdobeTokens->raw } },
    { "normal",
      { OpenPbrTokens->geometry_normal, SdfValueTypeNames->Float3, "out", AdobeTokens->raw } },
    { "coatNormal",
      { OpenPbrTokens->geometry_coat_normal, SdfValueTypeNames->Float3, "out", AdobeTokens->raw } },
    // geometry_tangent (no source info) (derive from anisotropyAngle?)
    // geometry_coat_tangent (no source info)
};

static const std::string heightStr = "height";
static const std::string heightLevelStr = "heightLevel";
static const std::string heightScaleStr = "heightScale";

SdfPath
bindTexture(SdfAbstractData* sdfData,
            const SdfPath& parentPath,
            const BindInfo& bindInfo,
            const SdfPath& uvOutputAttrPath,
            const SdfPath& textureAssetAttrPath,
            const NormalFormat& initialNormalFormat)
{
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("bindTexture: Binding texture channel %s\n", bindInfo.name.GetText());

    auto name = bindInfo.name;
    Input input;
    // XXX hardcoding this to repeat since we don't have the wrap mode info from the SBSAR at this
    // point and these are typically always repeating textures
    input.wrapS = AdobeTokens->repeat;
    input.wrapT = AdobeTokens->repeat;
    input.scale = kDefaultTexScale;
    input.bias = kDefaultTexBias;
    if (name == OpenPbrTokens->geometry_normal || name == OpenPbrTokens->geometry_coat_normal) {
        if (initialNormalFormat == NormalFormat::DirectX ||
            initialNormalFormat == NormalFormat::Unknown) {
            input.scale = kDirectXNormalTexScale;
            input.bias = kDirectXNormalTexBias;
        } else {
            input.scale = kOpenGLNormalTexScale;
            input.bias = kOpenGLNormalTexBias;
        }
    }

    if (bindInfo.sdfType == SdfValueTypeNames->Color3f) {
        input.channel = AdobeTokens->rgb;
        input.colorspace = AdobeTokens->sRGB;
    } else if (bindInfo.sdfType == SdfValueTypeNames->Float3) {
        input.channel = AdobeTokens->rgb;
    } else if (bindInfo.sdfType == SdfValueTypeNames->Float) {
        input.channel = AdobeTokens->r;
    } else {
        TF_CODING_ERROR("Unsupported texture type %s", bindInfo.sdfType.GetAsToken().GetText());
        return {};
    }

    SdfPath textureOutput = createMaterialXTextureReader(
      sdfData, parentPath, name, input, uvOutputAttrPath, textureAssetAttrPath);

    return textureOutput;
}

bool
addUsdOpenPbrShaderImpl(SdfAbstractData* sdfData,
                        const SdfPath& materialPath,
                        const GraphDesc& graphDesc,
                        const std::map<std::string, BindInfo>& mapBindings,
                        const NormalFormat& initialNormalFormat)
{
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("addUsdOpenPbrShaderImpl: Adding OpenPBR/MaterialX Implementation\n");

    // Create top level inputs to control the UV coordinate channel and the UV address modes.
    SdfPath uvChannelNamePath =
      createShaderInput(sdfData, materialPath, uv_channel_name, SdfValueTypeNames->String);
    setAttributeDefaultValue(
      sdfData, uvChannelNamePath, std::string("st"), SdfValueTypeNames->String);

    // Create a scope for the OpenPBR implementation
    SdfPath scopePath =
      createPrimSpec(sdfData, materialPath, _tokens->OpenPBR, UsdShadeTokens->NodeGraph);

    // Create Texcoord Reader
    SdfPath txOutputPath = createShader(sdfData,
                                        scopePath,
                                        _tokens->TexCoordReader,
                                        MtlXTokens->ND_geompropvalue_vector2,
                                        "out",
                                        {},
                                        { { "geomprop", uvChannelNamePath } });

#ifdef USDSBSAR_ENABLE_TEXTURE_TRANSFORM
    SdfPath uvScaleInputPath = inputPath(materialPath, uv_scale_inverse_input);
    SdfPath uvRotationInputPath = inputPath(materialPath, uv_rotation_input);
    SdfPath uvTranslationInputPath = inputPath(materialPath, uv_translation_input);

    // Create UV transform by applying scale, rotation and translation, in that order
    // This matches what the UsdTransform2d node does
    SdfPath uvOutputPath = createShader(sdfData,
                                        scopePath,
                                        _tokens->UvTransform,
                                        MtlXTokens->ND_place2d_vector2,
                                        "out",
                                        {},
                                        { { "texcoord", txOutputPath },
                                          { "scale", uvScaleInputPath },
                                          { "rotate", uvRotationInputPath },
                                          { "offset", uvTranslationInputPath } });
#else  // NOT USDSBSAR_ENABLE_TEXTURE_TRANSFORM
    SdfPath uvOutputPath = txOutputPath;
#endif // USDSBSAR_ENABLE_TEXTURE_TRANSFORM

    auto createTextureReader = [&](const std::string& usage, const BindInfo& bindInfo) -> SdfPath {
        // Get the path of the texture attribute on the Material prim
        std::string texAssetName = getTextureAssetName(usage);
        SdfPath textureAssetAttrPath = inputPath(materialPath, texAssetName);

        // Create the texture reader
        SdfPath texResultPath = bindTexture(
          sdfData, scopePath, bindInfo, uvOutputPath, textureAssetAttrPath, initialNormalFormat);
        return texResultPath;
    };

    auto createMaterialInput = [&](const std::string& usage, float defaultValue) -> SdfPath {
        SdfPath path;
        if (hasUsage(usage, graphDesc)) {
            auto it = mapBindings.find(usage);
            if (it != mapBindings.end()) {
                path = createTextureReader(usage, it->second);
            }
        }
        if (path.IsEmpty()) {
            path = createShaderInput(sdfData, materialPath, usage, SdfValueTypeNames->Float);
            setAttributeDefaultValue(
              sdfData, path, VtValue(defaultValue), SdfValueTypeNames->Float);
        }
        return path;
    };

    SdfPath heightLevelAttrPath;
    SdfPath heightScaleAttrPath;
    if (hasUsage(heightStr, graphDesc)) {
        heightLevelAttrPath = createMaterialInput(heightLevelStr, 0.5f);
        heightScaleAttrPath = createMaterialInput(heightScaleStr, 1.0f);
    }

    // Create texture sampling nodes
    InputValues inputValues;
    InputConnections inputConnections;
    bool enableSubsurface = false;
    for (const auto& usage : mapped_usages) {
        if (hasUsage(usage, graphDesc)) {
            if (usage == heightLevelStr || usage == heightScaleStr) {
                // these are handled above when "height" is present so skip
                continue;
            }

            auto it = mapBindings.find(usage);
            if (it != mapBindings.end()) {
                const BindInfo& bindInfo = it->second;
                SdfPath texResultPath = createTextureReader(usage, bindInfo);

                if (usage == heightStr) {
                    SdfPath heightLevel =
                      createShader(sdfData,
                                   scopePath,
                                   _tokens->HeightLevel,
                                   MtlXTokens->ND_subtract_float,
                                   "out",
                                   {},
                                   { { "in1", texResultPath }, { "in2", heightLevelAttrPath } });

                    SdfPath displacementOutputPath = createShader(
                      sdfData,
                      scopePath,
                      _tokens->Displacement,
                      MtlXTokens->ND_displacement_float,
                      "out",
                      {},
                      { { "displacement", heightLevel }, { "scale", heightScaleAttrPath } });

                    createShaderOutput(sdfData,
                                       materialPath,
                                       "mtlx:displacement",
                                       SdfValueTypeNames->Token,
                                       displacementOutputPath);

                } else {
                    inputConnections.emplace_back(bindInfo.name.GetString(), texResultPath);
                }

                if (usage == "scatteringColor" || usage == "scatteringDistanceScale") {
                    enableSubsurface = true;
                }

                if (usage == "emissive") {
                    // The luminance should be part of of the `scale` or `value` of the
                    // emission_color input texture reader, but that is missing.
                    // Still we need to turn emission on by setting the luminance to 1000.0,
                    // otherwise emission is turned off.
                    inputValues.emplace_back(OpenPbrTokens->emission_luminance, 1000.0f);
                }
            }
        }
    }

    if (enableSubsurface) {
        inputValues.emplace_back(OpenPbrTokens->subsurface_weight, 1.0f);
    }

// TODO: build mapping table for uniform values from the SBSAR usages to the corresponding
// inputs for OpenPBR. E.g. IOR -> specular_ior, etc.
#if 0
    // Connect to uniform values
    for (auto& usage : uniform_usages) {
        if (hasUsage(usage, graphDesc)) {
            SdfPath uniformAttrPath = inputPath(materialPath, usage);
            inputConnections.emplace_back(usage, uniformAttrPath);
        }
    }
#endif

    // Create MaterialX shader for Adobe Standard Material
    SdfPath surfaceOutputPath = createShader(sdfData,
                                             scopePath,
                                             _tokens->Surface,
                                             MtlXTokens->ND_open_pbr_surface_surfaceshader,
                                             "out",
                                             inputValues,
                                             inputConnections);
    createShaderOutput(
      sdfData, materialPath, "mtlx:surface", SdfValueTypeNames->Token, surfaceOutputPath);

    return true;
}

} // namespace

namespace adobe::usd::sbsar {

bool
addOpenPbrShader(SdfAbstractData* sdfData,
                 const SdfPath& materialPath,
                 const SubstanceAir::GraphDesc& graphDesc,
                 const NormalFormat& initialNormalFormat)
{
    return addUsdOpenPbrShaderImpl(
      sdfData, materialPath, graphDesc, _materialMapBindings, initialNormalFormat);
}

}
