/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include <assetPath/assetPathParser.h>
#include <sbsarDebug.h>

#include <pxr/base/tf/diagnostic.h>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sbsar {
namespace {

void
splitByDelimiter(const std::string& str, char delimiter, std::vector<std::string>& output)
{
    size_t pos_start = 0;
    while (true) {
        // find the next /
        size_t pos_next = str.find(delimiter, pos_start);
        if (pos_next == std::string::npos) {
            output.push_back(str.substr(pos_start));
            // found the end, stop
            break;
        }
        output.push_back(str.substr(pos_start, pos_next - pos_start));
        pos_start = pos_next + 1;
    }
}

JsValue
parsePathParameter(const std::string& pathParameters)
{
    JsParseError e;
    JsValue res = JsParseString(pathParameters, &e);
    if (res.IsNull()) {
        TF_RUNTIME_ERROR(
          "Parameter parse error: %s, line: %d, column: %d", e.reason.c_str(), e.line, e.column);
        TF_RUNTIME_ERROR("Parameter parse error: %s", pathParameters.c_str());
        return JsValue();
    }
    if (!res.IsObject()) {
        TF_RUNTIME_ERROR("Failed to parse parameters, needs to be an object at root");
        return JsValue();
    }
    // TF_STATUS("Parsing parameters successful!");
    return res;
}

} // namespace

ParsePathResult::ParseError
parsePath(const std::string& packagedPath, ParsePathResult& output)
{
    TF_DEBUG(SBSAR_PACKAGE_RESOLVER).Msg("Parsing package path %s\n", packagedPath.c_str());
    const std::string& trimmedPath = packagedPath;

    // TF_STATUS("Trimmed Path: %s", trimmedPath.c_str());
    std::vector<std::string> delimiter_split;
    splitByDelimiter(trimmedPath, '/', delimiter_split);
    if (delimiter_split.size() != 3) {
        TF_RUNTIME_ERROR("Path format error, invalid path count %lu: %s",
                         delimiter_split.size(),
                         trimmedPath.c_str());
        return ParsePathResult::PE_INVALID_FORMAT;
    }
    if (delimiter_split[0] != "graphs") {
        TF_RUNTIME_ERROR("Path format error, only assets at /graphs supported");
        return ParsePathResult::PE_INVALID_FORMAT;
    }
    output.graphName = delimiter_split[1];

    std::vector<std::string> parameter_string_split;
    splitByDelimiter(delimiter_split[2], '?', parameter_string_split);
    if (parameter_string_split.size() != 2) {
        TF_RUNTIME_ERROR("Path format error, only a single ? support %zu",
                         parameter_string_split.size());
        return ParsePathResult::PE_INVALID_FORMAT;
    }
    if (parameter_string_split[0] != "images") {
        TF_RUNTIME_ERROR("Path format error, only image resources supported");
        return ParsePathResult::PE_INVALID_ASSET_TYPE;
    }

    std::vector<std::string> parameter_split;
    splitByDelimiter(parameter_string_split[1], '#', parameter_split);
    output.inputParameters = "";
    for (const auto& p : parameter_split) {
        std::vector<std::string> parameter_data_split;
        splitByDelimiter(p, '=', parameter_data_split);
        if (parameter_data_split.size() != 2) {
            TF_RUNTIME_ERROR("Path format error, Only a single = in a parameters");
            return ParsePathResult::PE_INVALID_FORMAT;
        }
        const std::string& param_name = parameter_data_split[0];
        const std::string& param_data = parameter_data_split[1];

        if (param_name == "usage" || param_name == "identifier") {
            if (output.bt != ParsePathResult::BT_UNDEFINED) {
                TF_RUNTIME_ERROR("Path format error, Only a single usage or "
                                 "identifier supported");
            }
            output.bt =
              (param_name == "usage") ? ParsePathResult::BT_USAGE : ParsePathResult::BT_IDENTIFIER;
            output.usage = param_data;
        } else if (param_name == "params") {
            JsValue paramParseResult = parsePathParameter(param_data);
            if (paramParseResult.IsNull()) {
                TF_RUNTIME_ERROR("Failed to parse parameters");
                return ParsePathResult::PE_INVALID_FORMAT;
            }
            output.inputParameters += param_data;
            output.parameters = paramParseResult;
        } else if (param_name == "entries") {

        } else if (param_name == "preset") {
            if (!output.preset.empty()) {
                TF_RUNTIME_ERROR("Path format error, preset can only be given once");
                return ParsePathResult::PE_INVALID_FORMAT;
            }
            output.preset = param_data;
        } else if (param_name == "packageHash") {
            output.packageHash = std::stoull(param_data, nullptr, 16);
        } else {
            TF_RUNTIME_ERROR("Path format error, %s is not supported parameter",
                             param_name.c_str());
            return ParsePathResult::PE_INVALID_FORMAT;
        }
    }
    // TF_STATUS("Path parse successful");
    return ParsePathResult::PE_SUCCESS;
}

ParsePathResult::ParseError
generatePath(const ParsePathResult& parsedResult, std::string& output)
{
    std::stringstream result;
    TF_AXIOM(parsedResult.at == ParsePathResult::AT_IMAGE);
    result << "graphs/" << parsedResult.graphName << "/images?"
           << ((parsedResult.bt == ParsePathResult::BT_USAGE) ? "usage=" : "identifier=")
           << parsedResult.usage;
    // Don't write preset if it's __default__ or empty
    if (!(parsedResult.preset == "__default__" || parsedResult.preset.empty())) {
        result << "#preset=" << parsedResult.preset;
    }
    if (parsedResult.packageHash != 0) {
        result << "#packageHash=" << std::hex << parsedResult.packageHash << std::dec;
    }
    result << "#params=";
    // JsWriteToStream(parsedResult.parameters, result);
    JsWriter w(result);
    JsWriteValue(&w, parsedResult.parameters);
    output = result.str();
    return ParsePathResult::PE_SUCCESS;
}

bool
getAsFloat(const PXR_NS::JsValue& v, float& res)
{
    if (v.IsInt()) {
        res = static_cast<float>(v.GetInt());
        return true;
    }
    if (v.IsReal()) {
        res = static_cast<float>(v.GetReal());
        return true;
    }
    return false;
}
bool
getAsInt(const PXR_NS::JsValue& v, int& res)
{
    if (v.IsInt()) {
        res = v.GetInt();
        return true;
    }
    if (v.IsReal()) {
        res = static_cast<int>(v.GetReal());
        TF_WARN("Converting float to int when applying value");
        return true;
    }
    return false;
}
bool
getAsDoubleArray(const PXR_NS::JsValue& v, std::vector<double>& res)
{
    res = v.GetArrayOf<double>();
    return true;
}
bool
getAsIntArray(const PXR_NS::JsValue& v, std::vector<int>& res)
{
    res = v.GetArrayOf<int>();
    return true;
}

}
