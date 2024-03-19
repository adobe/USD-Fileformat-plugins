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
#include "dictEncoder.h"
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/js/json.h>

PXR_NAMESPACE_USING_DIRECTIVE
namespace adobe::usd::sbsar {

namespace DictEncoder {
void
writeDict(const VtDictionary& dict, std::ostream& output)
{
    JsObject o;
    for (const auto& kv : dict) {
        if (kv.second.IsHolding<int>()) {
            o[kv.first] = JsValue(kv.second.UncheckedGet<int>());
        } else if (kv.second.IsHolding<std::uint32_t>()){
            o[kv.first] = JsValue((std::uint64_t)kv.second.UncheckedGet<std::uint32_t>());
        } else if (kv.second.IsHolding<std::uint64_t>()) {
            o[kv.first] = JsValue(kv.second.UncheckedGet<std::uint64_t>());
        } else if (kv.second.IsHolding<size_t>()) {
            o[kv.first] = JsValue((std::uint64_t)kv.second.UncheckedGet<size_t>());
        } else if (kv.second.IsHolding<float>()) {
            o[kv.first] = JsValue(kv.second.UncheckedGet<float>());
        } else if (kv.second.IsHolding<double>()) {
            o[kv.first] = JsValue(kv.second.UncheckedGet<double>());
        } else if (kv.second.IsHolding<std::string>()) {
            o[kv.first] = JsValue(kv.second.UncheckedGet<std::string>());
        } else if (kv.second.IsHolding<GfVec2f>()) {
            GfVec2f v = kv.second.UncheckedGet<GfVec2f>();
            JsArray a;
            a.push_back(JsValue(v[0]));
            a.push_back(JsValue(v[1]));
            o[kv.first] = a;
        } else if (kv.second.IsHolding<GfVec3f>()) {
            GfVec3f v = kv.second.UncheckedGet<GfVec3f>();
            JsArray a;
            a.push_back(JsValue(v[0]));
            a.push_back(JsValue(v[1]));
            a.push_back(JsValue(v[2]));
            o[kv.first] = a;
        } else if (kv.second.IsHolding<GfVec4f>()) {
            GfVec4f v = kv.second.UncheckedGet<GfVec4f>();
            JsArray a;
            a.push_back(JsValue(v[0]));
            a.push_back(JsValue(v[1]));
            a.push_back(JsValue(v[2]));
            a.push_back(JsValue(v[3]));
            o[kv.first] = a;
        } else if (kv.second.IsHolding<GfVec2i>()) {
            GfVec2i v = kv.second.UncheckedGet<GfVec2i>();
            JsArray a;
            a.push_back(JsValue(v[0]));
            a.push_back(JsValue(v[1]));
            o[kv.first] = a;
        } else if (kv.second.IsHolding<GfVec3i>()) {
            GfVec3i v = kv.second.UncheckedGet<GfVec3i>();
            JsArray a;
            a.push_back(JsValue(v[0]));
            a.push_back(JsValue(v[1]));
            a.push_back(JsValue(v[2]));
            o[kv.first] = a;
        } else if (kv.second.IsHolding<GfVec4i>()) {
            GfVec4i v = kv.second.UncheckedGet<GfVec4i>();
            JsArray a;
            a.push_back(JsValue(v[0]));
            a.push_back(JsValue(v[1]));
            a.push_back(JsValue(v[2]));
            a.push_back(JsValue(v[3]));
            o[kv.first] = a;
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
    if (!error.reason.empty() || !v.IsObject()) {
        TF_RUNTIME_ERROR("Failed to parse json data");
        return {};
    }
    VtDictionary d;
    const JsObject& o = v.GetJsObject();
    for (const auto& oi : o) {
        if (oi.second.IsUInt64()) {
            d[oi.first] = VtValue(oi.second.GetUInt64());
        } else if (oi.second.IsInt()) {
            d[oi.first] = VtValue(oi.second.GetInt());
        } else if (oi.second.IsReal()) {
            d[oi.first] = VtValue(static_cast<float>(oi.second.GetReal()));
        } else if (oi.second.IsString()) {
            d[oi.first] = VtValue(oi.second.GetString());
        } else if (oi.second.IsArray()) {
            const JsArray& a = oi.second.GetJsArray();
            size_t sz = a.size();
            if (sz < 2 || sz > 4) {
                TF_RUNTIME_ERROR("Invalid array size %zu", sz);
            }
            if (a[0].IsInt()) {
                if (sz == 2) {
                    GfVec2i res{};
                    for (int i = 0; i < sz; ++i) {
                        res[i] = static_cast<float>(a[i].GetInt());
                    }
                    d[oi.first] = VtValue(res);
                } else if (sz == 3) {
                    GfVec3i res{};
                    for (int i = 0; i < sz; ++i) {
                        res[i] = static_cast<float>(a[i].GetInt());
                    }
                    d[oi.first] = VtValue(res);
                } else {
                    GfVec4i res{};
                    for (int i = 0; i < sz; ++i) {
                        res[i] = static_cast<float>(a[i].GetInt());
                    }
                    d[oi.first] = VtValue(res);
                }
            } else {
                if (sz == 2) {
                    GfVec3f res{};
                    for (int i = 0; i < sz; ++i) {
                        res[i] = static_cast<float>(a[i].GetReal());
                    }
                    d[oi.first] = VtValue(res);
                } else if (sz == 3) {
                    GfVec3f res{};
                    for (int i = 0; i < sz; ++i) {
                        res[i] = static_cast<float>(a[i].GetReal());
                    }
                    d[oi.first] = VtValue(res);
                } else {
                    GfVec4f res{};
                    for (int i = 0; i < sz; ++i) {
                        res[i] = static_cast<float>(a[i].GetReal());
                    }
                    d[oi.first] = VtValue(res);
                }
            }
        }
    }
    return d;
}
} // namespace DictEncoder
}
