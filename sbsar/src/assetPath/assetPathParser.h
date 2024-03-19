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
#pragma once

#include "api.h"
#include <pxr/base/js/json.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>

namespace adobe::usd::sbsar {
// TODO: Get rid of JS dependency in header file?
struct ParsePathResult
{
    enum AssetType
    {
        AT_IMAGE = 0
    };
    enum BindType
    {
        BT_USAGE = 0,
        BT_IDENTIFIER,
        BT_UNDEFINED
    };
    enum ParseError
    {
        PE_SUCCESS = 0,
        PE_INVALID_FORMAT,
        PE_INVALID_ASSET_TYPE,
    };
    AssetType at;
    BindType bt;
    std::string graphName;
    std::string usage;
    std::string preset;
    std::size_t packageHash = 0;
    std::string inputParameters;
    PXR_NS::JsValue parameters;
    ParsePathResult()
      : at(AT_IMAGE)
      , bt(BT_UNDEFINED)
      , parameters(PXR_NS::JsValue(PXR_NS::JsObject()))
    {}
};
// Parse a path to a ParsePathResult
USDSBSAR_API ParsePathResult::ParseError
parsePath(const std::string& packagedPath, ParsePathResult& output);

// generate a path from a parsed path
ParsePathResult::ParseError
generatePath(const ParsePathResult& parsedResult, std::string& output);

//! Helper to read JSValue
bool
getAsFloat(const PXR_NS::JsValue& v, float& res);
bool
getAsInt(const PXR_NS::JsValue& v, int& res);
bool
getAsDoubleArray(const PXR_NS::JsValue& v, std::vector<double>& res);
bool
getAsIntArray(const PXR_NS::JsValue& v, std::vector<int>& res);

}
