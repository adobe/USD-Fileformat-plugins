/*
Copyright 2026 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#include <sandbox/utilities/utilities.h>

#include <gtest/gtest.h>

#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>

#include <string>
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE
using adobe::usd::sandbox::FindAndModifyAssetPaths;

namespace {

// Rewrite every visited asset value to "InMemory://<authored>" and record what was visited.
auto
makeRecordingRewriter(std::unordered_map<std::string, std::string>& visited)
{
    return [&visited](const std::string& authored, std::string& newName) {
        visited[authored] = authored;
        newName = "InMemory://" + authored;
        return true;
    };
}

}

// A subverted worker can hide a reference in a time-sampled (animated) asset attribute that has no
// default value. The traversal must still visit that value and be able to rewrite it in place.
TEST(FindAndModifyAssetPaths, VisitsAndRewritesTimeSampledAssetValueWithNoDefault)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    SdfPrimSpecHandle prim = SdfPrimSpec::New(layer, "Prim", SdfSpecifierDef);
    SdfAttributeSpecHandle attr = SdfAttributeSpec::New(prim, "tex", SdfValueTypeNames->Asset);
    const SdfPath attrPath = attr->GetPath();

    // The reference lives ONLY in a time sample, never the default value.
    layer->SetTimeSample(attrPath, 0.0, VtValue(SdfAssetPath("hidden.png")));

    std::unordered_map<std::string, std::string> visited;
    FindAndModifyAssetPaths(layer, makeRecordingRewriter(visited));

    // Visited...
    ASSERT_EQ(visited.count("hidden.png"), 1u);

    // ...and rewritten in the time sample.
    VtValue sample;
    ASSERT_TRUE(layer->QueryTimeSample(attrPath, 0.0, &sample));
    ASSERT_TRUE(sample.IsHolding<SdfAssetPath>());
    EXPECT_EQ(sample.UncheckedGet<SdfAssetPath>().GetAssetPath(), "InMemory://hidden.png");
}

// Regression lock: the pre-existing default-value path keeps working unchanged.
TEST(FindAndModifyAssetPaths, StillVisitsAndRewritesDefaultAssetValue)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    SdfPrimSpecHandle prim = SdfPrimSpec::New(layer, "Prim", SdfSpecifierDef);
    SdfAttributeSpecHandle attr = SdfAttributeSpec::New(prim, "tex", SdfValueTypeNames->Asset);
    attr->SetDefaultValue(VtValue(SdfAssetPath("model.png")));

    std::unordered_map<std::string, std::string> visited;
    FindAndModifyAssetPaths(layer, makeRecordingRewriter(visited));

    ASSERT_EQ(visited.count("model.png"), 1u);
    EXPECT_EQ(attr->GetDefaultValue().UncheckedGet<SdfAssetPath>().GetAssetPath(),
              "InMemory://model.png");
}
