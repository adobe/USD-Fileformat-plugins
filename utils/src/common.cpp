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
#include <fileformatutils/common.h>

#include <fileformatutils/debugCodes.h>

//#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/packageUtils.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <regex>
#include <sstream>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE
TF_DEFINE_PUBLIC_TOKENS(AdobeTokens, ADOBE_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(MtlXTokens, MATERIAL_X_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(OpenPbrTokens, OPEN_PBR_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(AdobeNgpTokens, ADOBE_NGP_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(AdobeGsplatBaseTokens, ADOBE_GSPLAT_BASE_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(AdobeGsplatSHTokens, ADOBE_GSPLAT_SH_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

using namespace PXR_NS;
namespace adobe::usd {

const double pi = 3.14159265358979323846f;
const double deg2rad = pi / 180.0f;
const double rad2deg = 1 / deg2rad;

void
argComposeString(const PXR_NS::PcpDynamicFileFormatContext& context,
                 PXR_NS::SdfFileFormat::FileFormatArguments* args,
                 const PXR_NS::TfToken& token,
                 const std::string& debugTag)
{
    VtValue value;
    if (context.ComposeValue(token, &value) && value.IsHolding<std::string>()) {
        const std::string& val = value.Get<std::string>();
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: ComposeFileFormatArg: %s = %s\n",
                     debugTag.c_str(),
                     token.GetText(),
                     val.c_str());
        (*args)[token.GetText()] = val;
    }
}

void
argComposeBool(const PXR_NS::PcpDynamicFileFormatContext& context,
               PXR_NS::SdfFileFormat::FileFormatArguments* args,
               const PXR_NS::TfToken& token,
               const std::string& debugTag)
{
    VtValue value;
    if (context.ComposeValue(token, &value) && value.IsHolding<bool>()) {
        std::string val = value.Get<bool>() ? "true" : "false";
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: ComposeFileFormatArg: %s = %s\n",
                     debugTag.c_str(),
                     token.GetText(),
                     val.c_str());
        (*args)[token.GetString()] = val;
    }
}

void
argComposeFloat(const PXR_NS::PcpDynamicFileFormatContext& context,
                PXR_NS::SdfFileFormat::FileFormatArguments* args,
                const PXR_NS::TfToken& token,
                const std::string& debugTag)
{
    VtValue value;
    if (context.ComposeValue(token, &value) && value.IsHolding<float>()) {
        std::string val = std::to_string(value.Get<float>());
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: ComposeFileFormatArg: %s = %s\n",
                     debugTag.c_str(),
                     token.GetText(),
                     val.c_str());
        (*args)[token.GetString()] = val;
    }
}

void
argComposeFloatArray(const PcpDynamicFileFormatContext& context,
                     SdfFileFormat::FileFormatArguments* args,
                     const TfToken& token,
                     const std::string& debugTag)
{
    VtValue value;
    if (context.ComposeValue(token, &value) && value.IsHolding<VtFloatArray>()) {
        const auto& floatArray = value.UncheckedGet<VtFloatArray>();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < floatArray.size(); ++i) {
            if (i > 0)
                oss << ",";
            oss << floatArray[i];
        }
        oss << "]";

        std::string val = oss.str();
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: ComposeFileFormatArg: %s = %s\n",
                     debugTag.c_str(),
                     token.GetText(),
                     val.c_str());
        (*args)[token.GetString()] = val;
    }
}

void
argReadString(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
              const std::string& arg,
              std::string& target,
              const std::string& debugTag)
{
    if (const auto& it = args.find(arg); it != args.end()) {
        target = it->second;
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Read string arg: \"%s\" = \"%s\"\n",
                     debugTag.c_str(),
                     arg.c_str(),
                     it->second.c_str());
    }
}

void
argReadString(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
              const std::string& arg,
              PXR_NS::TfToken& target,
              const std::string& debugTag)
{
    std::string targetStr;
    argReadString(args, arg, targetStr, debugTag);
    target = PXR_NS::TfToken(targetStr);
}

void
argReadBool(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
            const std::string& arg,
            bool& target,
            const std::string& debugTag)
{
    if (const auto& it = args.find(arg); it != args.end()) {
        target = it->second == "true" || it->second == "True";
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Read bool arg: \"%s\" = \"%s\"\n",
                     debugTag.c_str(),
                     arg.c_str(),
                     target ? "true" : "false");
    }
}

void
argReadFloat(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
             const std::string& arg,
             float& target,
             const std::string& debugTag)
{
    if (const auto& it = args.find(arg); it != args.end()) {
        target = std::stof(it->second);
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Read float arg: \"%s\" = \"%s\"\n",
                     debugTag.c_str(),
                     arg.c_str(),
                     it->second.c_str());
    }
}

void
argReadFloatArray(const SdfFileFormat::FileFormatArguments& args,
                  const std::string& arg,
                  VtFloatArray& target,
                  const std::string& debugTag)
{
    if (const auto& it = args.find(arg); it != args.end()) {
        std::string value = it->second;
        std::regex floatRegex(R"([-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?)");
        std::sregex_iterator begin(value.begin(), value.end(), floatRegex);
        std::sregex_iterator end;

        target.clear();
        for (std::sregex_iterator i = begin; i != end; ++i) {
            target.push_back(std::stof((*i).str()));
        }

        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Read float array arg: \"%s\" = \"%s\"\n",
                     debugTag.c_str(),
                     arg.c_str(),
                     it->second.c_str());
    }
}

std::string
getFileExtension(const std::string& filePath, const std::string& defaultValue = "")
{
    // Find the last dot position
    std::size_t dotPos = filePath.rfind('.');
    if (dotPos != std::string::npos && dotPos + 1 < filePath.size()) {
        return filePath.substr(dotPos + 1);
    }
    return defaultValue;
}

std::string
getCurrentDate()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
    return ss.str();
}

// Splits the input string into a vector of substrings based on the specified delimiter.
std::vector<std::string>
split(const std::string& str, char delimiter)
{
    std::vector<std::string> pieces;
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string::npos) {
        pieces.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delimiter, start);
    }
    pieces.push_back(str.substr(start));

    return pieces;
}

// Creates a directory at the specified path, including any necessary parent directories.
// Returns true if the directory was created successfully or already exists, false otherwise.
bool
createDirectory(const std::filesystem::path& directoryPath)
{
    try {
        std::filesystem::create_directories(directoryPath);
    } catch (const std::filesystem::filesystem_error& e) {
        TF_CODING_ERROR("Error creating directory:\n  \"{}\"", e.what());
        return false;
    }

    return true;
}

// Retrieves the file path associated with a given layer identifier.
// Parses the layer identifier to extract the outer and inner paths,
// and returns the inner path if available; otherwise, returns the outer path.
std::string
getLayerFilePath(const std::string& layerIdentifier)
{
    std::string layerPath;
    SdfLayer::FileFormatArguments arguments;
    SdfLayer::SplitIdentifier(layerIdentifier, &layerPath, &arguments);
    const auto [outer, inner] = ArSplitPackageRelativePathInner(layerPath);
    return inner.empty() ? outer : inner;
}

}