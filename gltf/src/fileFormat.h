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
#pragma once
#include "api.h"
#include <version.h>

#include <fileformatutils/sdfUtils.h>

#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>

#include <iosfwd>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#define USDGLTF_FILE_FORMAT_TOKENS \
    ((Id, "gltf")) \
    ((Version, FILE_FORMATS_VERSION)) \
    ((Target, "usd"))
// clang-format on

TF_DECLARE_PUBLIC_TOKENS(UsdGltfFileFormatTokens, USDGLTF_FILE_FORMAT_TOKENS);
TF_DECLARE_WEAK_AND_REF_PTRS(GltfData);
TF_DECLARE_WEAK_AND_REF_PTRS(UsdGltfFileFormat);

class ArAsset;

/// \ingroup usdgltf
/// \brief SdfData specialization for working with glTF files.
class GltfData : public FileFormatDataBase
{
  public:
    std::string assetsPath;
    bool animationTracks = false;
    static GltfDataRefPtr InitData(const SdfFileFormat::FileFormatArguments& args);
};

/// \ingroup usdgltf
/// \brief SdfFileFormat specialization for working with glTF files.
class USDGLTF_API UsdGltfFileFormat
  : public SdfFileFormat
  , public PcpDynamicFileFormatInterface
{
  public:
    friend class GltfData;

    // If successful, returns true and fills in the assetPtr, baseDir and the isAscii flag
    // baseDir can be used to resolve external assets
    static bool OpenGltfAsset(const std::string& resolvedPath,
                              std::shared_ptr<ArAsset>& assetPtr,
                              std::string& baseDir,
                              bool& isAscii);

    virtual SdfAbstractDataRefPtr InitData(const FileFormatArguments& args) const override;

    virtual void ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                                     const PcpDynamicFileFormatContext& context,
                                                     FileFormatArguments* args,
                                                     VtValue* dependencyContextData) const override;

    virtual bool CanFieldChangeAffectFileFormatArguments(
      const TfToken& field,
      const VtValue& oldValue,
      const VtValue& newValue,
      const VtValue& dependencyContextData) const override;

    virtual bool CanRead(const std::string& file) const override;

    virtual bool Read(SdfLayer* layer,
                      const std::string& resolvedPath,
                      bool metadataOnly) const override;

    virtual bool ReadFromString(SdfLayer* layer, const std::string& str) const override;

    virtual bool WriteToString(const SdfLayer& layer,
                               std::string* str,
                               const std::string& comment = std::string()) const override;

    virtual bool WriteToStream(const SdfSpecHandle& spec,
                               std::ostream& out,
                               size_t indent) const override;

    virtual bool WriteToFile(
      const SdfLayer& layer,
      const std::string& filePath,
      const std::string& comment = std::string(),
      const FileFormatArguments& args = FileFormatArguments()) const override;

  protected:
    static const TfToken assetsPathToken;
    static const TfToken animationTracksToken;

    SDF_FILE_FORMAT_FACTORY_ACCESS;

    virtual ~UsdGltfFileFormat();

    UsdGltfFileFormat();
};

PXR_NAMESPACE_CLOSE_SCOPE
