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
#include "dictencoder.h"
#include "plyExport.h"
#include "plyImport.h"

#include <fileformatutils/common.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/usdData.h>

#include <happly.h>

#include <pxr/base/tf/fileUtils.h>
#include <pxr/usd/usd/usdaFileFormat.h>

#include <filesystem>
#include <fstream>

using namespace adobe::usd;
using namespace happly;

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(UsdPlyFileFormatTokens, USDPLY_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdPlyFileFormat, SdfFileFormat);
}

UsdPlyFileFormat::UsdPlyFileFormat()
  : SdfFileFormat(UsdPlyFileFormatTokens->Id,
                  UsdPlyFileFormatTokens->Version,
                  UsdPlyFileFormatTokens->Target,
                  UsdPlyFileFormatTokens->Id)
{
    TF_DEBUG_MSG(FILE_FORMAT_PLY, "usdply %s\n", FILE_FORMATS_VERSION);
}

UsdPlyFileFormat::~UsdPlyFileFormat() {}

SdfAbstractDataRefPtr
UsdPlyFileFormat::InitData(const FileFormatArguments& args) const
{
    PlyDataRefPtr pd(new PlyData());
    for (auto arg : args) {
        TF_DEBUG_MSG(
          FILE_FORMAT_PLY, "FileFormatArg: %s = %s\n", arg.first.c_str(), arg.second.c_str());
    }
    argReadBool(args, AdobeTokens->writeMaterialX.GetText(), pd->writeMaterialX, DEBUG_TAG);
    argReadBool(args, UsdPlyFileFormatTokens->points.GetText(), pd->points, DEBUG_TAG);
    argReadFloat(args, UsdPlyFileFormatTokens->pointWidth.GetText(), pd->pointWidth, DEBUG_TAG);
    argReadBool(args,
                UsdPlyFileFormatTokens->withUpAxisCorrection.GetText(),
                pd->withUpAxisCorrection,
                DEBUG_TAG);
    argReadFloatArray(args,
                      UsdPlyFileFormatTokens->pointsGsplatClippingBox.GetText(),
                      pd->gsplatsClippingBox,
                      DEBUG_TAG);
    return pd;
}

void
UsdPlyFileFormat::ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                                      const PcpDynamicFileFormatContext& context,
                                                      FileFormatArguments* args,
                                                      VtValue* dependencyContextData) const
{
    argComposeBool(context, args, UsdPlyFileFormatTokens->points, DEBUG_TAG);
    argComposeFloat(context, args, UsdPlyFileFormatTokens->pointWidth, DEBUG_TAG);
    argComposeBool(context, args, UsdPlyFileFormatTokens->withUpAxisCorrection, DEBUG_TAG);
    argComposeFloatArray(context, args, UsdPlyFileFormatTokens->pointsGsplatClippingBox, DEBUG_TAG);
}

bool
UsdPlyFileFormat::CanFieldChangeAffectFileFormatArguments(
  const TfToken& field,
  const VtValue& oldValue,
  const VtValue& newValue,
  const VtValue& dependencyContextData) const
{
    return true;
}

bool
UsdPlyFileFormat::CanRead(const std::string& filePath) const
{
    // Could check to see if it looks like valid ply data...
    return true;
}

bool
UsdPlyFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_PLY, "Read: %s\n", resolvedPath.c_str());
    std::string fileType = getFileExtension(resolvedPath, DEBUG_TAG);
    SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
    PlyDataConstPtr data = TfDynamic_cast<const PlyDataConstPtr>(layerData);
    UsdData usd;
    try {
        ImportPlyOptions options;
        options.importAsPoints = data->points;
        options.pointWidth = data->pointWidth;
        options.importWithUpAxisCorrection = data->withUpAxisCorrection;
        options.importGsplatClippingBox = data->gsplatsClippingBox;
        WriteLayerOptions layerOptions;
        layerOptions.writeMaterialX = data->writeMaterialX;

        // Since the resolved path may contain non-ascii characters, we convert the string
        // path to a filesystem path and then use it to create an ifstream we can then pass to
        // the PLYData constructor.
        const auto filePath = std::filesystem::u8path(resolvedPath);
        std::ifstream inStream(filePath, std::ios::binary);
        if (!inStream.is_open()) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Failed to open %s\n", resolvedPath.c_str());
        }
        PLYData ply(inStream);

        GUARD(importPly(options, ply, usd), "Error translating PLY to USD\n");
        GUARD(
          writeLayer(
            layerOptions, usd, layer, layerData, fileType, DEBUG_TAG, SdfFileFormat::_SetLayerData),
          "Error writing to the USD layer\n");
    } catch (std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_PLY, "Failed to open %s: %s\n", resolvedPath.c_str(), e.what());
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_PLY, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdPlyFileFormat::ReadFromString(SdfLayer* layer, const std::string& input) const
{
    // Ply ply;
    // UsdData usd;
    // GUARD(readPly(ply, input.c_str(), input.size()), "Error reading PLY from string\n");
    // ImportPlyOptions options;
    // GUARD(importPly(options, ply, usd), "Error translating PLY to USD\n");
    // SdfLayerRefPtr newLayer = SdfLayer::CreateAnonymous(".usda");
    // WriteLayerOptions layerOptions;
    // GUARD(writeLayer(layerOptions, usd, newLayer), "Error writing to the USD stage\n",
    // DEBUG_TAG); layer->TransferContent(newLayer);
    return true;
}

bool
UsdPlyFileFormat::WriteToFile(const SdfLayer& layer,
                              const std::string& filename,
                              const std::string& comment,
                              const FileFormatArguments& args) const
{
    TfStopwatch w;
    w.Start();
    UsdData usd;
    PLYData ply;
    ReadLayerOptions layerOptions;
    layerOptions.flatten = true;
    // PLY doesn't support invisible primitives, so we filter them out here
    layerOptions.ignoreInvisible = true;
    SdfAbstractDataRefPtr layerData = InitData(layer.GetFileFormatArguments());
    PlyDataConstPtr data = TfDynamic_cast<const PlyDataConstPtr>(layerData);
    GUARD(readLayer(layerOptions, layer, usd, DEBUG_TAG), "Error reading USD\n");
    GUARD(exportPly(usd, ply), "Error translating USD to PLY\n");
    try {
        // TODO: pass file format argument to select binary/ascii
        const std::string parentPath = TfGetPathName(filename);
        TfMakeDirs(parentPath, -1, true);
        ply.write(filename, happly::DataFormat::Binary);
    } catch (std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_PLY, "Error writing PLY to %s: %s\n", filename.c_str(), e.what());
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_PLY, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdPlyFileFormat::WriteToString(const SdfLayer& layer,
                                std::string* str,
                                const std::string& comment) const
{
    // Write USD as PLY: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdPlyFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // Write USD as PLY: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
