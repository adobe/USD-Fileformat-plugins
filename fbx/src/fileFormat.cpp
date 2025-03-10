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
#include "fbx.h"
#include "fbxExport.h"
#include "fbxImport.h"
#include <mutex>


#include <fileformatutils/common.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/resolver.h>
#include <fileformatutils/usdData.h>

#include <pxr/usd/usd/usdaFileFormat.h>

using namespace adobe::usd;

PXR_NAMESPACE_OPEN_SCOPE

static std::mutex mutex;
const TfToken UsdFbxFileFormat::assetsPathToken("fbxAssetsPath", TfToken::Immortal);
const TfToken UsdFbxFileFormat::phongToken("fbxPhong", TfToken::Immortal);
const TfToken UsdFbxFileFormat::originalColorSpaceToken("fbxOriginalColorSpace", TfToken::Immortal);
const TfToken UsdFbxFileFormat::animationStacksToken("fbxAnimationStacks", TfToken::Immortal);

TF_DEFINE_PUBLIC_TOKENS(UsdFbxFileFormatTokens, USDFBX_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdFbxFileFormat, SdfFileFormat);
}

UsdFbxFileFormat::UsdFbxFileFormat()
  : SdfFileFormat(UsdFbxFileFormatTokens->Id,
                  UsdFbxFileFormatTokens->Version,
                  UsdFbxFileFormatTokens->Target,
                  UsdFbxFileFormatTokens->Id)
{
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "usdfbx %s\n", FILE_FORMATS_VERSION);
}

UsdFbxFileFormat::~UsdFbxFileFormat() {}

SdfAbstractDataRefPtr
UsdFbxFileFormat::InitData(const FileFormatArguments& args) const
{
    FbxDataRefPtr pd(new FbxData());
    for (auto arg : args) {
        TF_DEBUG_MSG(
          FILE_FORMAT_FBX, "FileFormatArg: %s = %s\n", arg.first.c_str(), arg.second.c_str());
    }
    argReadBool(args, AdobeTokens->writeMaterialX.GetString(), pd->writeMaterialX, DEBUG_TAG);
    argReadString(args, assetsPathToken.GetString(), pd->assetsPath, DEBUG_TAG);
    argReadBool(args, phongToken.GetString(), pd->phong, DEBUG_TAG);
    argReadString(args, originalColorSpaceToken.GetString(), pd->originalColorSpace, DEBUG_TAG);
    argReadBool(args, animationStacksToken.GetString(), pd->animationStacks, DEBUG_TAG);
    return pd;
}
void
UsdFbxFileFormat::ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                                      const PcpDynamicFileFormatContext& context,
                                                      FileFormatArguments* args,
                                                      VtValue* dependencyContextData) const
{
    argComposeString(context, args, assetsPathToken, DEBUG_TAG);
    argComposeBool(context, args, phongToken, DEBUG_TAG);
    argComposeString(context, args, originalColorSpaceToken, DEBUG_TAG);
}

bool
UsdFbxFileFormat::CanFieldChangeAffectFileFormatArguments(
  const TfToken& field,
  const VtValue& oldValue,
  const VtValue& newValue,
  const VtValue& dependencyContextData) const
{
    return true;
}

bool
UsdFbxFileFormat::CanRead(const std::string& path) const
{
    return true;
}

bool
UsdFbxFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadata_only) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "Read: %s\n", resolvedPath.c_str());
    std::string fileType = getFileExtension(resolvedPath, DEBUG_TAG);
    SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
    FbxDataConstPtr data = TfDynamic_cast<const FbxDataConstPtr>(layerData);
    UsdData usd;
    ImportFbxOptions options;
    options.importGeometry = true;
    options.importMaterials = true;
    options.importImages = !data->assetsPath.empty();
    options.importPhong = data->phong;
    options.originalColorSpace = data->originalColorSpace;
    WriteLayerOptions layerOptions;
    layerOptions.writeMaterialX = data->writeMaterialX;
    layerOptions.assetsPath = data->assetsPath;
    layerOptions.animationTracks = data->animationStacks;
    {
        const std::lock_guard<std::mutex> lock(mutex); // FBX SDK is not thread safe
        Fbx fbx;
        GUARD(readFbx(fbx, resolvedPath, options.importImages, false),
              "Error reading FBX from %s\n",
              resolvedPath.c_str());
        GUARD(importFbx(options, fbx, usd), "Error translating FBX to USD\n");
    }
    GUARD(writeLayer(
            layerOptions, usd, layer, layerData, fileType, DEBUG_TAG, SdfFileFormat::_SetLayerData),
          "Error writing to the USD layer\n");

    if (options.importImages) {
        Resolver::populateCache(resolvedPath, std::move(usd.images));
    } else {
        Resolver::clearCache(resolvedPath);
    }

    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdFbxFileFormat::ReadFromString(SdfLayer* layer, const std::string& str) const
{
    // GLTF can use multiple files and the reader assumes those files are on disk.
    TF_RUNTIME_ERROR("Cannot import FBX from a string in memory.");
    return false;
}

bool
UsdFbxFileFormat::WriteToString(const SdfLayer& layer,
                                std::string* str,
                                const std::string& comment) const
{
    // Write as USDA because we don't implement FBX export.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdFbxFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // Write as USDA because we don't implement FBX export.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToStream(spec, out, indent);
}

bool
UsdFbxFileFormat::WriteToFile(const SdfLayer& layer,
                              const std::string& filename,
                              const std::string& comment,
                              const FileFormatArguments& args) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "WriteToFile: %s\n", filename.c_str());
    UsdData usd;
    ReadLayerOptions layerOptions;
    ExportFbxOptions exportOptions;

    bool embedImages = false;
    std::string outputColorSpace;
    argReadBool(args, "embedImages", embedImages, DEBUG_TAG);
    argReadString(args, "outputColorSpace", outputColorSpace, DEBUG_TAG);

    exportOptions.embedImages = embedImages;
    exportOptions.exportParentPath = TfGetPathName(filename);
    exportOptions.outputColorSpace = TfToken(outputColorSpace);

    GUARD(readLayer(layerOptions, layer, usd, DEBUG_TAG), "Error reading USD\n");
    {
        const std::lock_guard<std::mutex> lock(mutex); // FBX SDK is not thread safe
        Fbx fbx;
        GUARD(exportFbx(exportOptions, usd, fbx), "Error translating USD to FBX\n");
        GUARD(
          writeFbx(exportOptions, fbx, filename), "Error writing FBX to %s\n", filename.c_str());
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
