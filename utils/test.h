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

/// \file test.h
///
/// Set of utilities functions for testing.
/// These functions are as simple as they can be, and don't share code with the main body of code.
///

#include "api.h"
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/points.h>



#define TEST_TOKENS \
    (invalid) \
    (r) \
    (g) \
    (b) \
    (a) \
    (rgb) \
    (rgba) \
    (repeat) \
    (clamp) \
    (wrapS) \
    (wrapT) \
    (mirror) \
    (sourceColorSpace) \
    (result) \
    (raw) \
    (sRGB) \
    (st) \
    (file) \
    (scale) \
    (bias) \
    (normals) \
    (tangents) \
    (varname) \
    (UsdUVTexture) \
    (UsdPrimvarReader_float2) \
    (UsdTransform2d) \
    ((frame_stPrimvarName, "frame:stPrimvarName")) \
    (surface) \
    (UsdPreviewSurface) \
    (useSpecularWorkflow) \
    (diffuseColor) \
    (emissiveColor) \
    (specularColor) \
    (normal) \
    (metallic) \
    (roughness) \
    (clearcoat) \
    (clearcoatRoughness) \
    (opacity) \
    (opacityThreshold) \
    (displacement) \
    (occlusion) \
    (ior) \

PXR_NAMESPACE_OPEN_SCOPE
TF_DECLARE_PUBLIC_TOKENS(TestTokens, USDFFUTILS_API, TEST_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

#define ASSERT_PRIM(...) assertPrim(__VA_ARGS__)
#define ASSERT_NODE(...) assertNode(__VA_ARGS__)
#define ASSERT_MESH(...) assertMesh(__VA_ARGS__)
#define ASSERT_POINTS(...) assertPoints(__VA_ARGS__)
#define ASSERT_MATERIAL(...) assertMaterial(__VA_ARGS__)
#ifdef DO_RENDER
    #define ASSERT_RENDER(...) assertRender(__VA_ARGS__)
#else
    #define ASSERT_RENDER(...) {}
#endif

template<typename T>
struct USDFFUTILS_API ArrayData {
    size_t size;
    PXR_NS::VtArray<T> values; // a subset of the expected array data
};

template<typename T>
struct USDFFUTILS_API PrimvarData {
    PXR_NS::TfToken interpolation;
    ArrayData<T> values;
    ArrayData<int> indices;
};

struct USDFFUTILS_API MeshData {
    ArrayData<int> faceVertexCounts;
    ArrayData<int> faceVertexIndices;
    ArrayData<PXR_NS::GfVec3f> points;
    PrimvarData<PXR_NS::GfVec3f> normals;
    PrimvarData<PXR_NS::GfVec2f> uvs;
    PrimvarData<PXR_NS::GfVec3f> displayColor;
    PrimvarData<float> displayOpacity;
};

struct USDFFUTILS_API PointsData {
    size_t pointsCount;
};

struct USDFFUTILS_API InputData {
    PXR_NS::VtValue value;
    int uvIndex;
    PXR_NS::TfToken channel;
    PXR_NS::TfToken wrapS;
    PXR_NS::TfToken wrapT;
    PXR_NS::TfToken colorspace;
    PXR_NS::VtValue scale;
    PXR_NS::VtValue bias;
    PXR_NS::VtValue transformRotation;
    PXR_NS::VtValue transformScale;
    PXR_NS::VtValue transformTranslation;
    std::string file; // a relative path to the current binary dir
};

struct USDFFUTILS_API MaterialData {
    InputData useSpecularWorkflow;
    InputData diffuseColor;
    InputData emissiveColor;
    InputData specularColor;
    InputData normal;
    InputData metallic;
    InputData roughness;
    InputData clearcoat;
    InputData clearcoatRoughness;
    InputData opacity;
    InputData opacityThreshold;
    InputData displacement;
    InputData occlusion;
    InputData ior;
};

USDFFUTILS_API void assertPrim(PXR_NS::UsdStageRefPtr stage, const std::string& path);
USDFFUTILS_API void assertNode(PXR_NS::UsdStageRefPtr stage, const std::string& path);
USDFFUTILS_API void assertMesh(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MeshData& data);
USDFFUTILS_API void assertPoints(PXR_NS::UsdStageRefPtr stage, const std::string& path, const PointsData& data);
USDFFUTILS_API void assertMaterial(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MaterialData& data);

USDFFUTILS_API void assertRender(const std::string& filename, const std::string& imageFilename);