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

#include <fileformatutils/sdfUtils.h>
#include <sandbox/fileformat/api.h>

#include <version.h>

#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/pcp/dynamicFileFormatInterface.h>

#include <filesystem>
#include <iosfwd>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
#define USDSANDBOXPROXY_FILE_FORMAT_TOKENS \
    ((Id, "sandbox")) \
    ((Version, FILE_FORMATS_VERSION)) \
    ((Target, "usd")) \
    ((SandboxExecutableRelativePath, "")) \
    ((UnsafePluginRelativePath, "")) \
    ((SandboxLibraryPath, ""))
// clang-format on

TF_DECLARE_PUBLIC_TOKENS(UsdSandboxProxyFileFormatTokens, USDSANDBOXPROXY_FILE_FORMAT_TOKENS);
TF_DECLARE_WEAK_AND_REF_PTRS(UsdSandboxProxyFileFormat);
class ArAsset;

/**
 * Proxy plugin that allows for sandboxing other fileformat plugins. This plugin will be seen
 * externally and act as an entry point for the sandboxed fileformat plugins, but under the hood,
 * it will launch a separate sandboxed process with fewer permissions to perform the actual
 * fileformat conversion operations.
 */
class USDSANDBOXPROXY_API UsdSandboxProxyFileFormat
  : public SdfFileFormat
  , public PcpDynamicFileFormatInterface
{
public:
    void ComposeFieldsForFileFormatArguments(const std::string& assetPath,
                                             const PcpDynamicFileFormatContext& context,
                                             FileFormatArguments* args,
                                             VtValue* dependencyContextData) const override;

    bool CanRead(const std::string& file) const override;

    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;

    bool ReadFromString(SdfLayer* layer, const std::string& str) const override;

    bool WriteToString(const SdfLayer& layer,
                       std::string* str,
                       const std::string& comment = std::string()) const override;

    bool WriteToStream(const SdfSpecHandle& spec, std::ostream& out, size_t indent) const override;

    bool WriteToFile(const SdfLayer& layer,
                     const std::string& filePath,
                     const std::string& comment = std::string(),
                     const FileFormatArguments& args = FileFormatArguments()) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    virtual ~UsdSandboxProxyFileFormat();

    UsdSandboxProxyFileFormat();
    std::filesystem::path _sandboxExecutablePath;
    std::filesystem::path _proxyPluginPath;
    std::filesystem::path _unsafePluginRoot;
};

PXR_NAMESPACE_CLOSE_SCOPE
