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
#include <fileformatutils/sdfUtils.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <string>
#include <version.h>

PXR_NAMESPACE_OPEN_SCOPE

#define USDSTL_FILE_FORMAT_TOKENS ((Id, "stl"))((Version, FILE_FORMATS_VERSION))((Target, "usd"))
TF_DECLARE_PUBLIC_TOKENS(UsdStlFileFormatTokens, USDSTL_FILE_FORMAT_TOKENS);
TF_DECLARE_WEAK_AND_REF_PTRS(UsdStlFileFormat);

/// \ingroup usdstl
/// \brief SdfFileFormat specialization for working with stl files.
class USDSTL_API UsdStlFileFormat : public SdfFileFormat
{
  public:
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
    virtual ~UsdStlFileFormat();
    UsdStlFileFormat();

  private:
};

PXR_NAMESPACE_CLOSE_SCOPE
