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
#include "fileFormat.h"
#include "debugCodes.h"
#include "gltf.h"
#include "gltfExport.h"
#include "gltfImport.h"

// utils
#include <fileformatutils/common.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/resolver.h>
#include <fileformatutils/usdData.h>

// USD
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/usd/usdaFileFormat.h>

PXR_NAMESPACE_OPEN_SCOPE

using namespace adobe::usd;

const TfToken UsdGltfFileFormat::assetsPathToken("gltfAssetsPath", TfToken::Immortal);
const TfToken UsdGltfFileFormat::animationTracksToken("gltfAnimationTracks", TfToken::Immortal);

TF_DEFINE_PUBLIC_TOKENS(UsdGltfFileFormatTokens, USDGLTF_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdGltfFileFormat, SdfFileFormat);
}

UsdGltfFileFormat::UsdGltfFileFormat()
  : SdfFileFormat(UsdGltfFileFormatTokens->Id,
                  UsdGltfFileFormatTokens->Version,
                  UsdGltfFileFormatTokens->Target,
                  UsdGltfFileFormatTokens->Id)
{
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "usdgltf %s\n", FILE_FORMATS_VERSION);
}

UsdGltfFileFormat::~UsdGltfFileFormat() {}

SdfAbstractDataRefPtr
UsdGltfFileFormat::InitData(const FileFormatArguments& args) const
{
    GltfDataRefPtr pd(new GltfData());
    for (auto arg : args) {
        TF_DEBUG_MSG(
          FILE_FORMAT_GLTF, "FileFormatArg: %s = %s\n", arg.first.c_str(), arg.second.c_str());
    }
    argReadBool(args, AdobeTokens->writeMaterialX.GetText(), pd->writeMaterialX, DEBUG_TAG);
    argReadString(args, assetsPathToken.GetText(), pd->assetsPath, DEBUG_TAG);
    argReadBool(args, animationTracksToken.GetText(), pd->animationTracks, DEBUG_TAG);
    return pd;
}

void
UsdGltfFileFormat::ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                                       const PcpDynamicFileFormatContext& context,
                                                       FileFormatArguments* args,
                                                       VtValue* dependencyContextData) const
{
    argComposeString(context, args, assetsPathToken, DEBUG_TAG);
}

bool
UsdGltfFileFormat::CanFieldChangeAffectFileFormatArguments(
  const TfToken& field,
  const VtValue& oldValue,
  const VtValue& newValue,
  const VtValue& dependencyContextData) const
{
    return true;
}

bool
UsdGltfFileFormat::CanRead(const std::string& filePath) const
{
    // Could check to see if it looks like valid obj data...
    return true;
}

/*static*/
bool
UsdGltfFileFormat::OpenGltfAsset(const std::string& resolvedPath,
                                 std::shared_ptr<ArAsset>& asset,
                                 std::string& baseDir,
                                 bool& isAscii)
{
    asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (!asset) {
        TF_WARN("Couldn't open asset %s", resolvedPath.c_str());
        return false;
    }

    // Extract the inner most name of a potentially nest path, e.g. "archive.usdz[myAsset.gltf]"
    auto [packagePath, packagedPath] = ArSplitPackageRelativePathInner(resolvedPath);

    // If we have a direct path on disk, we set the baseDir to the same folder
    if (packagedPath.empty()) {
        baseDir = TfGetPathName(packagePath);
    }

    const std::string& fileName = packagedPath.empty() ? packagePath : packagedPath;
    std::string ext = TfStringToLower(TfGetExtension(fileName));
    isAscii = (ext == "gltf");

    return true;
}

bool
UsdGltfFileFormat::Read(PXR_NS::SdfLayer* layer,
                        const std::string& resolvedPath,
                        bool metadataOnly) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Read: %s\n", resolvedPath.c_str());

    SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
    GltfDataConstPtr data = TfDynamic_cast<const GltfDataConstPtr>(layerData);

    std::shared_ptr<ArAsset> asset;
    std::string baseDir;
    bool isAscii = false;
    if (!OpenGltfAsset(resolvedPath, asset, baseDir, isAscii)) {
        return false;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    size_t bufferSize = asset->GetSize();
    TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                 "Type: %s, Base path: '%s', Size: %zu KB\n",
                 isAscii ? "GLTF" : "GLB",
                 baseDir.c_str(),
                 bufferSize >> 10);

    tinygltf::Model gltf;
    GUARD(readGltfFromMemory(gltf, baseDir, isAscii, &*buffer, bufferSize),
          "Error reading glTF file\n");

    UsdData usd;
    ImportGltfOptions options;
    options.importGeometry = true;
    options.importMaterials = true;
    options.importImages = true;
    GUARD(importGltf(options, gltf, usd, resolvedPath), "Error translating glTF to USD\n");

    WriteLayerOptions layerOptions;
    layerOptions.writeMaterialX = data->writeMaterialX;
    layerOptions.pruneJoints = false;
    layerOptions.assetsPath = data->assetsPath;
    layerOptions.animationTracks = data->animationTracks;
    std::string ext = isAscii ? "GLTF" : "GLB";
    GUARD(
      writeLayer(layerOptions, usd, layer, layerData, ext, DEBUG_TAG, SdfFileFormat::_SetLayerData),
      "Error writing to the USD layer\n");

    // Populate the GLTF resolver with the images we just parsed from the asset, so that we don't
    // have to open the asset again.
    if (options.importImages) {
        Resolver::populateCache(resolvedPath, std::move(usd.images));
    } else {
        Resolver::clearCache(resolvedPath);
    }

    w.Stop();
    TF_DEBUG_MSG(
      FILE_FORMAT_GLTF, "Total time: %ld ms\n", static_cast<long int>(w.GetMilliseconds()));

    return true;
}

bool
UsdGltfFileFormat::ReadFromString(SdfLayer* layer, const std::string& str) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "ReadFromString: %zu KB\n", str.size() >> 10);

    SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
    GltfDataConstPtr data = TfDynamic_cast<const GltfDataConstPtr>(layerData);

    // We don't have a base directory for external references. So only complete GLTF files will work
    // with this path
    std::string baseDir;
    bool isAscii = true;

    tinygltf::Model gltf;
    GUARD(readGltfFromMemory(gltf, baseDir, isAscii, &str[0], str.size()),
          "Error reading glTF from string\n");

    UsdData usd;
    ImportGltfOptions options;
    options.importGeometry = true;
    options.importMaterials = true;
    options.importImages = true;
    GUARD(importGltf(options, gltf, usd, ""), "Error translating glTF to USD\n");

    WriteLayerOptions layerOptions;
    layerOptions.writeMaterialX = data->writeMaterialX;
    layerOptions.pruneJoints = false;
    layerOptions.assetsPath = data->assetsPath;
    layerOptions.animationTracks = data->animationTracks;
    std::string ext = isAscii ? "GLTF" : "GLB";
    GUARD(
      writeLayer(layerOptions, usd, layer, layerData, ext, DEBUG_TAG, SdfFileFormat::_SetLayerData),
      "Error writing to the USD layer\n");

    // Note, we can't populate the path resolver since we don't have an associated file

    w.Stop();
    TF_DEBUG_MSG(
      FILE_FORMAT_GLTF, "Total time: %ld ms\n", static_cast<long int>(w.GetMilliseconds()));

    return true;
}

bool
UsdGltfFileFormat::WriteToFile(const SdfLayer& layer,
                               const std::string& filename,
                               const std::string& comment,
                               const FileFormatArguments& args) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "WriteToFile: %s\n", filename.c_str());
    for (const auto& [k, v] : args) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "  ARG: %s -> %s\n", k.c_str(), v.c_str());
    }

    bool binary = "glb" == TfGetExtension(filename);
    bool embedImages = true;
    bool useMaterialExtensions = true;
    argReadBool(args, "embedImages", embedImages, DEBUG_TAG);
    argReadBool(args, "useMaterialExtensions", useMaterialExtensions, DEBUG_TAG);

    ReadLayerOptions options;
    options.triangulate = true;

    // don't set a max on exported joints and weights when reading the USD data
    options.maxMeshInfluenceCount = -1;

    // glTF doesn't support invisible primitives, so we filter them out here
    options.ignoreInvisible = true;

    UsdData usd;
    GUARD(readLayer(options, layer, usd, DEBUG_TAG), "Error reading USD file\n");

    ExportGltfOptions exportOptions;
    exportOptions.binary = binary;
    exportOptions.embedImages = embedImages;
    exportOptions.useMaterialExtensions = useMaterialExtensions;
    tinygltf::Model gltf;
    GUARD(exportGltf(exportOptions, usd, gltf), "Error translating USD to glTF\n");

    WriteGltfOptions writeOptions;
    writeOptions.embedImages = embedImages;
    GUARD(writeGltf(writeOptions, gltf, filename), "Error writing glTF file\n");

    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdGltfFileFormat::WriteToString(const SdfLayer& layer,
                                 std::string* str,
                                 const std::string& comment) const
{
    // Write USD as glTF: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdGltfFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    out << "WriteToStream: Nothing to see." << std::endl;
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
