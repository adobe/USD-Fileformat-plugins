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
#include <iosfwd>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
#include <fileformatutils/sdfUtils.h>
#include <string>
#include <version.h>

PXR_NAMESPACE_OPEN_SCOPE

#define USDOBJ_FILE_FORMAT_TOKENS ((Id, "obj"))((Version, FILE_FORMATS_VERSION))((Target, "usd"))
TF_DECLARE_PUBLIC_TOKENS(UsdObjFileFormatTokens, USDOBJ_FILE_FORMAT_TOKENS);
TF_DECLARE_WEAK_AND_REF_PTRS(ObjData);
TF_DECLARE_WEAK_AND_REF_PTRS(UsdObjFileFormat);

/// \ingroup usdobj
/// \brief SdfData specialization for working with obj files.
class ObjData : public FileFormatDataBase
{
  public:
    std::string assetsPath;
    bool phong = false;
    TfToken originalColorSpace;
    static ObjDataRefPtr InitData(const SdfFileFormat::FileFormatArguments& args);
};

/// \ingroup usdobj
/// \brief SdfFileFormat specialization for working with obj files.
class USDOBJ_API UsdObjFileFormat
  : public SdfFileFormat
  , public PcpDynamicFileFormatInterface
{
  public:
    friend class ObjData;

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

    virtual bool ReadFromString(SdfLayer* layer, const std::string& input) const override;

    virtual bool WriteToFile(
      const SdfLayer& layer,
      const std::string& filePath,
      const std::string& comment = std::string(),
      const FileFormatArguments& args = FileFormatArguments()) const override;

    virtual bool WriteToStream(const SdfSpecHandle& spec,
                               std::ostream& out,
                               size_t indent) const override;

    virtual bool WriteToString(const SdfLayer& layer,
                               std::string* str,
                               const std::string& comment = std::string()) const override;

  protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;
    virtual ~UsdObjFileFormat();
    UsdObjFileFormat();

  private:
    static const TfToken assetsPathToken;
    static const TfToken phongToken;
    static const TfToken originalColorSpaceToken;

    bool ReadFromStream(SdfLayer* layer,
                        std::istream& input,
                        bool metadataOnly,
                        std::string* outError,
                        std::istream& mtlinput) const;
};

PXR_NAMESPACE_CLOSE_SCOPE
