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

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/xform.h>

#include <gtest/gtest.h>

#define TEST_TOKENS                                                                                                                                                                                             \
    (invalid)(r)(                                                                                                                                                                                               \
      g)(b)(a)(rgb)(rgba)(repeat)(clamp)(wrapS)(wrapT)(mirror)(sourceColorSpace)(result)(raw)(sRGB)(st)(file)(scale)(bias)(normals)(tangents)(varname)(UsdUVTexture)(UsdPrimvarReader_float2)(UsdTransform2d)(( \
      frame_stPrimvarName, "frame:stPrimvarName"))(                                                                                                                                                             \
      surface)(UsdPreviewSurface)(useSpecularWorkflow)(diffuseColor)(emissiveColor)(specularColor)(normal)(metallic)(roughness)(clearcoat)(clearcoatRoughness)(opacity)(opacityThreshold)(displacement)(occlusion)(ior)

PXR_NAMESPACE_OPEN_SCOPE
TF_DECLARE_PUBLIC_TOKENS(TestTokens, USDFFUTILS_API, TEST_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

#define ASSERT_PRIM(...) ASSERT_TRUE(assertPrim(__VA_ARGS__))
#define ASSERT_NODE(...) ASSERT_TRUE(assertNode(__VA_ARGS__))
#define ASSERT_MESH(...) ASSERT_TRUE(assertMesh(__VA_ARGS__))
#define ASSERT_POINTS(...) ASSERT_TRUE(assertPoints(__VA_ARGS__))
#define ASSERT_MATERIAL(...) ASSERT_TRUE(assertMaterial(__VA_ARGS__))
#define ASSERT_ANIMATION(...) ASSERT_TRUE(assertAnimation(__VA_ARGS__))
#define ASSERT_CAMERA(...) ASSERT_TRUE(assertCamera(__VA_ARGS__))
#define ASSERT_LIGHT(...) ASSERT_TRUE(assertLight(__VA_ARGS__))
#define ASSERT_DISPLAY_NAME(...) ASSERT_TRUE(assertDisplayName(__VA_ARGS__))
#define ASSERT_VISIBILITY(...) ASSERT_TRUE(assertVisibility(__VA_ARGS__))
#ifdef DO_RENDER
#define ASSERT_RENDER(filename, imageFilename) ASSERT_TRUE(assertRender(filename, imageFilename))
#else
#define ASSERT_RENDER(...) \
    {}
#endif

// XXX This duplication of structs is highly suspicious
template<typename T>
struct USDFFUTILS_API ArrayData
{
    size_t size;
    PXR_NS::VtArray<T> values; // a subset of the expected array data
};

template<typename T>
struct USDFFUTILS_API PrimvarData
{
    PXR_NS::TfToken interpolation;
    ArrayData<T> values;
    ArrayData<int> indices;
};

struct USDFFUTILS_API MeshData
{
    ArrayData<int> faceVertexCounts;
    ArrayData<int> faceVertexIndices;
    ArrayData<PXR_NS::GfVec3f> points;
    PrimvarData<PXR_NS::GfVec3f> normals;
    PrimvarData<PXR_NS::GfVec4f> tangents;
    PrimvarData<PXR_NS::GfVec3f> bitangents;
    PrimvarData<PXR_NS::GfVec2f> uvs;
    PrimvarData<PXR_NS::GfVec3f> displayColor;
    PrimvarData<float> displayOpacity;
};

struct USDFFUTILS_API PointsData
{
    size_t pointsCount;
};

struct USDFFUTILS_API InputData
{
    PXR_NS::VtValue value;
    int uvIndex;
    PXR_NS::TfToken channel;
    PXR_NS::TfToken wrapS;
    PXR_NS::TfToken wrapT;
    PXR_NS::TfToken colorspace;
    PXR_NS::VtValue scale;
    PXR_NS::VtValue bias;
    PXR_NS::VtValue uvRotation;
    PXR_NS::VtValue uvScale;
    PXR_NS::VtValue uvTranslation;
    std::string file; // a relative path to the current binary dir
};

struct USDFFUTILS_API MaterialData
{
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
    InputData anisotropyAngle;
    InputData anisotropyLevel;
};

struct USDFFUTILS_API AnimationData
{
    std::map<float, PXR_NS::GfQuatf> orient;
    std::map<float, PXR_NS::GfVec3f> scale;
    std::map<float, PXR_NS::GfVec3d> translate;
};

struct USDFFUTILS_API CameraData
{
    PXR_NS::GfQuatf orient;
    PXR_NS::GfVec3f scale;
    PXR_NS::GfVec3d translate;

    PXR_NS::GfVec2f clippingRange;
    float focalLength;
    float focusDistance;
    float fStop;
    float horizontalAperture;
    std::string projection;
    float verticalAperture;
};

struct USDFFUTILS_API LightData
{
    // Light transformation data
    std::optional<PXR_NS::GfVec3d> translation;
    std::optional<PXR_NS::GfQuatf> rotation;
    std::optional<PXR_NS::GfVec3f> scale;

    // Light data
    std::optional<PXR_NS::GfVec3f> color;
    std::optional<float> intensity;
    std::optional<float> coneAngle;
    std::optional<float> coneFalloff;
    std::optional<float> radius;

    // Add these in when we support importing lights that use these
    // PXR_NS::GfVec2f length
    // ImageAsset texture
};

[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertPrim(PXR_NS::UsdStageRefPtr stage, const std::string& path);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertNode(PXR_NS::UsdStageRefPtr stage, const std::string& path);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertMesh(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MeshData& data);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertPoints(PXR_NS::UsdStageRefPtr stage, const std::string& path, const PointsData& data);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertMaterial(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MaterialData& data);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertAnimation(PXR_NS::UsdStageRefPtr stage, const std::string& path, const AnimationData& data);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertCamera(PXR_NS::UsdStageRefPtr stage, const std::string& path, const CameraData& data);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertLight(PXR_NS::UsdStageRefPtr stage, const std::string& path, const LightData& data);
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertDisplayName(PXR_NS::UsdStageRefPtr stage,
                  const std::string& primPath,
                  const std::string& displayName);
/**
 * Assert that a prim has a visibility attribute and that it is set to the expected value
 *
 * @param stage The stage containing the prim
 * @param path The path to the prim
 * @param expectedVisibilityAttr If the prim is expected to be set as inherited or invisible, when
 * the visibility attribute is checked with UsdGeomImageable::GetVisibilityAttr()
 * @param expectedActualVisibility If the prim is expected to be visible or invisible, when the
 * effective visibility is computed with UsdGeomImageable::ComputeVisibility()
 */
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertVisibility(PXR_NS::UsdStageRefPtr stage,
                 const std::string& path,
                 bool expectedVisibilityAttr,
                 bool expectedActualVisibility);

[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertRender(const std::string& filename, const std::string& imageFilename);

/// Compares a USD layer against a baseline USDA file.
/// If generateBaseline is true, exports the layer to the baseline path instead of comparing.
/// If dumpOnFailure is true, writes the actual output next to the baseline when comparison fails.
[[nodiscard]] USDFFUTILS_API ::testing::AssertionResult
assertUsda(const PXR_NS::SdfLayerHandle& sdfLayer,
           const std::string& baselinePath,
           bool generateBaseline = false,
           bool dumpOnFailure = false);

template<class T>
bool
extractUsdAttribute(PXR_NS::UsdPrim prim,
                    PXR_NS::TfToken attributeName,
                    T* value,
                    PXR_NS::UsdTimeCode time = PXR_NS::UsdTimeCode::Default())
{
    PXR_NS::UsdAttribute attribute = prim.GetAttribute(attributeName);

    PXR_NS::UsdGeomXformOp xForm(attribute);
    return xForm.Get<T>(value, time);
}

// Class to catch messages from the USD library
class UsdDiagnosticDelegate : public PXR_NS::TfDiagnosticMgr::Delegate
{
public:
    UsdDiagnosticDelegate() { PXR_NS::TfDiagnosticMgr::GetInstance().AddDelegate(this); }

    ~UsdDiagnosticDelegate() override
    {
        PXR_NS::TfDiagnosticMgr::GetInstance().RemoveDelegate(this);
    }

    void IssueError(const PXR_NS::TfError& err) override
    {
        m_errors.push_back(err.GetCommentary());
    }

    void IssueFatalError(PXR_NS::TfCallContext const& context, std::string const& msg) override
    {
        m_fatalErrors.push_back(msg);
    }

    void IssueStatus(const PXR_NS::TfStatus& status) override
    {
        m_statuses.push_back(status.GetCommentary());
    }

    void IssueWarning(const PXR_NS::TfWarning& warning) override
    {
        m_warnings.push_back(warning.GetCommentary());
    }

    const std::vector<std::string>& GetErrors() const { return m_errors; }

    const std::vector<std::string>& GetFatalErrors() const { return m_fatalErrors; }

    const std::vector<std::string>& GetStatuses() const { return m_statuses; }

    const std::vector<std::string>& GetWarnings() const { return m_warnings; }

private:
    std::vector<std::string> m_errors;
    std::vector<std::string> m_fatalErrors;
    std::vector<std::string> m_statuses;
    std::vector<std::string> m_warnings;
};