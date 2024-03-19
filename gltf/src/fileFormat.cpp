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
#include <common.h>
#include <layerRead.h>
#include <layerWriteSdfData.h>
#include <resolver.h>
#include <usdData.h>

// USD
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/usd/usdaFileFormat.h>

PXR_NAMESPACE_OPEN_SCOPE

using namespace adobe::usd;

const TfToken UsdGltfFileFormat::assetsPathToken("gltfAssetsPath");

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

bool
UsdGltfFileFormat::_ReadFromStream(SdfLayer* layer,
                                   std::istream& input,
                                   bool metadataOnly,
                                   std::string* outErr,
                                   std::istream& mtlinput) const
{
    // // Read Obj data stream.
    // UsdGltfStream gltfStream;
    // if (!UsdObjReadDataFromStream(input, mtlinput, &gltfStream, outErr))
    //     return false;

    // // Translate obj to usd schema.
    // SdfLayerRefPtr objAsUsd = UsdObjTranslateObjToUsd(gltfStream);
    // if (!objAsUsd)
    //     return false;

    // // Move generated content into final layer.
    // layer->TransferContent(objAsUsd);
    return true;
}

// void function(TfDebug::FILE_FORMAT_GLTF a) {
//     TF_DEBUG_MSG(a, "Experiment debug cdoe\n");
// }

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
    UsdData usd;
    tinygltf::Model gltf;
    ImportGltfOptions options;
    options.importGeometry = true;
    options.importMaterials = true;
    options.importImages = true;
    WriteLayerOptions layerOptions;
    layerOptions.writeMaterialX = data->writeMaterialX;
    layerOptions.pruneJoints = false;
    layerOptions.assetsPath = data->assetsPath;
    GUARD(readGltf(gltf, resolvedPath), "Error reading glTF file\n");
    GUARD(importGltf(options, gltf, usd, resolvedPath), "Error translating glTF to USD\n");
    GUARD(writeLayer(layerOptions, usd, layer, layerData, DEBUG_TAG, SdfFileFormat::_SetLayerData),
          "Error writing to the USD layer\n");

    if (options.importImages) {
        Resolver::populateCache(resolvedPath, std::move(usd.images));
    } else {
        Resolver::clearCache(resolvedPath);
    }

    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Total time: %ld ms\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdGltfFileFormat::ReadFromString(SdfLayer* layer, const std::string& str) const
{
    std::string error;
    std::stringstream ss(str);
    std::stringstream ssmtl("");
    if (!_ReadFromStream(layer, ss, /*metadataOnly=*/false, &error, ssmtl)) {
        TF_RUNTIME_ERROR("Failed to read GLTF data from string: %s", error.c_str());
        return false;
    }
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
