/*
Copyright 2025 Adobe. All rights reserved.
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
#include "spzExport.h"
#include "spzImport.h"

#include <fileformatutils/common.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/usdData.h>

#include <load-spz.h>

#include <pxr/base/tf/fileUtils.h>
#include <pxr/usd/usd/usdaFileFormat.h>

using namespace adobe::usd;
using namespace spz;

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(UsdSpzFileFormatTokens, USDSPZ_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdSpzFileFormat, SdfFileFormat);
}

UsdSpzFileFormat::UsdSpzFileFormat()
  : SdfFileFormat(UsdSpzFileFormatTokens->Id,
                  UsdSpzFileFormatTokens->Version,
                  UsdSpzFileFormatTokens->Target,
                  UsdSpzFileFormatTokens->Id)
{
    TF_DEBUG_MSG(FILE_FORMAT_SPZ, "usdspz %s\n", FILE_FORMATS_VERSION);
}

UsdSpzFileFormat::~UsdSpzFileFormat() {}

SdfAbstractDataRefPtr
UsdSpzFileFormat::InitData(const FileFormatArguments& args) const
{
    SpzDataRefPtr pd(new SpzData());
    for (auto arg : args) {
        TF_DEBUG_MSG(
          FILE_FORMAT_SPZ, "FileFormatArg: %s = %s\n", arg.first.c_str(), arg.second.c_str());
    }
    argReadBool(
      args, UsdSpzFileFormatTokens->gsplatsWithZup.GetText(), pd->gsplatsWithZup, DEBUG_TAG);
    argReadFloatArray(
	  args, UsdSpzFileFormatTokens->gsplatsClippingBox.GetText(), pd->gsplatsClippingBox, DEBUG_TAG);
    return pd;
}

void
UsdSpzFileFormat::ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                                      const PcpDynamicFileFormatContext& context,
                                                      FileFormatArguments* args,
                                                      VtValue* dependencyContextData) const
{
    argComposeBool(context, args, UsdSpzFileFormatTokens->gsplatsWithZup, DEBUG_TAG);
    argComposeFloatArray(context, args, UsdSpzFileFormatTokens->gsplatsClippingBox, DEBUG_TAG);
}

bool
UsdSpzFileFormat::CanFieldChangeAffectFileFormatArguments(
  const TfToken& field,
  const VtValue& oldValue,
  const VtValue& newValue,
  const VtValue& dependencyContextData) const
{
    return true;
}

bool
UsdSpzFileFormat::CanRead(const std::string& filePath) const
{
    // Could check to see if it looks like valid spz data...
    return true;
}

bool
UsdSpzFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_SPZ, "Read: %s\n", resolvedPath.c_str());
    std::string fileType = getFileExtension(resolvedPath, DEBUG_TAG);
    SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
    SpzDataConstPtr data = TfDynamic_cast<const SpzDataConstPtr>(layerData);
    UsdData usd;
    try {
        WriteLayerOptions layerOptions;
        ImportSpzOptions options;
        options.importGsplatWithZup = data->gsplatsWithZup;
        options.importGsplatClippingBox = data->gsplatsClippingBox;
        GaussianCloud gaussianCloud = loadSpz(resolvedPath);
        GUARD(importSpz(options, gaussianCloud, usd), "Error translating SPZ to USD\n");
        GUARD(
          writeLayer(layerOptions, usd, layer, layerData, fileType, DEBUG_TAG, SdfFileFormat::_SetLayerData),
          "Error writing to the USD layer\n");
    } catch (std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_SPZ, "Failed to open %s: %s\n", resolvedPath.c_str(), e.what());
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_SPZ, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdSpzFileFormat::ReadFromString(SdfLayer* layer, const std::string& input) const
{
    return true;
}

bool
UsdSpzFileFormat::WriteToFile(const SdfLayer& layer,
                              const std::string& filename,
                              const std::string& comment,
                              const FileFormatArguments& args) const
{
    TfStopwatch w;
    w.Start();
    UsdData usd;
    GaussianCloud gaussianCloud;
    ReadLayerOptions layerOptions;
    layerOptions.flatten = true;
    // SPZ doesn't support invisible primitives, so we filter them out here
    layerOptions.ignoreInvisible = true;
    SdfAbstractDataRefPtr layerData = InitData(layer.GetFileFormatArguments());
    SpzDataConstPtr data = TfDynamic_cast<const SpzDataConstPtr>(layerData);
    GUARD(readLayer(layerOptions, layer, usd, DEBUG_TAG), "Error reading USD\n");
    GUARD(exportSpz(usd, gaussianCloud), "Error translating USD to SPZ\n");
    try {
        const std::string parentPath = TfGetPathName(filename);
        TfMakeDirs(parentPath, -1, true);
        saveSpz(gaussianCloud, filename);
    } catch (std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_SPZ, "Error writing SPZ to %s: %s\n", filename.c_str(), e.what());
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_SPZ, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdSpzFileFormat::WriteToString(const SdfLayer& layer,
                                std::string* str,
                                const std::string& comment) const
{
    // Write USD as SPZ: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdSpzFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // Write USD as SPZ: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
