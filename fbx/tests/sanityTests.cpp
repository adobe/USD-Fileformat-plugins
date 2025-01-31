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
#include <gtest/gtest.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>

TEST(Sanity, LoadCube)
{
    PXR_NAMESPACE_USING_DIRECTIVE

    // Load an FBX
    UsdStageRefPtr stage = UsdStage::Open("SanityCube.fbx");
    ASSERT_TRUE(stage);
    UsdPrim mesh = stage->GetPrimAtPath(SdfPath("/SanityCube/Cube"));
    ASSERT_TRUE(mesh);
}
