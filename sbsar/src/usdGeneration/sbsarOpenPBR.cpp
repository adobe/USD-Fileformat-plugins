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
    GfVec4f scale;
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
    // base_weight (no ASM source info)
    { "baseColor",
      { OpenPbrTokens->base_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    // ambient occlusion will be handled with a custom shader graph since OpenPBR does not have a
    // dedicated input for it
    { "ambientOcclusion",
      { AsmTokens->ambientOcclusion,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    // base_diffuse_roughness (no ASM source info)
    { "metallic",
      { OpenPbrTokens->base_metalness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Specular

    // specular_weight = 2.0 * specularLevel so we specify a non-unit scale value that is applied to
    // the texture input in the shader graph that specularLevel is connected to.
    { "specularLevel",
      { OpenPbrTokens->specular_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        GfVec4f(2.0f, 2.0f, 2.0f, 1.0f) } },
    { "specularEdgeColor",
      { OpenPbrTokens->specular_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "roughness",
      { OpenPbrTokens->specular_roughness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    // specular_ior (no ASM source info)
    { "anisotropyLevel",
      { OpenPbrTokens->specular_roughness_anisotropy,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Transmission
    { "translucency",
      { OpenPbrTokens->transmission_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "absorptionColor",
      { OpenPbrTokens->transmission_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    // transmission_depth (no ASM source info)
    // transmission_scatter (no ASM source info)
    // transmission_scatter_anisotropy (no ASM source info)
    // transmission_dispersion_scale (no ASM source info)
    // transmission_dispersion_abbe_number (no ASM source info)

    // * Subsurface
    // subsurface_weight (no ASM source info) (is set to 1 if we have scattering color or distance
    // scale)
    { "scatteringColor",
      { OpenPbrTokens->subsurface_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "scatteringDistanceScale",
      { OpenPbrTokens->subsurface_radius_scale,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "scatteringDistance",
      { OpenPbrTokens->subsurface_radius,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    // subsurface_scatter_anisotropy (no ASM source info)

    // * Fuzz
    { "sheenOpacity",
      { OpenPbrTokens->fuzz_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "sheenColor",
      { OpenPbrTokens->fuzz_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "sheenRoughness",
      { OpenPbrTokens->fuzz_roughness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Coat
    { "coatOpacity",
      { OpenPbrTokens->coat_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatColor",
      { OpenPbrTokens->coat_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "coatRoughness",
      { OpenPbrTokens->coat_roughness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    // coat_roughness_anisotropy (no ASM source info)
    // coat_ior (no ASM source info)
    // coat_darkening (no ASM source info)

    // * Thin film
    // thin_film_weight (no ASM source info)
    // thin_film_thickness (no ASM source info)
    // thin_film_ior (no ASM source info)

    // * Emission
    // emission_luminance (no ASM source info) (is set to 1000 if we have "emissive" input)
    { "emissive",
      { OpenPbrTokens->emission_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },

    // * Displacement
    // height, heightLevel and heightScale are sbs inputs that have the same names as ASM inputs
    // but not OpenPBR native. We keep the ASM naming of the material inputs which are connected
    // to a seperate displacement shader.
    { "height",
      { TfToken("height"), SdfValueTypeNames->Float, "out", AdobeTokens->raw, kDefaultTexScale } },
    { "heightLevel",
      { TfToken("heightLevel"),
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "heightScale",
      { TfToken("heightScale"),
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Geometry
    { "opacity",
      { OpenPbrTokens->geometry_opacity,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "normal",
      { OpenPbrTokens->geometry_normal,
        SdfValueTypeNames->Float3,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    // tangent is mapped to geometry_tangent
    { "tangent",
      { OpenPbrTokens->geometry_tangent,
        SdfValueTypeNames->Float3,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatNormal",
      { OpenPbrTokens->geometry_coat_normal,
        SdfValueTypeNames->Float3,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    // geometry_coat_tangent (no ASM source info)
};

// Identity mapping table for sbsar graphs authored with the OpenPBR material model.
// Each entry maps an OpenPBR output usage name directly to the same OpenPBR shader input.
// Note: specular_weight uses no scale factor — unlike the ASM "specularLevel" mapping
// (which applies 2x), a native OpenPBR sbsar already outputs values in the expected range.
static const std::map<std::string, BindInfo> _openPbrNativeMapBindings = {
    // * Base
    { "baseWeight",
      { OpenPbrTokens->base_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "baseColor",
      { OpenPbrTokens->base_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "baseDiffuseRoughness",
      { OpenPbrTokens->base_diffuse_roughness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "metallic",
      { OpenPbrTokens->base_metalness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    // ambient occlusion will be handled with a custom shader graph since OpenPBR does not have a
    // dedicated input for it
    { "ambientOcclusion",
      { AsmTokens->ambientOcclusion,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Specular
    { "specularWeight",
      { OpenPbrTokens->specular_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "specularColor",
      { OpenPbrTokens->specular_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "specularRoughness",
      { OpenPbrTokens->specular_roughness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "specularIOR",
      { OpenPbrTokens->specular_ior,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "specularRoughnessAnisotropy",
      { OpenPbrTokens->specular_roughness_anisotropy,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Transmission
    { "transmissionWeight",
      { OpenPbrTokens->transmission_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "transmissionColor",
      { OpenPbrTokens->transmission_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "transmissionDepth",
      { OpenPbrTokens->transmission_depth,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "transmissionScatter",
      { OpenPbrTokens->transmission_scatter,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "transmissionScatterAnisotropy",
      { OpenPbrTokens->transmission_scatter_anisotropy,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "transmissionDispersionScale",
      { OpenPbrTokens->transmission_dispersion_scale,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "transmissionDispersionAbbeNumber",
      { OpenPbrTokens->transmission_dispersion_abbe_number,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Subsurface
    { "subsurfaceWeight",
      { OpenPbrTokens->subsurface_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "subsurfaceColor",
      { OpenPbrTokens->subsurface_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "subsurfaceRadius",
      { OpenPbrTokens->subsurface_radius,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "subsurfaceRadiusScale",
      { OpenPbrTokens->subsurface_radius_scale,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "subsurfaceScatterAnisotropy",
      { OpenPbrTokens->subsurface_scatter_anisotropy,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Coat
    { "coatWeight",
      { OpenPbrTokens->coat_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatColor",
      { OpenPbrTokens->coat_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "coatRoughness",
      { OpenPbrTokens->coat_roughness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatRoughnessAnisotropy",
      { OpenPbrTokens->coat_roughness_anisotropy,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatIOR",
      { OpenPbrTokens->coat_ior,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatDarkening",
      { OpenPbrTokens->coat_darkening,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Fuzz
    { "fuzzWeight",
      { OpenPbrTokens->fuzz_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "fuzzColor",
      { OpenPbrTokens->fuzz_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },
    { "fuzzRoughness",
      { OpenPbrTokens->fuzz_roughness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Emission
    { "emissionLuminance",
      { OpenPbrTokens->emission_luminance,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "emissionColor",
      { OpenPbrTokens->emission_color,
        SdfValueTypeNames->Color3f,
        "out",
        AdobeTokens->sRGB,
        kDefaultTexScale } },

    // * Displacement
    // height, heightLevel and heightScale are sbs inputs that have the same names as ASM inputs
    // but not OpenPBR native. We keep the ASM naming of the material inputs which are connected
    // to a seperate displacement shader.
    { "height",
      { TfToken("height"), SdfValueTypeNames->Float, "out", AdobeTokens->raw, kDefaultTexScale } },
    { "heightLevel",
      { TfToken("heightLevel"),
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "heightScale",
      { TfToken("heightScale"),
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Thin Film
    { "thinFilmWeight",
      { OpenPbrTokens->thin_film_weight,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "thinFilmThickness",
      { OpenPbrTokens->thin_film_thickness,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "thinFilmIOR",
      { OpenPbrTokens->thin_film_ior,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },

    // * Geometry
    { "opacity",
      { OpenPbrTokens->geometry_opacity,
        SdfValueTypeNames->Float,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "normal",
      { OpenPbrTokens->geometry_normal,
        SdfValueTypeNames->Float3,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatNormal",
      { OpenPbrTokens->geometry_coat_normal,
        SdfValueTypeNames->Float3,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "tangent",
      { OpenPbrTokens->geometry_tangent,
        SdfValueTypeNames->Float3,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
    { "coatTangent",
      { OpenPbrTokens->geometry_coat_tangent,
        SdfValueTypeNames->Float3,
        "out",
        AdobeTokens->raw,
        kDefaultTexScale } },
};

static const std::string heightStr = "height";
static const std::string heightLevelStr = "heightLevel";
static const std::string heightScaleStr = "heightScale";
static const std::string baseColorStr = "baseColor";
static const std::string ambientOcclusionStr = "ambientOcclusion";

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
    if (bindInfo.scale != kDefaultTexScale) {
        input.scale = bindInfo.scale;
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
                        const std::vector<std::string>& usages,
                        const std::map<std::string, BindInfo>& mapBindings,
                        const NormalFormat& initialNormalFormat,
                        bool hasScatter)
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

    // Create texture sampling nodes
    InputValues inputValues;
    InputConnections inputConnections;
    bool enableSubsurface = false;
    for (const auto& usage : usages) {
        if (hasImageUsage(usage, graphDesc)) {
            if (usage == heightLevelStr || usage == heightScaleStr) {
                // these are handled above when "height" is present so skip
                continue;
            }
            if (usage == ambientOcclusionStr) {
                // ambient occlusion is handled below when baseColor is present
                continue;
            }

            auto it = mapBindings.find(usage);
            if (it != mapBindings.end()) {
                // translucency is normally mapped to transmission_weight but when hasScatter is
                // true, map to subsurface_weight.
                BindInfo bindInfo = it->second;
                if (hasScatter && usage == "translucency") {
                    bindInfo.name = OpenPbrTokens->subsurface_weight;
                }
                SdfPath texResultPath = createTextureReader(usage, bindInfo);

                if (usage == baseColorStr) {

                    // If we also have ambient occlusion, we convert the ambient occlusion float to
                    // a color3 and then combine the base color and ambient occlusion together with
                    // an ND_mix_color3 shader and connect the result to the base color input of the
                    // OpenPBR shader.

                    // create a texture node for ambient occlusion
                    SdfPath ambientOcclusionAttrPath;
                    if (hasUsage(ambientOcclusionStr, graphDesc)) {
                        auto it = mapBindings.find(ambientOcclusionStr);
                        if (it != mapBindings.end()) {
                            const BindInfo& bindInfo = it->second;
                            ambientOcclusionAttrPath =
                              createTextureReader(ambientOcclusionStr, bindInfo);
                        }
                    }
                    if (!ambientOcclusionAttrPath.IsEmpty()) {
                        // convert ambientOcclusion float to color3
                        SdfPath occlusionColorOutput =
                          createShader(sdfData,
                                       scopePath,
                                       AdobeTokens->AmbientOcclusionAsColor,
                                       MtlXTokens->ND_convert_float_color3,
                                       "out",
                                       {},
                                       { { "in", ambientOcclusionAttrPath } });

                        // Provide base_color and occlusion color as inputs to the ND_mix_color3
                        // node. Note: We use a fixed value of 0.0 for the "mix" input which means
                        // that the "bg" input is connected to the base color source and "fg" is
                        // connected to the ambient occlusion source.
                        SdfPath ambientOcclusionBaseColor =
                          createShader(sdfData,
                                       scopePath,
                                       AdobeTokens->AmbientOcclusionBaseColor,
                                       MtlXTokens->ND_mix_color3,
                                       "out",
                                       { { "mix", 0.0f } },
                                       { { "bg", texResultPath }, { "fg", occlusionColorOutput } });

                        // replace the original base color texture result with the output of the
                        // ambient occlusion mix node
                        texResultPath = ambientOcclusionBaseColor;
                    }
                    inputConnections.emplace_back(bindInfo.name.GetString(), texResultPath);

                } else if (usage == heightStr) {

                    SdfPath heightLevelAttrPath = createMaterialInput(heightLevelStr, 0.5f);
                    SdfPath heightScaleAttrPath = createMaterialInput(heightScaleStr, 1.0f);
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

    if (enableSubsurface && !hasScatter) {
        inputValues.emplace_back(OpenPbrTokens->subsurface_weight, 1.0f);
    }

    // Connect to uniform values
    for (auto& usage : usages) {
        auto [hasUsage, iotype] = getUsageAndSubstanceType(usage, graphDesc);
        if (hasUsage) {

            TF_DEBUG(FILE_FORMAT_SBSAR).Msg("uniform: %s\n", usage.c_str());
            if (iotype == SubstanceIOType::Substance_IOType_Image ||
                iotype == SubstanceIOType::Substance_IOType_String ||
                iotype == SubstanceIOType::Substance_IOType_Font) {
                continue;
            }
            auto it = mapBindings.find(usage);
            if (it != mapBindings.end()) {
                const BindInfo& bindInfo = it->second;
                SdfPath attrPath = inputPath(materialPath, usage);
                inputConnections.emplace_back(bindInfo.name.GetString(), attrPath);
            }
        }
    }

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
                 const NormalFormat& initialNormalFormat,
                 bool hasScatter)
{
    if (isOpenPbrNativeGraph(graphDesc)) {
        return addUsdOpenPbrShaderImpl(sdfData,
                                       materialPath,
                                       graphDesc,
                                       mapped_usages_openpbr,
                                       _openPbrNativeMapBindings,
                                       initialNormalFormat,
                                       hasScatter);
    }
    return addUsdOpenPbrShaderImpl(sdfData,
                                   materialPath,
                                   graphDesc,
                                   mapped_usages,
                                   _materialMapBindings,
                                   initialNormalFormat,
                                   hasScatter);
}

}
