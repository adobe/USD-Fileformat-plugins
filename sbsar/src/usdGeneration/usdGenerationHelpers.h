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
#pragma once

#include "sbsarSymbolMapper.h"
#include <api.h>

#include <substance/framework/framework.h>

#include <pxr/base/js/json.h>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/valueTypeName.h>

namespace adobe::usd::sbsar {

static constexpr int SBSAR_DEFAULT_RESOLUTION = 9;
struct DefaultChannel
{
    PXR_NS::SdfValueTypeName type;
    PXR_NS::VtValue value;
    std::pair<PXR_NS::VtValue, PXR_NS::VtValue> range;
};
extern const std::vector<std::string> mapped_usages;
extern const std::vector<std::string> uniform_usages;
extern const std::vector<int> default_resolutions;
extern const std::map<std::string, DefaultChannel> default_channels;

extern const std::string uv_scale_input;
extern const std::string uv_rotation_input;
extern const std::string uv_translation_input;

extern const std::string proceduralParameterPrefix;

USDSBSAR_API std::string
getResolutionVariantName(size_t xResLog2, size_t yResLog2);

enum class GraphType
{
    Material = 0,
    Light = 1,
    Unknown = 2
};

GraphType
guessGraphType(const SubstanceAir::GraphDesc& graphDesc);

std::pair<std::string, std::string>
getDefaultValueNames(const std::string& channelName);
std::string
getGraphName(const SubstanceAir::GraphDesc& desc);
bool
hasUsage(const std::string& usage, const SubstanceAir::GraphDesc& graphDesc);

USDSBSAR_API PXR_NS::JsValue
convertSbsarParamters(const PXR_NS::VtDictionary& sbsarParmeters);

//! \brief Generat a texture path.
//! An sbsar info path has several parts and look like this:
//!  Path[Graph?Usage=xxx#Hash=xxx#params={"name:value","name:value"}]
//! - Path : Path to the .sbsar file (Not set in this function)
//! - Graph: Graph Name
//! - Usage: The output texture
//! - Hash: Hash of the .sbsar
//! - Parameters: Parameters to send to the sbsar to generate the texture.
//! And this function setup the part between the [].
//! \param usage     Output texture
//! \param graphName Graph name in the sbsar
//! \param sbsarHash Hash of the sbsar.
//! \param params    Sbsar parameters used to generate texture asset
//! path.
//! \return
USDSBSAR_API std::string
generateSbsarInfoPath(const std::string& usage,
                      const MappedSymbol& graphName,
                      std::size_t sbsarHash,
                      const PXR_NS::JsValue& params);

std::string
getTextureAssetName(const std::string& usage);

MappedSymbol
getGraphCategory(const SubstanceAir::GraphDesc& graphDesc, SymbolMapper& symbolMapper);

void
setGraphMetadataOnPrim(PXR_NS::SdfAbstractData* sdfData,
                       const PXR_NS::SdfPath& primPath,
                       const SubstanceAir::GraphDesc& graphDesc);

PXR_NS::TfToken
getInputParamToken(SymbolMapper& symbolMapper, const std::string& substanceInputName);

//! \brief Setup procedural paramters as default attribut of the prim.
//! Each parameters is set with the default value in the graph
//! and meta data are added : Identified, label, min/max threshold, etc...
//! \param sdfData              Sdf data to store the layer in.
//! \param primPath             Path of the prim to setup.
//! \param inputs               All public input of the current sbsar graph.
//! \param symbolMapper         Symbol mapper to avoid conflict between parameters.
//! \param isEnvironmentTexture Bool that indicates the graph produces an environment texture
void
setupProceduralParameters(PXR_NS::SdfAbstractData* sdfData,
                          const PXR_NS::SdfPath& primPath,
                          const SubstanceAir::GraphDesc::Inputs& inputs,
                          SymbolMapper& symbolMapper,
                          bool isEnvironmentTexture = false);

//! \brief Add the preset variant to control preset parameters. Create one variant value per preset.
//! \param sdfData          Sdf data to store the layer in.
//! \param symbolMapper     Symbol mapper to avoid conflict between parameters.
//! \param graphDesc        Description of the current sbsar graph.
//! \param packagePath      Path of the sbsar file.
//! \param primPath         Path to add the variant to.
//! \param targetPrimPath   Path in the payload that should be pulled in.
void
addPresetVariant(PXR_NS::SdfAbstractData* sdfData,
                 SymbolMapper& symbolMapper,
                 const SubstanceAir::GraphDesc& graphDesc,
                 const std::string& packagePath,
                 const PXR_NS::SdfPath& primPath,
                 const PXR_NS::SdfPath& targetPrimPath);

//! \brief Add resolution variant to control the outputsize parameters with explicit value.
//! \param sdfData              Sdf data to store the layer in.
//! \param symbolMapper         Symbol mapper to avoid conflict between parameters.
//! \param packagePath          Path of the sbsar file.
//! \param primPath             Path to add the variant to.
//! \param targetPrimPath       Path in the payload that should be pulled in.
//! \param isEnvironmentTexture Bool that indicates the graph produces an environment texture
void
addResolutionVariant(PXR_NS::SdfAbstractData* sdfData,
                     SymbolMapper& symbolMapper,
                     const SubstanceAir::GraphDesc& graphDesc,
                     const std::string& packagePath,
                     const PXR_NS::SdfPath& primPath,
                     const PXR_NS::SdfPath& targetPrimPath,
                     bool isEnvironmentTexture = false);

//! \brief Add payload arc to a prim to reference this package again with different parameters
//! \param sdfData          Sdf data to store the layer in.
//! \param packagePath      Path of the sbsar file.
//! \param primPath         Path to add the payload to.
//! \param targetPrimPath   Path in the payload that should be pulled in.
//! \param depth            Recursion depth for the file format plugin
void
addPayload(PXR_NS::SdfAbstractData* sdfData,
           const std::string& packagePath,
           const PXR_NS::SdfPath& primPath,
           const PXR_NS::SdfPath& targetPrimPath,
           std::uint32_t depth);

}
