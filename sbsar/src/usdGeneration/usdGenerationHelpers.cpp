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

#include "usdGenerationHelpers.h"
#include "dictEncoder.h"

#include <assetPath/assetPathParser.h>

#include <sbsarDebug.h>

#include <pxr/base/arch/hash.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>

#include <iomanip>

// File format utils
#include <fileformatutils/common.h>
#include <fileformatutils/images.h>
#include <fileformatutils/sdfUtils.h>

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace SubstanceAir;

namespace adobe::usd::sbsar {

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((proceduralParameters, "proceduralParameters"))
    ((preset, "preset"))
    ((defaultPreset, "__default__"))
    ((resolution, "resolution"))
);
// clang-format on

// # TODO it's hardcoded
// clang-format off
const std::vector<std::string> mapped_usages = {
    "baseColor",
    "absorptionColor",
    "ambientOcclusion",
    "roughness",
    "metallic",
    "normal",
    "opacity",
    "refraction",
    "emissive",
    "height",
    "specularLevel",
    "specularEdgeColor",
    "anisotropyLevel",
    "anisotropyAngle",
    "sheenOpacity",
    "sheenColor",
    "sheenRoughness",
    "coatOpacity",
    "coatColor",
    "coatNormal",
    "coatRoughness",
    "coatSpecularLevel",
    "translucency",
    "scatteringDistanceScale",
    "scatteringColor",
};
// clang-format on

const std::vector<std::string> uniform_usages = { "IOR",
                                                  "absorptionDistance",
                                                  "coatNormalScale",
                                                  "coatIOR",
                                                  "scatter",
                                                  "scatteringRayleigh",
                                                  "scatteringRedShift",
                                                  "scatteringDistance",
                                                  "emissiveIntensity",
                                                  "combineNormalAndHeight",
                                                  "heightLevel",
                                                  "heightScale",
                                                  "normalScale" };

const std::vector<std::string> normal_usages = { "normal", "coatNormal" };

const std::map<std::string, std::string> reserved_label_map = { { "$time", "Time" },
                                                                { "$outputsize", "Output Size" },
                                                                { "$randomseed", "Random Seed" },
                                                                { "$physicalsize",
                                                                  "Physical Size" } };

const std::map<std::string, DefaultChannel> default_channels = {
    { "baseColor",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.5f, 0.5f, 0.5f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "absorptionColor",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "normal",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 1.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "roughness",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.5f, 0.5f, 0.5f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "metallic",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "height",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.5f, 0.5f, 0.5f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "opacity",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "specularLevel",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.5f, 0.5f, 0.5f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "specularEdgeColor",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "anisotropyLevel",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "anisotropyAngle",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "sheenOpacity",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "sheenRoughness",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.5f, 0.5f, 0.5f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "coatOpacity",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "coatNormal",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.1f, 0.1f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "coatRoughness",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "coatSpecularLevel",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.5f, 0.5f, 0.5f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "translucency",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "scatteringDistanceScale",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "scatteringColor",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    { "emissive",
      { SdfValueTypeNames->Float4,
        VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)),
        { VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f)), VtValue(GfVec4f(1.0f, 1.0f, 1.0f, 1.0f)) } } },
    // Uniform
    { "IOR", { SdfValueTypeNames->Float, VtValue(1.4f), { VtValue(0.0f), VtValue(2.0f) } } },
    { "absorptionDistance",
      { SdfValueTypeNames->Float, VtValue(0.0f), { VtValue(0.0f), VtValue(1000.0f) } } },
    { "coatNormalScale",
      { SdfValueTypeNames->Float, VtValue(1.0f), { VtValue(0.0f), VtValue(1000.0f) } } },
    { "coatIOR", { SdfValueTypeNames->Float, VtValue(1.6f), { VtValue(1.0f), VtValue(3.0f) } } },
    { "scatter", { SdfValueTypeNames->Bool, VtValue(false), { VtValue(0.0f), VtValue(1.0f) } } },
    { "scatteringRayleigh",
      { SdfValueTypeNames->Float, VtValue(0.0f), { VtValue(0.0f), VtValue(1.0f) } } },
    { "scatteringRedShift",
      { SdfValueTypeNames->Float, VtValue(0.0f), { VtValue(0.0f), VtValue(1.0f) } } },
    { "scatteringDistance",
      { SdfValueTypeNames->Float, VtValue(1.0f), { VtValue(0.0f), VtValue(1000.0f) } } },
    { "emissiveIntensity",
      { SdfValueTypeNames->Float, VtValue(0.0f), { VtValue(0.0f), VtValue(1000.0f) } } },
    { "combineNormalAndHeight",
      { SdfValueTypeNames->Bool, VtValue(false), { VtValue(0.0f), VtValue(1.0f) } } },
    { "heightLevel",
      { SdfValueTypeNames->Float, VtValue(0.5f), { VtValue(0.0f), VtValue(1.0f) } } },
    { "heightScale",
      { SdfValueTypeNames->Float, VtValue(1.0f), { VtValue(0.0f), VtValue(1000.0f) } } },
    { "normalScale",
      { SdfValueTypeNames->Float, VtValue(1.0f), { VtValue(0.0f), VtValue(1000.0f) } } }
};

const std::vector<int> default_resolutions = { 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

const std::string uv_scale_input("uvscale");
const std::string uv_rotation_input("uvrotation");
const std::string uv_translation_input("uvtranslation");

const std::string uv_channel_name("uvChannelName");

const std::string proceduralParameterPrefix("procedural_sbsar:");

std::string
getResolutionVariantName(size_t xResLog2, size_t yResLog2)
{
    std::stringstream variantName;
    variantName << "res" << std::setfill('0') << std::setw(4) << static_cast<int>(pow(2, xResLog2))
                << "x" << std::setfill('0') << std::setw(4) << static_cast<int>(pow(2, yResLog2));
    return variantName.str();
}

GraphType
guessGraphType(const SubstanceAir::GraphDesc& graphDesc)
{
    // If we have an explicit graph type we use it
    if (graphDesc.mType == SubstanceAir::GraphType::GraphType_Material ||
        graphDesc.mType == SubstanceAir::GraphType::GraphType_DecalMaterial ||
        graphDesc.mType == SubstanceAir::GraphType::GraphType_AtlasMaterial) {
        return GraphType::Material;
    }
    if (graphDesc.mType == SubstanceAir::GraphType::GraphType_EnvironmentLight) {
        return GraphType::Light;
    }

    // If we don't have an explicit type we try to infer it via a heuristic

    // First aggressively check for material since this is the most common case
    for (const auto& u : mapped_usages) {
        if (hasUsage(u, graphDesc)) {
            return GraphType::Material;
        }
    }
    // Check for light
    if (hasUsage("environment", graphDesc)) {
        return GraphType::Light;
    }
    // Didn't find anything relevant
    return GraphType::Unknown;
}

std::pair<std::string, std::string>
getDefaultValueNames(const std::string& channelName)
{
    return { channelName + "_default", channelName + "_textureInfluence" };
}

std::string
getGraphName(const GraphDesc& desc)
{
    std::string uri = std::string(desc.mPackageUrl.c_str());
    // Package starts with pkg:[/*]
    TF_AXIOM(uri.substr(0, 4) == "pkg:");
    size_t slashes_end = uri.find_first_not_of('/', 4);
    return uri.substr(slashes_end);
}

bool
hasUsage(const std::string& usage, const GraphDesc& graphDesc)
{
    // This should potentially be cached to avoid double loop
    for (const auto& o : graphDesc.mOutputs) {
        for (const auto& c : o.mChannelsStr) {
            if (usage == c.c_str()) {
                return true;
            }
        }
    }
    return false;
}

bool
hasInput(const std::string& identifier, const GraphDesc& graphDesc)
{
    for (const auto& input : graphDesc.mInputs) {
        if (identifier.c_str() == input->mIdentifier) {
            return true;
        }
    }
    return false;
}

bool
isNormal(const std::string& usage)
{
    for (const std::string& normal_usage : normal_usages) {
        if (usage == normal_usage) {
            return true;
        }
    }
    return false;
}

JsValue
convertSbsarParameters(const VtDictionary& sbsarParameters)
{
    std::stringstream temp;
    DictEncoder::writeDict(sbsarParameters, temp);
    JsValue params = JsParseString(temp.str());
    if (!params.IsObject()) {
        TF_RUNTIME_ERROR("Parameters didn't parse to an object: %s", temp.str().c_str());
        return {};
    }
    return params;
}

void
convertColorLinearToSRGB(VtValue& value)
{
    if (value.IsHolding<GfVec3f>()) {
        GfVec3f v = value.UncheckedGet<GfVec3f>();
        v[0] = linearToSRGB(v[0]);
        v[1] = linearToSRGB(v[1]);
        v[2] = linearToSRGB(v[2]);
        value = v;
    }
}

void
convertColorSRGBToLinear(VtValue& value)
{
    if (value.IsHolding<GfVec3f>()) {
        GfVec3f v = value.UncheckedGet<GfVec3f>();
        v[0] = srgbToLinear(v[0]);
        v[1] = srgbToLinear(v[1]);
        v[2] = srgbToLinear(v[2]);
        value = v;
    }
}

std::pair<std::string, std::string>
getNormalMapScaleAndBiasNames(const std::string& channelName)
{
    return { channelName + "_scale", channelName + "_bias" };
}

// This is the name that all SBSAR files from Substance Source use
static const char* normalFormatParamName = "normal_format";
// We default to the OpenGL format, since that is the common format in USD
const NormalFormat defaultNormalFormat = NormalFormat::OpenGL;

bool
adjustNormalFormatInput(const SubstanceAir::string& identifier,
                        SubstanceIOType inputType,
                        VtValue& defaultValue)
{
    if (inputType == Substance_IOType_Integer && identifier == normalFormatParamName) {
        int currentValue = defaultValue.GetWithDefault<int>(-1);
        if ((currentValue == 0 ? NormalFormat::DirectX : NormalFormat::OpenGL) !=
            defaultNormalFormat) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("Detected normal format parameter %s with value %d. Changing to default %d.\n",
                   normalFormatParamName,
                   currentValue,
                   defaultNormalFormat == NormalFormat::DirectX ? 0 : 1);
            defaultValue = VtValue(defaultNormalFormat == NormalFormat::DirectX ? 0 : 1);
            return true;
        }
    }

    return false;
}

NormalFormat
getDefaultNormalFormat(const SubstanceAir::GraphDesc& graphDesc)
{
    bool hasNormalFormatInput = hasInput(normalFormatParamName, graphDesc);

    // If the graph has a normal format input, we're adjusting the default input values to match the
    // defaultNormalFormat. Otherwise we assume it's a DirectX normal map.
    return hasNormalFormatInput ? defaultNormalFormat : NormalFormat::DirectX;
}

NormalFormat
determineNormalFormat(const JsValue& jsParams)
{
    if (!jsParams.IsObject()) {
        TF_WARN("JsParams not a JsObject");
        return NormalFormat::Unknown;
    }
    const JsObject& jsObject = jsParams.GetJsObject();
    const auto it = jsObject.find(normalFormatParamName);
    if (it == jsObject.end()) {
        // It's OK if this is missing. Not all SBSARs have this parameter
        return NormalFormat::Unknown;
    }

    if (!it->second.IsInt()) {
        TF_WARN("%s parameter is not an int", normalFormatParamName);
        return NormalFormat::Unknown;
    }
    int normalFormat = it->second.GetInt();

    if (normalFormat == 0) {
        return NormalFormat::DirectX;
    } else if (normalFormat == 1) {
        return NormalFormat::OpenGL;
    }

    TF_WARN("%s parameter has value %d, which is not a supported value",
            normalFormatParamName,
            normalFormat);

    return NormalFormat::Unknown;
}

std::pair<GfVec4f, GfVec4f>
getNormalMapScaleAndBias(NormalFormat normalFormat)
{
    // By default we assume that SBSAR files generate DirectX normal maps
    if (normalFormat == NormalFormat::Unknown || normalFormat == NormalFormat::DirectX) {
        // USD usually expects OpenGL style normals maps. We express the conversion (flip of the
        // green channel) via the scale and bias.
        return { GfVec4f(2.0f, -2.0f, 2.0f, 1.0f), GfVec4f(-1.0f, 1.0f, -1.0f, 0.0f) };
    } else if (normalFormat == NormalFormat::OpenGL) {
        // The `XYZ = 2 * RGB - 1` base equation is always needed to unpack [0, 1] RGB values into a
        // XYZ vector in the [-1, 1] range.
        return { GfVec4f(2.0f, 2.0f, 2.0f, 1.0f), GfVec4f(-1.0f, -1.0f, -1.0f, 0.0f) };
    } else {
        TF_CODING_ERROR("Unsupported normalFormat");
        return { GfVec4f(1.0f), GfVec4f(0.0f) };
    }
}

std::string
generateSbsarInfoPath(const std::string& usage,
                      const MappedSymbol& graphName,
                      std::size_t sbsarHash,
                      const JsValue& params)
{
    ParsePathResult parsePathRes;
    parsePathRes.at = ParsePathResult::AT_IMAGE;
    parsePathRes.bt = ParsePathResult::BT_USAGE;
    parsePathRes.graphName = graphName.substanceName;
    parsePathRes.usage = usage;
    parsePathRes.packageHash = sbsarHash;
    parsePathRes.parameters = params;
    std::string resultPath;
    ParsePathResult::ParseError parseResult = generatePath(parsePathRes, resultPath);
    if (parseResult != ParsePathResult::PE_SUCCESS) {
        std::string temp = JsWriteToString(params);
        TF_RUNTIME_ERROR("Failed to parse json %s", temp.c_str());
        return {};
    }
    return resultPath;
}

std::string
getTextureAssetName(const std::string& usage)
{
    return usage + "_texture";
}

MappedSymbol
getGraphCategory(const GraphDesc& graphDesc, SymbolMapper& symbolMapper)
{
    return symbolMapper.GetSymbol(graphDesc.mCategory.c_str());
}

void
setGraphMetadataOnPrim(SdfAbstractData* sdfData,
                       const SdfPath& primPath,
                       const SubstanceAir::GraphDesc& graphDesc)
{
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("setGraphMetadataOnPrim: type %s (%d), packageUrl %s\n  label: %s\n  desc: %s\n  "
           "category: %s\n  keywords: %s\n  author: %s\n  authorUrl: %s\n  userTag: %s\n  "
           "thumbnail: %zu bytes\n",
           getGraphTypeNames()[graphDesc.mType],
           (int)graphDesc.mType,
           graphDesc.mPackageUrl.c_str(),
           graphDesc.mLabel.c_str(),
           graphDesc.mDescription.c_str(),
           graphDesc.mCategory.c_str(),
           graphDesc.mKeywords.c_str(),
           graphDesc.mAuthor.c_str(),
           graphDesc.mAuthorUrl.c_str(),
           graphDesc.mUserTag.c_str(),
           graphDesc.mThumbnail.size());

    if (!graphDesc.mLabel.empty()) {
        setPrimMetadata(
          sdfData, primPath, SdfFieldKeys->DisplayName, VtValue(graphDesc.mLabel.c_str()));
    }
    if (!graphDesc.mDescription.empty()) {
        setPrimMetadata(
          sdfData, primPath, SdfFieldKeys->Documentation, VtValue(graphDesc.mDescription.c_str()));
    }

    VtDictionary customData;
    if (graphDesc.mType < GraphType_UNSPECIFIED) {
        customData["graphType"] = getGraphTypeNames()[graphDesc.mType];
    }
    if (!graphDesc.mCategory.empty()) {
        customData["packageUrl"] = graphDesc.mPackageUrl.c_str();
    }
    if (!graphDesc.mCategory.empty()) {
        customData["category"] = graphDesc.mCategory.c_str();
    }
    if (!graphDesc.mKeywords.empty()) {
        auto keywords = split(std::string(graphDesc.mKeywords), ';');
        customData["keywords"] = VtArray<std::string>(keywords.begin(), keywords.end());
    }
    if (!graphDesc.mAuthor.empty()) {
        customData["author"] = graphDesc.mAuthor.c_str();
    }
    if (!graphDesc.mAuthorUrl.empty()) {
        customData["authorUrl"] = graphDesc.mAuthorUrl.c_str();
    }
    if (!graphDesc.mUserTag.empty()) {
        customData["userTag"] = graphDesc.mUserTag.c_str();
    }
    if (!customData.empty()) {
        setPrimMetadata(sdfData, primPath, SdfFieldKeys->CustomData, VtValue(customData));
    }
}

TfToken
getInputParamToken(SymbolMapper& symbolMapper, const std::string& substanceInputName)
{
    const MappedSymbol paramName = symbolMapper.GetSymbol(substanceInputName);
    return TfToken(proceduralParameterPrefix + paramName.usdName);
}

// Overload for substance strings
TfToken
getInputParamToken(SymbolMapper& symbolMapper, const SubstanceAir::string& identifier)
{
    return getInputParamToken(symbolMapper, std::string(identifier));
}

SdfValueTypeName
getInputSdfTypeName(SubstanceIOType substanceType, InputWidget substanceGuiWidget)
{
    if (substanceType == Substance_IOType_Float) {
        return SdfValueTypeNames->Float;
    } else if (substanceType == Substance_IOType_Float2) {
        return SdfValueTypeNames->Float2;
    } else if (substanceType == Substance_IOType_Float3) {
        if (substanceGuiWidget == InputWidget::Input_Color) {
            return SdfValueTypeNames->Color3f;
        } else {
            return SdfValueTypeNames->Float3;
        }
    } else if (substanceType == Substance_IOType_Float4) {
        if (substanceGuiWidget == InputWidget::Input_Color) {
            return SdfValueTypeNames->Color4f;
        } else {
            return SdfValueTypeNames->Float4;
        }
    } else if (substanceType == Substance_IOType_Integer) {
        return SdfValueTypeNames->Int;
    } else if (substanceType == Substance_IOType_Integer2) {
        return SdfValueTypeNames->Int2;
    } else if (substanceType == Substance_IOType_Integer3) {
        return SdfValueTypeNames->Int3;
    } else if (substanceType == Substance_IOType_Integer4) {
        return SdfValueTypeNames->Int4;
    } else if (substanceType == Substance_IOType_Image) {
        return SdfValueTypeNames->Asset;
    } else if (substanceType == Substance_IOType_String) {
        return SdfValueTypeNames->String;
    } else if (substanceType == Substance_IOType_Font) {
        TF_CODING_ERROR("No SdfType for Front");
    } else {
        TF_CODING_ERROR("Unknown SubstanceIOType");
    }

    return SdfValueTypeNames->Token;
}

// XXX N.B., the SubstanceAir::string is NOT a default std::string, it uses a custom allocator.
// Because of that it can't be put into a `VtValue` directly. It needs to be converted to a default
// std::string, otherwise the extraction code would have to use the SubstanceAir::string as well.
// When ever you see `VtValue(substanceAirString.c_str())` it is a short hand for making a copy into
// a regular std::string and packaging that up into a VtValue.

template<typename T>
T
substanceToUsdType(const T& v)
{
    return v;
}

std::string
substanceToUsdType(const SubstanceAir::string& v)
{
    return std::string(v);
}

GfVec2f
substanceToUsdType(const SubstanceAir::Vec2Float& v)
{
    return GfVec2f(v.x, v.y);
}

GfVec3f
substanceToUsdType(const SubstanceAir::Vec3Float& v)
{
    return GfVec3f(v.x, v.y, v.z);
}

GfVec4f
substanceToUsdType(const SubstanceAir::Vec4Float& v)
{
    return GfVec4f(v.x, v.y, v.z, v.w);
}

GfVec2i
substanceToUsdType(const SubstanceAir::Vec2Int& v)
{
    return GfVec2i(v.x, v.y);
}

GfVec3i
substanceToUsdType(const SubstanceAir::Vec3Int& v)
{
    return GfVec3i(v.x, v.y, v.z);
}

GfVec4i
substanceToUsdType(const SubstanceAir::Vec4Int& v)
{
    return GfVec4i(v.x, v.y, v.z, v.w);
}

GfVec3f
sRGBColorToLinear(const GfVec3f& v)
{
    return GfVec3f(srgbToLinear(v[0]), srgbToLinear(v[1]), srgbToLinear(v[2]));
}

template<typename T>
void
setupNumericalInput(const InputDescNumerical<T>* numericInput,
                    VtValue& defaultValue,
                    VtDictionary& guiWidgetData)
{
    auto usdDefaultValue = substanceToUsdType(numericInput->mDefaultValue);

    if (numericInput->mGuiWidget == SubstanceAir::InputWidget::Input_Slider) {
        guiWidgetData["minValue"] = VtValue(substanceToUsdType(numericInput->mMinValue));
        guiWidgetData["maxValue"] = VtValue(substanceToUsdType(numericInput->mMaxValue));
        guiWidgetData["step"] = VtValue(numericInput->mSliderStep);
        guiWidgetData["clamp"] = VtValue(numericInput->mSliderClamp);
        if (!numericInput->mGuiVecLabels.empty()) {
            std::vector<std::string> vecLabels;
            vecLabels.reserve(numericInput->mGuiVecLabels.size());
            for (const auto& label : numericInput->mGuiVecLabels) {
                vecLabels.emplace_back(label);
            }
            guiWidgetData["vecLabels"] = VtArray<std::string>(vecLabels.begin(), vecLabels.end());
        }
    } else if (numericInput->mGuiWidget == SubstanceAir::InputWidget::Input_Combobox) {
        VtArray<std::string> enumValues;
        enumValues.reserve(numericInput->mEnumValues.size());
        for (const auto& p : numericInput->mEnumValues) {
            enumValues.emplace_back(p.second);
        }
        guiWidgetData["enumValues"] = enumValues;
    } else if (numericInput->mGuiWidget == SubstanceAir::InputWidget::Input_Togglebutton ||
               numericInput->mGuiWidget == SubstanceAir::InputWidget::Input_Enumbuttons) {
        guiWidgetData["labelTrue"] = VtValue(numericInput->mLabelTrue.c_str());
        guiWidgetData["labelFalse"] = VtValue(numericInput->mLabelFalse.c_str());
    } else if (numericInput->mGuiWidget == SubstanceAir::InputWidget::Input_Angle) {
        guiWidgetData["minValue"] = VtValue(numericInput->mMinValue);
        guiWidgetData["maxValue"] = VtValue(numericInput->mMaxValue);
    } else if (numericInput->mGuiWidget == SubstanceAir::InputWidget::Input_Color) {
        guiWidgetData["spotColorInfo"] = VtValue(numericInput->mSpotColorInfo.c_str());

        if constexpr (std::is_same_v<T, SubstanceAir::Vec3Float>) {
            // Color values in USD are in linear space, but color inputs for a Substance graph are
            // (usually) in sRGB space. So we convert the default value here. Note that we do the
            // inverse transform when sending a color from USD to the engine.
            usdDefaultValue = sRGBColorToLinear(usdDefaultValue);
        }
    }

    defaultValue = VtValue(usdDefaultValue);
}

void
setupProceduralParameters(SdfAbstractData* sdfData,
                          const SdfPath& primPath,
                          const SubstanceAir::GraphDesc::Inputs& inputs,
                          SymbolMapper& symbolMapper,
                          bool isEnvironmentTexture)
{

    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("setupProceduralParameters: Set procedural parameters\n");
    for (const auto& input : inputs) {
        bool validProcParameter = true;
        VtDictionary guiWidgetData;
        SdfValueTypeName targetType = getInputSdfTypeName(input->mType, input->mGuiWidget);
        VtValue defaultValue;
        if (input->isNumerical()) {
            if (input->mType == Substance_IOType_Integer) {
                auto numericInput = dynamic_cast<const SubstanceAir::InputDescInt*>(input);
                setupNumericalInput(numericInput, defaultValue, guiWidgetData);
            } else if (input->mType == Substance_IOType_Integer2) {
                auto numericInput = dynamic_cast<const SubstanceAir::InputDescInt2*>(input);
                // handle special case of $outputsize
                if (input->mIdentifier == "$outputsize") {
                    if (isEnvironmentTexture) {
                        defaultValue =
                          GfVec2i(SBSAR_DEFAULT_RESOLUTION + 1, SBSAR_DEFAULT_RESOLUTION);
                    } else {
                        defaultValue = GfVec2i(SBSAR_DEFAULT_RESOLUTION, SBSAR_DEFAULT_RESOLUTION);
                    }
                    assert(default_resolutions.size() > 0);
                    int minRes = default_resolutions.front();
                    int maxRes = default_resolutions.back();
                    GfVec2i minValue(std::max(minRes, numericInput->mMinValue[0]),
                                     std::max(minRes, numericInput->mMinValue[1]));
                    GfVec2i maxValue(std::min(maxRes, numericInput->mMaxValue[0]),
                                     std::min(maxRes, numericInput->mMaxValue[1]));
                    guiWidgetData["minValue"] = minValue;
                    guiWidgetData["maxValue"] = maxValue;
                } else {
                    setupNumericalInput(numericInput, defaultValue, guiWidgetData);
                }
            } else if (input->mType == Substance_IOType_Integer3) {
                auto numericInput = dynamic_cast<const SubstanceAir::InputDescInt3*>(input);
                setupNumericalInput(numericInput, defaultValue, guiWidgetData);
            } else if (input->mType == Substance_IOType_Integer4) {
                auto numericInput = dynamic_cast<const SubstanceAir::InputDescInt4*>(input);
                setupNumericalInput(numericInput, defaultValue, guiWidgetData);
            } else if (input->mType == Substance_IOType_Float) {
                auto numericInput = dynamic_cast<const SubstanceAir::InputDescFloat*>(input);
                setupNumericalInput(numericInput, defaultValue, guiWidgetData);
            } else if (input->mType == Substance_IOType_Float2) {
                auto numericInput = dynamic_cast<const SubstanceAir::InputDescFloat2*>(input);
                setupNumericalInput(numericInput, defaultValue, guiWidgetData);
            } else if (input->mType == Substance_IOType_Float3) {
                auto numericInput = dynamic_cast<const SubstanceAir::InputDescFloat3*>(input);
                setupNumericalInput(numericInput, defaultValue, guiWidgetData);
            } else if (input->mType == Substance_IOType_Float4) {
                auto numericInput = dynamic_cast<const InputDescFloat4*>(input);
                setupNumericalInput(numericInput, defaultValue, guiWidgetData);
            } else {
                validProcParameter = false;
                TF_DEBUG(FILE_FORMAT_SBSAR)
                  .Msg("setupProceduralParameters: Numerical input '%s' has unsupported type\n",
                       input->mIdentifier.c_str());
            }

        } else if (input->isString()) {
            auto inputString = dynamic_cast<const SubstanceAir::InputDescString*>(input);
            defaultValue = VtValue(inputString->mDefaultValue.c_str());
        } else if (input->isImage()) {
            defaultValue = VtValue();
        } else {
            validProcParameter = false;
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("setupProceduralParameters: Unsupported input type for input %s\n",
                   input->mIdentifier.c_str());
        }

        if (validProcParameter) {
            // Special check for normal format inputs and their default values
            adjustNormalFormatInput(input->mIdentifier, input->mType, defaultValue);

            TfToken paramToken = getInputParamToken(symbolMapper, input->mIdentifier);
            SdfPath paramPath = createAttributeSpec(sdfData, primPath, paramToken, targetType);
            setAttributeDefaultValue(sdfData, paramPath, defaultValue);
            setAttributeMetadata(sdfData, paramPath, SdfFieldKeys->Custom, VtValue(true));

            bool isHidden =
              input->mIdentifier == "$outputsize" || input->mIdentifier == "$randomseed";
            if (isHidden) {
                setAttributeMetadata(sdfData, paramPath, SdfFieldKeys->Hidden, VtValue(true));
            }

            // Set general metadata
            auto stringLabel = std::string(input->mLabel);
            auto it = reserved_label_map.find(stringLabel);
            if (it != reserved_label_map.end()) {
                stringLabel = it->second;
            }
            setAttributeMetadata(
              sdfData, paramPath, SdfFieldKeys->DisplayName, VtValue(stringLabel.c_str()));
            if (!input->mGuiGroup.empty()) {
                setAttributeMetadata(sdfData,
                                     paramPath,
                                     SdfFieldKeys->DisplayGroup,
                                     VtValue(input->mGuiGroup.c_str()));
            }
            setAttributeMetadata(sdfData,
                                 paramPath,
                                 SdfFieldKeys->Documentation,
                                 VtValue(input->mGuiDescription.c_str()));

            // Set widget metadata
            guiWidgetData["widget"] = VtValue((int)input->mGuiWidget);
            guiWidgetData["visibleIf"] = VtValue(input->mGuiVisibleIf.c_str());
            guiWidgetData["userTag"] = VtValue(input->mUserTag.c_str());

            // Set procedural metadata
            VtDictionary proceduralParameters;
            proceduralParameters["uid"] = VtValue(input->mUid);
            proceduralParameters["identifier"] = VtValue(input->mIdentifier.c_str());
            if (!defaultValue.IsEmpty())
                proceduralParameters["default"] = defaultValue;
            proceduralParameters["type"] = VtValue((int)input->mType);
            proceduralParameters["guiWidgetData"] = guiWidgetData;

            setAttributeMetadata(
              sdfData, paramPath, _tokens->proceduralParameters, VtValue(proceduralParameters));
        }
    }
}

/// Convert a string from the SubstanceAir framework to a given usd type
template<typename SrcType, typename DstType = SrcType>
VtValue
convertStringToVtValue(const SubstanceAir::string& val_str)
{
    SrcType value;
    stringstream sstr(val_str);
    sstr >> value;
    if constexpr (std::is_same_v<SrcType, int> || std::is_same_v<SrcType, string> ||
                  std::is_same_v<SrcType, float>)
        return VtValue(value);
    else
        return VtValue(DstType(&value.x));
}

VtValue
convertPresetToVtValue(const Preset::InputValue& val)
{
    if (val.mType == Substance_IOType_Integer) {
        return convertStringToVtValue<int>(val.mValue);
    } else if (val.mType == Substance_IOType_Integer2) {
        return convertStringToVtValue<Vec2Int, GfVec2i>(val.mValue);
    } else if (val.mType == Substance_IOType_Integer3) {
        return convertStringToVtValue<Vec3Int, GfVec3i>(val.mValue);
    } else if (val.mType == Substance_IOType_Integer4) {
        return convertStringToVtValue<Vec4Int, GfVec4i>(val.mValue);
    } else if (val.mType == Substance_IOType_Float) {
        return convertStringToVtValue<float>(val.mValue);
    } else if (val.mType == Substance_IOType_Float2) {
        return convertStringToVtValue<Vec2Float, GfVec2f>(val.mValue);
    } else if (val.mType == Substance_IOType_Float3) {
        return convertStringToVtValue<Vec3Float, GfVec3f>(val.mValue);
    } else if (val.mType == Substance_IOType_Float4) {
        return convertStringToVtValue<Vec4Float, GfVec4f>(val.mValue);
    } else if (val.mType == Substance_IOType_String) {
        return convertStringToVtValue<string>(val.mValue);
    } else {
        TF_DEBUG(FILE_FORMAT_SBSAR)
          .Msg("Preset input parameter is of unsupported type: %s\n", val.mIdentifier.c_str());
    }

    return VtValue();
}

void
addPresetVariant(SdfAbstractData* sdfData,
                 SymbolMapper& symbolMapper,
                 const SubstanceAir::GraphDesc& graphDesc,
                 const std::string& packagePath,
                 const SdfPath& primPath,
                 const SdfPath& targetPrimPath)
{
    if (graphDesc.mPresets.empty()) {
        addPayload(sdfData, packagePath, primPath, targetPrimPath, 1);
        return;
    }

    SdfPath presetVSPath = createVariantSetSpec(sdfData, primPath, _tokens->preset);

    // Add default preset
    {
        SdfPath presetVariantPath =
          createVariantSpec(sdfData, presetVSPath, _tokens->defaultPreset);
        addPayload(sdfData, packagePath, presetVariantPath, targetPrimPath, 1);

        addVariantSelection(sdfData, primPath, _tokens->preset, _tokens->defaultPreset);
    }

    // Build a map from input UID to input descriptor
    // This is used below to map a preset input value to its original input description
    std::unordered_map<SubstanceAir::UInt, const SubstanceAir::InputDescBase*> inputUidToInputDesc;
    for (const InputDescBase* input : graphDesc.mInputs) {
        inputUidToInputDesc[input->mUid] = input;
    }

    for (const auto& preset : graphDesc.mPresets) {
        MappedSymbol presetName = symbolMapper.GetSymbol(preset.mLabel.c_str());
        TfToken presetToken(presetName.usdName);
        SdfPath presetVariantPath = createVariantSpec(sdfData, presetVSPath, presetToken);
        TF_DEBUG(FILE_FORMAT_SBSAR)
          .Msg("SDF:Write preset variant: %s, %zu input values\n",
               presetName.usdName.c_str(),
               preset.mInputValues.size());

        for (const SubstanceAir::Preset::InputValue& val : preset.mInputValues) {
            // Remove resolution
            if (val.mIdentifier.compare("$outputsize") == 0)
                continue;
            // Skip degenerate preset inputs
            if (val.mIdentifier.empty())
                continue;

            // Find the original input description, so that we can look-up the widget hint
            const SubstanceAir::InputDescBase* inputDesc = nullptr;
            const auto it = inputUidToInputDesc.find(val.mUid);
            if (it != inputUidToInputDesc.end()) {
                inputDesc = it->second;
            } else {
                TF_WARN(
                  "Couldn't find input for preset input %s/%u", val.mIdentifier.c_str(), val.mUid);
                continue;
            }

            SdfValueTypeName targetType = getInputSdfTypeName(val.mType, inputDesc->mGuiWidget);
            VtValue targetValue = convertPresetToVtValue(val);
            if (targetValue.IsEmpty()) {
                continue;
            }

            // Special check for normal format inputs and their values within a preset
            adjustNormalFormatInput(inputDesc->mIdentifier, inputDesc->mType, targetValue);

            if (inputDesc->mGuiWidget == SubstanceAir::InputWidget::Input_Color) {
                convertColorSRGBToLinear(targetValue);
            }

            TfToken paramToken = getInputParamToken(symbolMapper, val.mIdentifier);
            SdfPath paramPath =
              createAttributeSpec(sdfData, presetVariantPath, paramToken, targetType);
            setAttributeDefaultValue(sdfData, paramPath, targetValue);
            setAttributeMetadata(sdfData, paramPath, SdfFieldKeys->Custom, VtValue(true));
        }

        addPayload(sdfData, packagePath, presetVariantPath, targetPrimPath, 1);
    }
}

void
addResolutionVariantSet(SdfAbstractData* sdfData,
                        SymbolMapper& symbolMapper,
                        const SubstanceAir::GraphDesc& graphDesc,
                        const std::string& packagePath,
                        const SdfPath& primPath,
                        const SdfPath& targetPrimPath,
                        bool isEnvironmentTexture)
{
    SdfPath resolutionVSPath = createVariantSetSpec(sdfData, primPath, _tokens->resolution);

    for (auto res : default_resolutions) {
        int xres = isEnvironmentTexture ? res + 1 : res;
        int yres = res;
        std::string resolutionName = getResolutionVariantName(xres, yres);
        TfToken resVariantName(resolutionName);
        SdfPath resVariantPath = createVariantSpec(sdfData, resolutionVSPath, resVariantName);

        /// Set the outputsize parameter according to the resolution variant
        const TfToken paramToken = getInputParamToken(symbolMapper, std::string("$outputsize"));
        SdfPath paramPath =
          createAttributeSpec(sdfData, resVariantPath, paramToken, SdfValueTypeNames->Int2);
        setAttributeDefaultValue(sdfData, paramPath, GfVec2i(xres, yres));
        setAttributeMetadata(sdfData, paramPath, SdfFieldKeys->Custom, VtValue(true));

        addPresetVariant(
          sdfData, symbolMapper, graphDesc, packagePath, resVariantPath, targetPrimPath);
    }
}

void
addResolutionVariantSelection(SdfAbstractData* sdfData,
                              const SdfPath& primPath,
                              bool isEnvironmentTexture,
                              int resolution)
{
    const int xRes = isEnvironmentTexture ? resolution + 1 : resolution;
    const int yRes = resolution;
    TfToken resolutionVariant = TfToken(getResolutionVariantName(xRes, yRes));
    addVariantSelection(sdfData, primPath, _tokens->resolution, resolutionVariant);
}

void
addPayload(SdfAbstractData* sdfData,
           const std::string& packagePath,
           const SdfPath& primPath,
           const SdfPath& targetPrimPath,
           std::uint32_t depth)
{
    SdfLayer::FileFormatArguments arguments = { { "depth", std::to_string(depth) } };
    std::string assetPath = SdfLayer::CreateIdentifier(packagePath, arguments);

    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("SDF:Write payload: %s, %s %s\n",
           primPath.GetText(),
           assetPath.c_str(),
           targetPrimPath.GetText());
    addPrimPayload(sdfData, primPath, SdfPayload(assetPath, targetPrimPath));
}
}
