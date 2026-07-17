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

#include <sandbox/utilities/quarantine.h>

#include <gtest/gtest.h>

#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>

#include <algorithm>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

using adobe::usd::sandbox::CollectQuarantinedReferences;
using adobe::usd::sandbox::IsQuarantined;
using adobe::usd::sandbox::QuarantineReference;
using adobe::usd::sandbox::RevealQuarantinedReference;

// The BadAsset:// scheme resolves to an inert path: Resolve() echoes the URI unchanged (so it is a
// stable, opaque handle), but OpenAsset() yields nothing. This is what makes a quarantined
// reference safe to carry in a stage without ever granting access to the underlying bytes.
TEST(BadAssetResolver, ResolvesInert)
{
    const std::string uri = "BadAsset://L2V0Yy9wYXNzd2Q"; // encodes /etc/passwd
    auto resolved = ArGetResolver().Resolve(uri);
    // The BadAsset resolver is discovered through the USD plugin registry (PXR_PLUGINPATH_NAME
    // pointed at the sandbox proxy plugins). The package-install / CI test env sets that up; a bare
    // developer `ctest` after `cmake && make` does not, so skip rather than report a false failure.
    // The quarantine-helper tests below need no plugin and always run.
    if (resolved.GetPathString() != uri) {
        GTEST_SKIP()
          << "BadAsset resolver not registered (PXR_PLUGINPATH_NAME not pointed at the "
             "sandbox proxy plugins); validated under the package-install / CI test env.";
    }
    EXPECT_EQ(resolved.GetPathString(), uri);
    EXPECT_EQ(ArGetResolver().OpenAsset(resolved), nullptr);
}

TEST(Quarantine, RoundTripThroughReveal)
{
    const std::string q = QuarantineReference("/etc/passwd");
    EXPECT_EQ(q, "BadAsset://L2V0Yy9wYXNzd2Q");
    EXPECT_TRUE(IsQuarantined(q));
    ASSERT_TRUE(RevealQuarantinedReference(q).has_value());
    EXPECT_EQ(*RevealQuarantinedReference(q), "/etc/passwd");
}

TEST(Quarantine, RejectsNonQuarantine)
{
    EXPECT_FALSE(RevealQuarantinedReference("/etc/passwd").has_value());
    EXPECT_FALSE(IsQuarantined("model.fbx[t.png]"));
}

TEST(Quarantine, RejectsEmbeddedNulOnReveal)
{
    const std::string q = QuarantineReference(std::string("/allowed/x\0/../etc/passwd", 25));
    EXPECT_FALSE(RevealQuarantinedReference(q).has_value()); // NUL-truncation bypass closed
}

TEST(Quarantine, RefusesDoubleWrap)
{
    const std::string once = QuarantineReference("/etc/passwd");
    EXPECT_EQ(QuarantineReference(once), once); // no nesting on re-import/round-trip
}

TEST(Quarantine, HandlesEmptyAndShortReferences)
{
    // An empty reference quarantines to the bare scheme (empty payload) and reveals to empty.
    const std::string qEmpty = QuarantineReference("");
    EXPECT_EQ(qEmpty, "BadAsset://");
    EXPECT_TRUE(IsQuarantined(qEmpty));
    ASSERT_TRUE(RevealQuarantinedReference(qEmpty).has_value());
    EXPECT_EQ(*RevealQuarantinedReference(qEmpty), "");

    // 1- and 2-byte references round-trip through quarantine + reveal.
    for (const std::string& ref : { std::string("x"), std::string("ab") }) {
        const std::string q = QuarantineReference(ref);
        EXPECT_TRUE(IsQuarantined(q));
        ASSERT_TRUE(RevealQuarantinedReference(q).has_value());
        EXPECT_EQ(*RevealQuarantinedReference(q), ref);
    }
}

TEST(Quarantine, RejectsMalformedShortPayload)
{
    // The encoder never emits a length-1 (mod 4) payload, so a 1-character payload is malformed;
    // reveal must reject it rather than return garbage.
    EXPECT_FALSE(RevealQuarantinedReference("BadAsset://A").has_value());
    // The bare scheme (empty payload) is well-formed and reveals to the empty string.
    EXPECT_TRUE(RevealQuarantinedReference("BadAsset://").has_value());
}

TEST(Quarantine, CollectsFromLayerIncludingTimeSamples)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    SdfPrimSpecHandle prim = SdfPrimSpec::New(layer, "Prim", SdfSpecifierDef);

    // One quarantined value in a default, another only in a time sample.
    const std::string qDefault = QuarantineReference("/etc/passwd");
    const std::string qSample = QuarantineReference("../../secret");

    SdfAttributeSpecHandle defAttr =
      SdfAttributeSpec::New(prim, "defTex", SdfValueTypeNames->Asset);
    defAttr->SetDefaultValue(VtValue(SdfAssetPath(qDefault)));

    SdfAttributeSpecHandle tsAttr = SdfAttributeSpec::New(prim, "tsTex", SdfValueTypeNames->Asset);
    layer->SetTimeSample(tsAttr->GetPath(), 0.0, VtValue(SdfAssetPath(qSample)));

    // A non-quarantined asset value must not be collected.
    SdfAttributeSpecHandle okAttr = SdfAttributeSpec::New(prim, "okTex", SdfValueTypeNames->Asset);
    okAttr->SetDefaultValue(VtValue(SdfAssetPath("model.fbx[texture.png]")));

    const std::vector<std::string> collected = CollectQuarantinedReferences(layer);
    EXPECT_EQ(collected.size(), 2u);
    EXPECT_NE(std::find(collected.begin(), collected.end(), qDefault), collected.end());
    EXPECT_NE(std::find(collected.begin(), collected.end(), qSample), collected.end());
}
