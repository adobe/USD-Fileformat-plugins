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
#include "stlExport.h"
#include "stlImport.h"
#include "stlModel.h"

#include <fileformatutils/common.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerWriteSdfData.h>

#include <pxr/usd/usd/usdaFileFormat.h>
#include <pxr/usd/usdGeom/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

using namespace usdStl;
using namespace adobe::usd;

TF_DEFINE_PUBLIC_TOKENS(UsdStlFileFormatTokens, USDSTL_FILE_FORMAT_TOKENS);
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdStlFileFormat, SdfFileFormat);
}

UsdStlFileFormat::UsdStlFileFormat()
  : SdfFileFormat(UsdStlFileFormatTokens->Id,
                  UsdStlFileFormatTokens->Version,
                  UsdStlFileFormatTokens->Target,
                  UsdStlFileFormatTokens->Id)
{
    TF_DEBUG_MSG(FILE_FORMAT_STL, "usdstl %s\n", FILE_FORMATS_VERSION);
}

UsdStlFileFormat::~UsdStlFileFormat() {}

bool
UsdStlFileFormat::CanRead(const std::string& filePath) const
{
    // Could check to see if it looks like valid stl data...
    return true;
}

bool
UsdStlFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    UsdData usd;

    // Note, the STL format doesn't actually prescribe an up-axis. But many STL files out there,
    // especially those exported from Blender, use the Z-up convention. So we adopt it here as a
    // default as well.
    usd.upAxis = UsdGeomTokens->z;

    SdfAbstractDataRefPtr layerData(new SdfData());
    StlModel stlModel;
    stlModel.Read(resolvedPath);
    std::string fileType = getFileExtension(resolvedPath, DEBUG_TAG);
    GUARD(stlModel.Populated(), "Failed opening STL file: %s \n", resolvedPath.c_str());
    GUARD(importStl(usd, stlModel), "Error translating STL to USD\n");
    WriteLayerOptions layerOptions;
    GUARD(writeLayer(
            layerOptions, usd, layer, layerData, fileType, DEBUG_TAG, SdfFileFormat::_SetLayerData),
          "Error writing to the USD layer\n");

    return true;
}

bool
UsdStlFileFormat::ReadFromString(SdfLayer* layer, const std::string& input) const
{
    // Obj obj;
    // UsdData usd;
    // GUARD(readSbsm(obj, input), "Error reading SBSM from string\n");
    // GUARD(readUsd(usd, obj), "Error translating SBSM to USD\n");
    // SdfLayerRefPtr newLayer = SdfLayer::CreateAnonymous(".usda");
    // GUARD(writeUsd(usd, newLayer), "Error writing to the USD stage\n");
    // layer->TransferContent(newLayer);
    return true;
}

bool
UsdStlFileFormat::WriteToFile(const SdfLayer& layer,
                              const std::string& filename,
                              const std::string& comment,
                              const FileFormatArguments& args) const
{
    TfStopwatch watch;
    UsdData usd;
    StlModel stl;
    ReadLayerOptions layerOptions;
    layerOptions.triangulate = true;
    // STL doesn't support invisible primitives, so we filter them out here
    layerOptions.ignoreInvisible = true;
    GUARD(readLayer(layerOptions, layer, usd, DEBUG_TAG), "Error reading USD\n");
    ExportStlOptions options;
    GUARD(exportStl(options, usd, stl), "Error translating USD to STL\n");
    StlFormat format = readStlExportFormat(usd);
    TF_DEBUG_MSG(
      FILE_FORMAT_STL, "START time: %ld\n", static_cast<long int>(watch.GetMilliseconds()));
    watch.Start();
    GUARD(stl.Write(filename, format), "Error writing STL to %s\n", filename.c_str());
    watch.Stop();
    TF_DEBUG_MSG(
      FILE_FORMAT_STL, "WRITE time: %ld\n", static_cast<long int>(watch.GetMilliseconds()));
    return true;
}

bool
UsdStlFileFormat::WriteToString(const SdfLayer& layer,
                                std::string* str,
                                const std::string& comment) const
{
    // Write USD as SBSM: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdStlFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // Write USD as SBSM: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
