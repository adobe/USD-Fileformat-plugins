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
#ifdef _MSC_VER
// Disable warnings in the pxr headers.
// conversion from 'double' to 'float', possible loss of data
#    pragma warning(disable : 4244)
// conversion from 'size_t' to 'int', possible loss of data
#    pragma warning(disable : 4267)
// truncation from 'double' to 'float'
#    pragma warning(disable : 4305)
#endif // _MSC_VER
#include "api.h"
#include <iosfwd>
#include <fileformatutils/sdfUtils.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
#include <pxr/usd/sdf/data.h>
#include <string>
#include <version.h>

PXR_NAMESPACE_OPEN_SCOPE

#define USDFBX_FILE_FORMAT_TOKENS ((Id, "fbx"))((Version, FILE_FORMATS_VERSION))((Target, "usd"))

TF_DECLARE_PUBLIC_TOKENS(UsdFbxFileFormatTokens, USDFBX_FILE_FORMAT_TOKENS);

TF_DECLARE_WEAK_AND_REF_PTRS(UsdFbxFileFormat);

TF_DECLARE_WEAK_AND_REF_PTRS(FbxData);

/// \ingroup usdfbx
/// \brief SdfData specialization for working with FBX files.
class FbxData : public FileFormatDataBase
{
  public:
    std::string assetsPath;
    bool phong = false;
    bool animationStacks = false;
    TfToken originalColorSpace;
    static FbxDataRefPtr InitData(const SdfFileFormat::FileFormatArguments& args);
};

/// \ingroup usdfbx
/// \brief SdfFileFormat specialization for working with FBX files.
class USDFBX_API UsdFbxFileFormat
  : public SdfFileFormat
  , public PcpDynamicFileFormatInterface
{
  public:
    friend class FbxData;

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
                      const std::string& resolved_path,
                      bool metadata_only) const override;

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
    static const TfToken phongToken;
    static const TfToken originalColorSpaceToken;
    static const TfToken animationStacksToken;

    SDF_FILE_FORMAT_FACTORY_ACCESS;
    ~UsdFbxFileFormat() override;
    UsdFbxFileFormat();
};

PXR_NAMESPACE_CLOSE_SCOPE