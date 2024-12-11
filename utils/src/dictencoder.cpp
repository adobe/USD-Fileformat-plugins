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
#include <fileformatutils/dictencoder.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/js/json.h>

using namespace PXR_NS;
namespace adobe::usd {

void
writeDict(const VtDictionary& dict, std::ostream& output)
{
    JsObject o;
    for (auto kv : dict) {
        const std::string typeName = kv.second.GetType().GetTypeName();
        if (typeName == "int") {
            o[kv.first] = JsValue(kv.second.UncheckedGet<int>());
        } else if (typeName == "string") {
            o[kv.first] = JsValue(kv.second.UncheckedGet<std::string>());
        } else if (typeName == "float") {
            o[kv.first] = JsValue(kv.second.UncheckedGet<float>());
        } else if (typeName == "double") {
            o[kv.first] = JsValue(kv.second.UncheckedGet<double>());
        } else if (typeName == "GfVec3f") {
            GfVec3f v = kv.second.UncheckedGet<GfVec3f>();
            JsArray a;
            a.push_back(JsValue(v[0]));
            a.push_back(JsValue(v[1]));
            a.push_back(JsValue(v[2]));
            o[kv.first] = a;
        } else if (typeName == "bool") {
            o[kv.first] = JsValue(kv.second.UncheckedGet<bool>());
        } else {
            TF_WARN("Unsupported dict value %s: %s",
                    kv.first.c_str(),
                    kv.second.GetType().GetTypeName().c_str());
        }
    }
    JsWriteToStream(o, output);
}

VtDictionary
readDict(std::istream& input)
{
    JsParseError error;
    JsValue v = JsParseStream(input, &error);
    if (error.reason != "" || !v.IsObject()) {
        TF_RUNTIME_ERROR("Failed to parse json data");
        return {};
    }
    VtDictionary d;
    const JsObject& o = v.GetJsObject();
    for (auto oi : o) {
        if (oi.second.IsInt()) {
            d[oi.first] = VtValue(oi.second.GetInt());
        } else if (oi.second.IsReal()) {
            d[oi.first] = VtValue(static_cast<float>(oi.second.GetReal()));
        } else if (oi.second.IsString()) {
            d[oi.first] = VtValue(static_cast<std::string>(oi.second.GetString()));
        } else if (oi.second.IsArray()) {
            const JsArray& a = oi.second.GetJsArray();
            if (a.size() != 3) {
                TF_RUNTIME_ERROR("Invalid array size %zu", a.size());
            }
            GfVec3f res;
            for (int i = 0; i < 3; ++i) {
                res[i] = a[i].GetReal();
            }
            d[oi.first] = VtValue(res);
        } else if (oi.second.IsBool()) {
            d[oi.first] = VtValue(static_cast<bool>(oi.second.GetBool()));
        }
    }
    return d;
}

}
