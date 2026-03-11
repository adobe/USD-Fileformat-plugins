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
#include "sbsarfileformat.h"
#include "sbsarDebug.h"

#include <assetPath/assetPathParser.h>
#include <sbsarEngine/sbsarInputImageCache.h>
#include <sbsarEngine/sbsarPackageCache.h>
#include <usdGeneration/dictEncoder.h>
#include <usdGeneration/sbsarLuxDomeLight.h>
#include <usdGeneration/sbsarMaterial.h>
#include <usdGeneration/usdGenerationHelpers.h>

#include <substance/framework/framework.h>
#include <substance/framework/graph.h>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/pcp/dynamicFileFormatContext.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdMedia/tokens.h>

// File format utils
#include <fileformatutils/common.h>
#include <fileformatutils/sdfUtils.h>

#include <filesystem>
#include <fstream>
#include <iomanip>

using namespace adobe::usd;
using namespace adobe::usd::sbsar;

PXR_NAMESPACE_OPEN_SCOPE
TF_DEFINE_PUBLIC_TOKENS(SBSARFileFormatTokens, SBSAR_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(SBSARFileFormat, SdfFileFormat);
}

SBSARFileFormat::SBSARFileFormat()
  : SdfFileFormat(SBSARFileFormatTokens->Id,        // formatId
                  SBSARFileFormatTokens->Version,   // versionString
                  SBSARFileFormatTokens->Target,    // target
                  SBSARFileFormatTokens->Extension) // extension
{}

SBSARFileFormat::~SBSARFileFormat() = default;

bool
SBSARFileFormat::CanRead(const std::string& file) const
{
    return true;
}

bool
SBSARFileFormat::IsPackage() const
{
    return true;
}

std::string
SBSARFileFormat::GetPackageRootLayerPath(const std::string& resolvedPath) const
{
    return "";
}

//! Check if the given graph has the same name as the package.
bool
stringsMatchIgnoreCase(const std::string& packageName, const std::string& graphName)
{
    if (packageName.size() != graphName.size())
        return false;
    return std::equal(
      packageName.begin(),
      packageName.end(),
      graphName.begin(),
      [](unsigned char l, unsigned char r) { return std::tolower(l) == std::tolower(r); });
}

//! Validate graph type filter and check if package contains matching graphs.
//! Returns the expected GraphType and whether to continue processing.
std::pair<GraphType, bool>
validateGraphTypeFilter(const std::string& graphTypeFilter,
                        const SubstanceAir::PackageDesc* packageDesc,
                        const std::string& packageName)
{
    GraphType expectedType = GraphType::Unknown;

    // Parse and validate the filter value
    if (!graphTypeFilter.empty()) {
        if (graphTypeFilter == "material") {
            expectedType = GraphType::Material;
        } else if (graphTypeFilter == "light") {
            expectedType = GraphType::Light;
        } else {
            TF_WARN(
              "SBSAR: Invalid graphTypeFilter value '%s'. Valid values are 'material' or 'light'.",
              graphTypeFilter.c_str());
        }
    }

    // If no valid filter, continue processing
    if (expectedType == GraphType::Unknown) {
        return { expectedType, true };
    }

    // Pre-scan graphs to validate against filter
    std::vector<GraphType> foundTypes;
    bool hasMatchingGraph = false;

    for (const SubstanceAir::GraphDesc& graphDesc : packageDesc->getGraphs()) {
        GraphType graphType = guessGraphType(graphDesc);
        foundTypes.push_back(graphType);
        if (graphType == expectedType) {
            hasMatchingGraph = true;
        }
    }

    if (!hasMatchingGraph) {
        // Generate helpful error message
        std::string foundTypesStr = describeGraphTypes(foundTypes);
        std::string expectedTypeStr = graphTypeToString(expectedType);

        TF_RUNTIME_ERROR("SBSAR package '%s' does not contain any %s graphs. "
                         "Package contains: %s. "
                         "This SBSAR file cannot be used in this context.",
                         packageName.c_str(),
                         expectedTypeStr.c_str(),
                         foundTypesStr.c_str());
        return { expectedType, false };
    }

    return { expectedType, true };
}

bool
SBSARFileFormat::CreateLayerData(const SdfAbstractDataRefPtr& sdfDataPtr,
                                 const std::string& resolvedPath,
                                 const SBSAROptions& sbsarData)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("SBSARFileFormat:Read sbsar: %s\n", resolvedPath.c_str());
    using namespace adobe::usd;

    // Get the raw pointer for efficiency while we hold the ref pointer
    SdfAbstractData* sdfData = get_pointer(sdfDataPtr);

    createPseudoRootSpec(sdfData);

    SymbolMapper symbolMapper;
    // Open SBSAR file
    size_t sbsarHash = 0;
    auto packageDesc = getSbsarFromPackageCache(resolvedPath, &sbsarHash);
    if (!packageDesc || !packageDesc->isValid()) {
        TF_RUNTIME_ERROR("Failed to read sbsar package %s", resolvedPath.c_str());
        return false;
    }
    std::string packageName = TfStringGetBeforeSuffix(TfGetBaseName(resolvedPath));

    // Validate graph types if filter is set
    auto [expectedType, shouldContinue] =
      validateGraphTypeFilter(sbsarData.graphTypeFilter, packageDesc.get(), packageName);
    if (!shouldContinue) {
        return false;
    }

    // Create all prim
    SdfPath defaultPrimPath;
    // Create a class prim for the materials in the package
    SdfPath classPath;

    for (const SubstanceAir::GraphDesc& graphDesc : packageDesc->getGraphs()) {
        TF_DEBUG(FILE_FORMAT_SBSAR)
          .Msg("SBSARFileFormat:Read graph: %s\n", graphDesc.mLabel.c_str());

        const MappedSymbol graphName = symbolMapper.GetSymbol(getGraphName(graphDesc));
        GraphType graphType = guessGraphType(graphDesc);

        // Skip graphs that don't match filter
        if (expectedType != GraphType::Unknown && graphType != expectedType) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("SBSARFileFormat: Skipping graph %s (type %s, expected %s)\n",
                   graphName.usdName.c_str(),
                   graphTypeToString(graphType).c_str(),
                   graphTypeToString(expectedType).c_str());
            continue;
        }

        SdfPath primPath;

        if (graphType == GraphType::Material) {
            if (classPath.IsEmpty()) {
                classPath = addClassPrim(sdfData, TfToken("_class_sbsarMaterial"));

                // Mark class prim as active=false, so that it is discarded when the stage is
                // flattened
                setPrimMetadata(sdfData, classPath, SdfFieldKeys->Active, VtValue(false));
            }

            primPath = addMaterialPrim(sdfData,
                                       graphName,
                                       graphDesc,
                                       resolvedPath,
                                       classPath,
                                       sbsarHash,
                                       symbolMapper,
                                       sbsarData);
        } else if (graphType == GraphType::Light) {
            primPath = addLuxDomeLight(
              sdfData, graphName, graphDesc, resolvedPath, sbsarHash, symbolMapper, sbsarData);
        }

        if (graphDesc.mThumbnail.size() > 0 && primPath.IsEmpty() == false) {
            SdfAssetPath thumbnailPath(resolvedPath + "[thumbnails/" + graphName.usdName + ".png]");
            VtDictionary thumbnails;
            thumbnails[UsdMediaTokens->defaultImage] = thumbnailPath;
            sdfData->SetDictValueByKey(primPath,
                                       SdfFieldKeys->AssetInfo,
                                       UsdMediaTokens->previewThumbnailsDefault,
                                       VtValue(thumbnails));
            prependApiSchema(sdfData, primPath, UsdMediaTokens->AssetPreviewsAPI);
        }

        if (defaultPrimPath.IsEmpty() || stringsMatchIgnoreCase(packageName, graphName.usdName))
            defaultPrimPath = primPath;
    }
    setLayerMetadata(sdfData, SdfFieldKeys->DefaultPrim, VtValue(defaultPrimPath.GetNameToken()));
    return true;
}

SBSAROptions
parseFileFormatArguments(const SBSARFileFormat::FileFormatArguments& args)
{
    SBSAROptions data;
    auto sbsarParameters = args.find("sbsarParameters");
    if (sbsarParameters != args.end()) {
        std::stringstream param_stream(sbsarParameters->second);
        data.sbsarParameters = DictEncoder::readDict(param_stream);
    }

    auto depth = args.find("depth");
    if (depth != args.end()) {
        data.depth = std::stoi(depth->second);
    } else {
        data.depth = 0;
    }

    argReadBool(args, "writeUsdPreviewSurface", data.writeUsdPreviewSurface, "SBSAR");
    argReadBool(args, "writeASM", data.writeASM, "SBSAR");
    argReadBool(args, "writeOpenPBR", data.writeOpenPBR, "SBSAR");
    argReadBool(args, "preserveExtraMaterialInfo", data.preserveExtraMaterialInfo, "SBSAR");

    // Parse graphTypeFilter
    argReadString(args, "graphTypeFilter", data.graphTypeFilter, "SBSAR");

    return data;
}

/// Resolve an image asset path authored on a parameter of an SBSAR file.
///
/// Resolution strategy:
/// 1. Use SdfAssetPath::GetResolvedPath() if available (USD-correct).
/// 2. Resolve package-relative paths if the SBSAR itself is packaged (USDZ).
/// 3. Accept absolute filesystem paths directly.
/// 4. Apply BEST-EFFORT filesystem heuristics:
///    - relative to the SBSAR file
///    - relative to current working directory (CWD)
///
/// IMPORTANT:
/// Anchor-relative paths (./foo.png) cannot be resolved correctly without
/// the authoring layer context.
/// XXX : Heuristic fallbacks are NOT guaranteed
/// to be correct and should be removed once USD exposes the layer context.
///
/// \return empty string on failure.
std::string
resolveSbsarImageInputAssetPath(const SdfAssetPath& imageAssetPath,
                                const std::string& resolvedSbsarPath,
                                const std::string& parameterName)
{
    std::string resolvedPath;
    // 1. Prefer USD-resolved path if already available
    if (!imageAssetPath.GetResolvedPath().empty()) {
        resolvedPath = imageAssetPath.GetResolvedPath();
        TF_DEBUG(FILE_FORMAT_SBSAR)
          .Msg("SBSAR: Using pre-resolved image path: %s\n", resolvedPath.c_str());
        return resolvedPath;
    }
    const std::string& imagePath = imageAssetPath.GetAssetPath();
    // 2. Reject unsupported package-relative image paths
    if (ArIsPackageRelativePath(imagePath)) {
        TF_WARN("SBSAR: Image path '%s' for parameter '%s' is package-relative "
                "and cannot be resolved without authoring layer context.",
                imagePath.c_str(),
                parameterName.c_str());
        return {};
    }
    // 3. SBSAR inside a package (USDZ)
    if (ArIsPackageRelativePath(resolvedSbsarPath)) {
        const std::string& packageAnchor = ArSplitPackageRelativePathOuter(resolvedSbsarPath).first;
        std::string imageIdentifier = ArJoinPackageRelativePath(packageAnchor, imagePath);
        if (std::string resolved = ArGetResolver().Resolve(imageIdentifier); !resolved.empty()) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("SBSAR: Resolved image inside package: %s\n", resolved.c_str());
            return resolved;
        }
        TF_WARN("SBSAR: Failed to resolve image '%s' relative to packaged SBSAR '%s'",
                imagePath.c_str(),
                resolvedSbsarPath.c_str());
        return {};
    }
    // 4. Absolute filesystem paths are accepted directly
    try {
        std::filesystem::path fsImagePath(imagePath);
        if (fsImagePath.is_absolute()) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("SBSAR: Using absolute image path: %s\n", imagePath.c_str());
            return imagePath;
        }
    } catch (const std::exception& e) {
        TF_WARN(
          "SBSAR: Image path '%s' is not a valid filesystem path: %s", imagePath.c_str(), e.what());
        return {};
    }
    // 5. HEURISTIC FALLBACKS (best-effort, NOT USD-correct)
    // 5.1 Relative to SBSAR file location
    {
        std::filesystem::path sbsarPath(resolvedSbsarPath);
        std::filesystem::path baseDir = sbsarPath.parent_path();
        std::filesystem::path candidate = baseDir / imagePath;
        std::error_code ec;
        candidate = std::filesystem::weakly_canonical(candidate, ec);
        if (!ec && std::filesystem::exists(candidate)) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("SBSAR: HEURISTIC resolved image relative to SBSAR: %s\n",
                   candidate.string().c_str());
            return candidate.string();
        }
    }
    // 5.2 Relative to current working directory (CWD)
    {
        std::filesystem::path candidate = std::filesystem::current_path() / imagePath;
        std::error_code ec;
        candidate = std::filesystem::weakly_canonical(candidate, ec);
        if (!ec && std::filesystem::exists(candidate)) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("SBSAR: HEURISTIC (CWD) resolved image: %s\n", candidate.string().c_str());
            return candidate.string();
        }
    }
    // 5.3 Relative to parent of SBSAR directory
    {
        std::filesystem::path sbsarPath(resolvedSbsarPath);
        std::filesystem::path baseDir = sbsarPath.parent_path().parent_path();

        std::filesystem::path candidate = baseDir / imagePath;

        std::error_code ec;
        candidate = std::filesystem::weakly_canonical(candidate, ec);

        if (!ec && std::filesystem::exists(candidate)) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("SBSAR: HEURISTIC resolved image relative to SBSAR parent dir: %s\n",
                   candidate.string().c_str());
            return candidate.string();
        }
    }
    TF_WARN("SBSAR: Failed to resolve image '%s' for parameter '%s' (SBSAR: %s)",
            imagePath.c_str(),
            parameterName.c_str(),
            resolvedSbsarPath.c_str());
    return {};
}

bool
SBSARFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("SBSARFileFormat::Read, layerIdentifier: %s, resolvedPath: %s\n",
           layer->GetIdentifier().c_str(),
           resolvedPath.c_str());
    if (!TF_VERIFY(layer)) {
        return false;
    }

    // Parse argument to get sbsar parameters
    FileFormatArguments args = layer->GetFileFormatArguments();
    SBSAROptions sbsarData = parseFileFormatArguments(layer->GetFileFormatArguments());

    SdfAbstractDataRefPtr layerData = InitData(args);
    CreateLayerData(layerData, resolvedPath, sbsarData);
    _SetLayerData(layer, layerData);

    // Enforce that the layer is read only.
    layer->SetPermissionToSave(false);
    layer->SetPermissionToEdit(false);
    return true;
}

void
SBSARFileFormat::ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                                     const PcpDynamicFileFormatContext& context,
                                                     FileFormatArguments* args,
                                                     VtValue* dependencyContextData) const
{
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("SBSARFileFormat::ComposeFieldsForFileFormatArguments: asset path : %s\n",
           assetPath.c_str());
    std::string sbsarPath;
    FileFormatArguments arguments;
    SdfLayer::SplitIdentifier(assetPath, &sbsarPath, &arguments);

    // Ensure the SBSAR path is fully resolved for use as an anchor
    std::string resolvedSbsarPath = ArGetResolver().Resolve(sbsarPath);
    if (resolvedSbsarPath.empty()) {
        resolvedSbsarPath = sbsarPath; // Fall back to original if resolution fails
    }
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("SBSARFileFormat::ComposeFieldsForFileFormatArguments: resolved SBSAR path : %s\n",
           resolvedSbsarPath.c_str());

    ParameterListPtr sbsarParameters = getParameterListFromPackageCache(sbsarPath);
    SymbolMapper symbolMapper;
    VtDictionary dict;
    for (const SubstanceAir::InputDescBase* parameter : *sbsarParameters) {
        std::string parameterName = parameter->mIdentifier.c_str();
        const TfToken paramToken = getInputParamToken(symbolMapper, parameterName);
        VtValue paramValue;
        if (context.ComposeAttributeDefaultValue(paramToken, &paramValue)) {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("SBSARFileFormat::ComposeFieldsForFileFormatArguments: "
                   "Param found : %s, value: %s\n",
                   parameterName.c_str(),
                   TfStringify(paramValue).c_str());
            if (parameter->isImage()) {
                const auto& imageAssetPath = paramValue.Get<SdfAssetPath>();
                std::string resolvedImageAssetPath =
                  resolveSbsarImageInputAssetPath(imageAssetPath, resolvedSbsarPath, parameterName);
                std::size_t hash = addImageToInputImageCache(resolvedImageAssetPath);
                dict[parameterName] = VtValue(hash);
            } else {
                // Color values in USD are in linear space, but color inputs for a Substance graph
                // are (usually) in sRGB space. So we convert the incoming value from USD to sRGB
                // space. Note that we do the inverse transform when extracting the default value
                // from the graph to provide it to USD.
                if (parameter->mGuiWidget == SubstanceAir::InputWidget::Input_Color) {
                    convertColorLinearToSRGB(paramValue);
                }
                dict[parameterName] = paramValue;
            }
        }
    }

    if (!dict.empty()) {
        std::stringstream ss;
        DictEncoder::writeDict(dict, ss);
        (*args)["sbsarParameters"] = ss.str();
    }
}

bool
SBSARFileFormat::CanAttributeDefaultValueChangeAffectFileFormatArguments(
  const TfToken& attributeName,
  const VtValue& oldValue,
  const VtValue& newValue,
  const VtValue& dependencyContextData) const
{
    // All procedural parameters that influence the file format arguments have a shared prefix
    if (!TfStringStartsWith(attributeName, proceduralParameterPrefix)) {
        return false;
    }

    // All value changes trigger a recomputation
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("SBSARFileFormat::"
           "CanAttributeDefaultValueChangeAffectFileFormatArguments, "
           "attributeName: %s, oldvalue: %s, newvalue: %s\n",
           attributeName.GetString().c_str(),
           TfStringify(oldValue).c_str(),
           TfStringify(newValue).c_str());

    return true;
}

bool
SBSARFileFormat::CanFieldChangeAffectFileFormatArguments(const TfToken& field,
                                                         const VtValue& oldValue,
                                                         const VtValue& newValue,
                                                         const VtValue& dependencyContextData) const
{
    // No field on the prim with the payload can influence the file format arguments.
    // We only look at attribute default values.
    return false;
}

bool
SBSARFileFormat::WriteToString(const SdfLayer& layer,
                               std::string* str,
                               const std::string& comment) const
{
    // Fall back to USDA
    return SdfFileFormat::FindById(FileFormatsUsdaFileFormatTokensId)
      ->WriteToString(layer, str, comment);
}

bool
SBSARFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // Fall back to USDA
    return SdfFileFormat::FindById(FileFormatsUsdaFileFormatTokensId)
      ->WriteToStream(spec, out, indent);
}

bool
SBSARFileFormat::WriteToFile(const SdfLayer& layer,
                             const std::string& filePath,
                             const std::string& comment,
                             const FileFormatArguments& args) const
{
    TF_CODING_ERROR("Writing sbsar layers is not allowed.");
    return false;
}
PXR_NAMESPACE_CLOSE_SCOPE
