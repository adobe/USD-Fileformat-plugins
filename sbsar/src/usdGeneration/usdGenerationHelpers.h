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

/// Default resolution level for SBSAR textures (log2 scale, where 9 = 512x512)
static constexpr int SBSAR_DEFAULT_RESOLUTION = 9;

/// @brief Represents default properties for a texture channel.
///
/// This structure encapsulates the default type, value, and valid range
/// for a specific texture channel in SBSAR materials.
struct DefaultChannel
{
    PXR_NS::SdfValueTypeName type;                     ///< USD value type for this channel
    PXR_NS::VtValue value;                             ///< Default value for the channel
    std::pair<PXR_NS::VtValue, PXR_NS::VtValue> range; ///< Valid range (min,max) for the channel
};

/// List of SBSAR channel usages that have a known mapping
extern const std::vector<std::string> mapped_usages;

/// List of SBSAR channel usages that should use uniform values
extern const std::vector<std::string> uniform_usages;

/// List of SBSAR channel usages that represent normal maps
extern const std::vector<std::string> normal_usages;

/// Default resolution values for different quality levels
extern const std::vector<int> default_resolutions;

/// Mapping of channel names to their default properties
extern const std::map<std::string, DefaultChannel> default_channels;

/// Input parameter name for UV scale transformation
extern const std::string uv_scale_input;

/// Input parameter name for UV scale inverse transformation.  This parameter is only used for
/// OpenPBR networks since the Place2D works differently than used in the other ASM and
/// UsdPreviewSurface networks.
extern const std::string uv_scale_inverse_input;

/// Input parameter name for UV rotation transformation
extern const std::string uv_rotation_input;

/// Input parameter name for UV translation transformation
extern const std::string uv_translation_input;

/// Name used for UV channel attributes
extern const std::string uv_channel_name;

/// Name used for UV wrap mode in S direction
extern const std::string uv_wrap_s_name;

/// Name used for UV wrap mode in T direction
extern const std::string uv_wrap_t_name;

/// Prefix used for procedural parameter attribute names
extern const std::string proceduralParameterPrefix;

/// @brief Generate a variant name for a specific resolution.
/// @param xResLog2 X-axis resolution as log2 value (e.g., 9 for 512)
/// @param yResLog2 Y-axis resolution as log2 value (e.g., 9 for 512)
/// @return String representation of the resolution variant name
USDSBSAR_API std::string
getResolutionVariantName(size_t xResLog2, size_t yResLog2);

/// @brief Enumeration of different graph types in SBSAR files.
///
/// SBSAR files can contain different types of graphs that produce
/// different kinds of outputs (materials, lights, etc.).
enum class GraphType
{
    Material = 0, ///< Graph represents a material with multiple channels
    Light = 1,    ///< Graph represents an environment light with an IBL texture
    Unknown = 2   ///< Graph type could not be determined
};

/// @brief Determine the type of graph based on its description.
/// @param graphDesc Description of the SBSAR graph to analyze
/// @return GraphType indicating the detected graph type
GraphType
guessGraphType(const SubstanceAir::GraphDesc& graphDesc);

/// @brief Convert GraphType enum to string representation.
/// @param type GraphType to convert
/// @return String representation ("material", "light/environment", or "unknown")
USDSBSAR_API std::string
graphTypeToString(GraphType type);

/// @brief Generate a human-readable description of a collection of graph types.
/// @param types Vector of GraphType values to describe
/// @return String like "2 material graph(s), 1 light graph(s)"
USDSBSAR_API std::string
describeGraphTypes(const std::vector<GraphType>& types);

/// @brief Get the default value attribute names for a given channel.
/// @param channelName Name of the texture channel
/// @return Pair of strings representing the default value attribute name and the texture influence
/// attribute name
std::pair<std::string, std::string>
getDefaultValueNames(const std::string& channelName);

/// @brief Extract the graph name from a graph description.
/// @param desc Graph description to extract name from
/// @return String containing the graph name
std::string
getGraphName(const SubstanceAir::GraphDesc& desc);

/// @brief Check if a graph has an output channel with the specified usage name.
/// @param usage Output usage to check for
/// @param graphDesc Graph description to search in
/// @return True if the graph has the specified usage output
bool
hasUsage(const std::string& usage, const SubstanceAir::GraphDesc& graphDesc);

/// @brief Check if a graph has an input parameter with the specified identifier.
/// @param identifier Input parameter identifier to check for
/// @param graphDesc Graph description to search in
/// @return True if the graph has the specified input parameter
bool
hasInput(const std::string& identifier, const SubstanceAir::GraphDesc& graphDesc);

/// @brief Determine if a usage string represents a normal map
/// @param usage Usage string to check
/// @return True if the usage represents a normal map
bool
isNormal(const std::string& usage);

/// @brief Determine if a usage string represents a color output
/// @param usage Usage string to check
/// @return True if the usage represents a color
///
/// This is useful to choose the right color space
bool
isColorUsage(const std::string& usage);

/// @brief Convert SBSAR parameters from VtDictionary to JsValue format.
/// @param sbsarParmeters Dictionary of SBSAR parameters to convert
/// @return JsValue containing the converted parameters
USDSBSAR_API PXR_NS::JsValue
convertSbsarParameters(const PXR_NS::VtDictionary& sbsarParmeters);

/// @brief Convert color values from linear to sRGB color space.
/// @param value Color value as GfVec3f to convert (modified in place)
void
convertColorLinearToSRGB(PXR_NS::VtValue& value);

/// @brief Convert color values from sRGB to linear color space.
/// @param value Color value as GfVec3fto convert (modified in place)
void
convertColorSRGBToLinear(PXR_NS::VtValue& value);

/// Returns the name of the scale and bias interface attributes for a given normal channel.
std::pair<std::string, std::string>
getNormalMapScaleAndBiasNames(const std::string& channelName);

/// @brief Enumeration of normal map formats.
///
/// Different rendering engines use different conventions for normal maps,
/// particularly regarding the Y-axis direction.
enum class NormalFormat
{
    Unknown, ///< Normal format could not be determined
    DirectX, ///< DirectX-style normal maps (Y-axis down)
    OpenGL   ///< OpenGL-style normal maps (Y-axis up)
};

/// Default normal format used for USD output (OpenGL convention)
extern const NormalFormat defaultNormalFormat;

/// @brief Apply the default normal format to parameters.
///
/// This function applies the default normal format (OpenGL) to the jsParams to ensure the Substance
/// engine is using OpenGL, if the SBSAR has a standard input parameter for that. Adding this to the
/// JsParams ensures texture paths generate the right normal maps.
///
/// @param graphDesc Description of the SBSAR graph
/// @param jsParams Current parameters to modify
/// @return Modified JsValue with default normal format applied
PXR_NS::JsValue
applyDefaultNormalFormatInput(const SubstanceAir::GraphDesc& graphDesc,
                              const PXR_NS::JsValue& jsParams);

/// @brief Determine the default normal format for a graph.
///
/// This function checks if the graph supports the "normal_format" input parameter.
/// If it does, it returns the defaultNormalFormat. If the graph doesn't support
/// that input, it assumes DirectX-style normal maps.
///
/// @param graphDesc Description of the SBSAR graph to check
/// @return NormalFormat indicating the default format for this graph
NormalFormat
getDefaultNormalFormat(const SubstanceAir::GraphDesc& graphDesc);

/// @brief Determine normal format from current parameters.
///
/// This function looks for the "normal_format" parameter in the current parameters.
/// Not all SBSAR files have this parameter, but all Substance Source materials do.
/// When available, it can be used to determine the normal format being generated.
///
/// @param jsParams Current parameters to examine
/// @return NormalFormat determined from the parameters
NormalFormat
determineNormalFormat(const PXR_NS::JsValue& jsParams);

/// @brief Get scale and bias values for normal map texture readers.
///
/// Returns the appropriate scale and bias values for a texture reader
/// based on the normal map format being used.
///
/// @param normalFormat The normal map format to get values for
/// @return Pair of GfVec4f values (scale, bias) for texture reading
std::pair<PXR_NS::GfVec4f, PXR_NS::GfVec4f>
getNormalMapScaleAndBias(NormalFormat normalFormat);

/// @brief Generate a texture path.
/// An sbsar info path has several parts and look like this:
///  Path[Graph?Usage=xxx#Hash=xxx#params={"name:value","name:value"}]
/// - Path : Path to the .sbsar file (Not set in this function)
/// - Graph: Graph Name
/// - Usage: The output texture
/// - Hash: Hash of the .sbsar
/// - Parameters: Parameters to send to the sbsar to generate the texture.
/// And this function setup the part between the [].
/// @param usage     Output texture
/// @param graphName Graph name in the sbsar
/// @param sbsarHash Hash of the sbsar.
/// @param params    Sbsar parameters used to generate texture asset
/// path.
/// @return String containing the generated SBSAR info path
USDSBSAR_API std::string
generateSbsarInfoPath(const std::string& usage,
                      const MappedSymbol& graphName,
                      std::size_t sbsarHash,
                      const PXR_NS::JsValue& params);

/// @brief Generate an asset name for a texture based on its usage.
/// @param usage The usage string for the texture
/// @return String representing the asset name
std::string
getTextureAssetName(const std::string& usage);

/// @brief Get the category of a graph using symbol mapping.
/// @param graphDesc Description of the SBSAR graph
/// @param symbolMapper Symbol mapper for name resolution
/// @return MappedSymbol representing the graph category
MappedSymbol
getGraphCategory(const SubstanceAir::GraphDesc& graphDesc, SymbolMapper& symbolMapper);

/// @brief Set graph metadata on a USD primitive.
///
/// This function adds metadata from the SBSAR graph description
/// to the specified USD primitive.
///
/// @param sdfData SDF data container to modify
/// @param primPath Path to the primitive to add metadata to
/// @param graphDesc Graph description containing the metadata
void
setGraphMetadataOnPrim(PXR_NS::SdfAbstractData* sdfData,
                       const PXR_NS::SdfPath& primPath,
                       const SubstanceAir::GraphDesc& graphDesc);

/// @brief Generate a USD token for a Substance input parameter.
/// @param symbolMapper Symbol mapper for name resolution
/// @param substanceInputName Name of the Substance input parameter
/// @return TfToken representing the USD parameter name
PXR_NS::TfToken
getInputParamToken(SymbolMapper& symbolMapper, const std::string& substanceInputName);

/// @brief Set up procedural parameters as default attributes of the primitive.
///
/// Each parameter is set with the default value from the graph, and metadata
/// is added including identifier, label, min/max thresholds, etc.
///
/// @param sdfData              SDF data container to store the layer in
/// @param primPath             Path of the primitive to set up
/// @param inputs               All public inputs of the current SBSAR graph
/// @param symbolMapper         Symbol mapper to avoid conflicts between parameters
/// @param isEnvironmentTexture Bool indicating if the graph produces an environment texture
void
setupProceduralParameters(PXR_NS::SdfAbstractData* sdfData,
                          const PXR_NS::SdfPath& primPath,
                          const SubstanceAir::GraphDesc::Inputs& inputs,
                          SymbolMapper& symbolMapper,
                          bool isEnvironmentTexture = false);

/// @brief Add preset variant to control preset parameters.
///
/// Creates one variant value per preset defined in the SBSAR graph.
///
/// @param sdfData          SDF data container to store the layer in
/// @param symbolMapper     Symbol mapper to avoid conflicts between parameters
/// @param graphDesc        Description of the current SBSAR graph
/// @param packagePath      Path of the SBSAR file
/// @param primPath         Path to add the variant to
/// @param targetPrimPath   Path in the payload that should be pulled in
void
addPresetVariant(PXR_NS::SdfAbstractData* sdfData,
                 SymbolMapper& symbolMapper,
                 const SubstanceAir::GraphDesc& graphDesc,
                 const std::string& packagePath,
                 const PXR_NS::SdfPath& primPath,
                 const PXR_NS::SdfPath& targetPrimPath);

/// @brief Add resolution variant set to control output size parameters.
///
/// Creates variant set with explicit resolution values to control texture output sizes.
///
/// @param sdfData              SDF data container to store the layer in
/// @param symbolMapper         Symbol mapper to avoid conflicts between parameters
/// @param graphDesc            Description of the current SBSAR graph
/// @param packagePath          Path of the SBSAR file
/// @param primPath             Path to add the variant to
/// @param targetPrimPath       Path in the payload that should be pulled in
/// @param isEnvironmentTexture Bool indicating if the graph produces an environment texture
void
addResolutionVariantSet(PXR_NS::SdfAbstractData* sdfData,
                        SymbolMapper& symbolMapper,
                        const SubstanceAir::GraphDesc& graphDesc,
                        const std::string& packagePath,
                        const PXR_NS::SdfPath& primPath,
                        const PXR_NS::SdfPath& targetPrimPath,
                        bool isEnvironmentTexture = false);

/// @brief Add resolution variant selection to control output size parameters.
///
/// Sets the default resolution variant choice for the primitive.
///
/// @param sdfData              SDF data container to store the layer in
/// @param primPath             Path to add the variant choice to
/// @param isEnvironmentTexture Bool indicating whether the graph produces an environment texture
/// @param resolution           Default resolution level to select
void
addResolutionVariantSelection(PXR_NS::SdfAbstractData* sdfData,
                              const PXR_NS::SdfPath& primPath,
                              bool isEnvironmentTexture = false,
                              int resolution = SBSAR_DEFAULT_RESOLUTION);

/// @brief Add payload arc to a primitive to reference the package with different parameters.
///
/// Creates a payload reference that allows the same SBSAR package to be loaded
/// with different parameter configurations.
///
/// @param sdfData          SDF data container to store the layer in
/// @param packagePath      Path of the SBSAR file
/// @param primPath         Path to add the payload to
/// @param targetPrimPath   Path in the payload that should be pulled in
/// @param depth            Recursion depth for the file format plugin
void
addPayload(PXR_NS::SdfAbstractData* sdfData,
           const std::string& packagePath,
           const PXR_NS::SdfPath& primPath,
           const PXR_NS::SdfPath& targetPrimPath,
           std::uint32_t depth);

}
