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
#include "obj.h"
#include "objExport.h"
#include "objImport.h"

#include <fileformatutils/common.h>
#include <fileformatutils/dictencoder.h>
#include <fileformatutils/geometry.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/resolver.h>
#include <fileformatutils/usdData.h>

#include <pxr/usd/usd/usdaFileFormat.h>

PXR_NAMESPACE_OPEN_SCOPE

using namespace adobe::usd;

const TfToken UsdObjFileFormat::assetsPathToken("objAssetsPath", TfToken::Immortal);
const TfToken UsdObjFileFormat::phongToken("objPhong", TfToken::Immortal);
const TfToken UsdObjFileFormat::originalColorSpaceToken("objOriginalColorSpace", TfToken::Immortal);

TF_DEFINE_PUBLIC_TOKENS(UsdObjFileFormatTokens, USDOBJ_FILE_FORMAT_TOKENS);
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdObjFileFormat, SdfFileFormat);
}

UsdObjFileFormat::UsdObjFileFormat()
  : SdfFileFormat(UsdObjFileFormatTokens->Id,
                  UsdObjFileFormatTokens->Version,
                  UsdObjFileFormatTokens->Target,
                  UsdObjFileFormatTokens->Id)
{
    TF_DEBUG_MSG(FILE_FORMAT_OBJ, "usdobj %s\n", FILE_FORMATS_VERSION);
}

UsdObjFileFormat::~UsdObjFileFormat() {}

SdfAbstractDataRefPtr
UsdObjFileFormat::InitData(const FileFormatArguments& args) const
{
    ObjDataRefPtr pd(new ObjData());
    for (auto arg : args) {
        TF_DEBUG_MSG(
          FILE_FORMAT_OBJ, "FileFormatArg: %s = %s\n", arg.first.c_str(), arg.second.c_str());
    }
    argReadBool(args, AdobeTokens->writeMaterialX.GetText(), pd->writeMaterialX, DEBUG_TAG);
    argReadString(args, assetsPathToken.GetText(), pd->assetsPath, DEBUG_TAG);
    argReadBool(args, phongToken.GetText(), pd->phong, DEBUG_TAG);
    argReadString(args, originalColorSpaceToken.GetText(), pd->originalColorSpace, DEBUG_TAG);
    return pd;
}
void
UsdObjFileFormat::ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                                      const PcpDynamicFileFormatContext& context,
                                                      FileFormatArguments* args,
                                                      VtValue* dependencyContextData) const
{
    argComposeString(context, args, assetsPathToken, DEBUG_TAG);
    argComposeBool(context, args, phongToken, DEBUG_TAG);
    argComposeString(context, args, originalColorSpaceToken, DEBUG_TAG);
}

bool
UsdObjFileFormat::CanFieldChangeAffectFileFormatArguments(
  const TfToken& field,
  const VtValue& oldValue,
  const VtValue& newValue,
  const VtValue& dependencyContextData) const
{
    return true;
}

bool
UsdObjFileFormat::CanRead(const std::string& filePath) const
{
    // Could check to see if it looks like valid obj data...
    return true;
}

bool
UsdObjFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Read: %s\n", resolvedPath.c_str());
    std::string fileType = getFileExtension(resolvedPath, DEBUG_TAG);
    SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
    ObjDataConstPtr data = TfDynamic_cast<const ObjDataConstPtr>(layerData);
    UsdData usd;
    Obj obj;
    bool readImages = !data->assetsPath.empty();
    ImportObjOptions options;
    options.importGeometry = true;
    options.importMaterials = true;
    options.importImages = readImages;
    options.importPhong = data->phong;
    WriteLayerOptions layerOptions;
    layerOptions.writeMaterialX = data->writeMaterialX;
    layerOptions.assetsPath = data->assetsPath;
    obj.originalColorSpace = data->originalColorSpace;
    GUARD(
      readObj(obj, resolvedPath, readImages), "Error reading OBJ from %s\n", resolvedPath.c_str());
    GUARD(importObj(options, obj, usd), "Error translating OBJ to USD\n");
    GUARD(writeLayer(layerOptions, usd, layer, layerData, fileType, DEBUG_TAG, SdfFileFormat::_SetLayerData),
          "Error writing to the USD layer\n");
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));

    if (options.importImages) {
        Resolver::populateCache(resolvedPath, std::move(usd.images));
    } else {
        Resolver::clearCache(resolvedPath);
    }

    return true;
}

bool
UsdObjFileFormat::ReadFromString(SdfLayer* layer, const std::string& input) const
{
    TfStopwatch w;
    w.Start();
    SdfAbstractDataRefPtr layerData = InitData(layer->GetFileFormatArguments());
    UsdData usd;
    Obj obj;
    ImportObjOptions options;
    WriteLayerOptions layerOptions;
    GUARD(readObj(obj, input.c_str(), input.size()), "Error reading OBJ from string\n");
    GUARD(importObj(options, obj, usd), "Error translating OBJ to USD\n");
    GUARD(writeLayer(layerOptions, usd, layer, layerData, "obj", DEBUG_TAG, SdfFileFormat::_SetLayerData),
          "Error writing to the USD stage\n");
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdObjFileFormat::WriteToFile(const SdfLayer& layer,
                              const std::string& filename,
                              const std::string& comment,
                              const FileFormatArguments& args) const
{
    TfStopwatch w;
    w.Start();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ, "WriteToFile: %s\n", filename.c_str());
    UsdData usd;
    Obj obj;
    ReadLayerOptions layerOptions;
    layerOptions.flatten = true;
    // OBJ doesn't support invisible primitives, so we filter them out here
    layerOptions.ignoreInvisible = true;
    argReadString(args, "outputColorSpace", obj.outputColorSpace, DEBUG_TAG);
    ExportObjOptions options;
    options.filename = filename;
    GUARD(readLayer(layerOptions, layer, usd, DEBUG_TAG), "Error reading USD\n");
    GUARD(exportObj(options, usd, obj), "Error translating USD to OBJ\n");
    GUARD(writeObj(obj, filename, false), "Error writing OBJ to %s\n", filename.c_str());
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Total time: %ld\n", static_cast<long int>(w.GetMilliseconds()));
    return true;
}

bool
UsdObjFileFormat::WriteToString(const SdfLayer& layer,
                                std::string* str,
                                const std::string& comment) const
{
    // Write USD as OBJ: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdObjFileFormat::WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const
{
    // Write USD as OBJ: Defer to the usda file format for now.
    return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
