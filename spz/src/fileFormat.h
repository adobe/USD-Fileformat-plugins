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
#pragma once
#include "api.h"
#include <iosfwd>
#include <fileformatutils/sdfUtils.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>
#include <string>
#include <version.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#define USDSPZ_FILE_FORMAT_TOKENS \
    ((Id, "spz")) \
    ((Version, FILE_FORMATS_VERSION)) \
    ((Target, "usd")) \
    ((gsplatsWithZup, "spzGsplatsWithZup")) \
    ((gsplatsClippingBox, "spzGsplatsClippingBox"))
// clang-format on

TF_DECLARE_PUBLIC_TOKENS(UsdSpzFileFormatTokens, USDSPZ_FILE_FORMAT_TOKENS);
TF_DECLARE_WEAK_AND_REF_PTRS(SpzData);
TF_DECLARE_WEAK_AND_REF_PTRS(UsdSpzFileFormat);

/// \ingroup usdspz
/// \brief SdfData specialization for working with spz files.
class SpzData : public FileFormatDataBase
{
  public:
    bool gsplatsWithZup = false;
    PXR_NS::VtFloatArray gsplatsClippingBox = { -2.0, -2.0, -2.0, 2.0, 2.0, 2.0 };
    static SpzDataRefPtr InitData(const SdfFileFormat::FileFormatArguments& args);
};

/// \ingroup usdspz
/// \brief SdfFileFormat specialization for working with spz files.
class USDSPZ_API UsdSpzFileFormat
  : public SdfFileFormat
  , public PcpDynamicFileFormatInterface
{
  public:
    friend class SpzData;

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
    static const TfToken gsplatsWithZupToken;
    static const TfToken gsplatsWithClippingToken;

    SDF_FILE_FORMAT_FACTORY_ACCESS;
    virtual ~UsdSpzFileFormat();
    UsdSpzFileFormat();

  private:
    bool ReadFromStream(SdfLayer* layer,
                        std::istream& input,
                        bool metadataOnly,
                        std::string* outError,
                        std::istream& mtlinput) const;
};

PXR_NAMESPACE_CLOSE_SCOPE
