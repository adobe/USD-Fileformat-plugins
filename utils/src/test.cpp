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
#include <fileformatutils/test.h>
#include <gtest/gtest.h>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdShade/material.h>

PXR_NAMESPACE_OPEN_SCOPE
TF_DEFINE_PUBLIC_TOKENS(TestTokens, TEST_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

using namespace PXR_NS;

int
archSystem(const std::string& command)
{
#if defined(_WIN32)
    return _wsystem(ArchWindowsUtf8ToUtf16(command).c_str());
#else
    return system(command.c_str());
#endif
}

template<typename T>
struct Primvar
{
    PXR_NS::TfToken interpolation;
    PXR_NS::VtArray<T> values;
    PXR_NS::VtIntArray indices;
};

template<typename T>
static bool
readPrimvar(UsdGeomPrimvarsAPI& api, const TfToken& name, Primvar<T>& primvar)
{
    UsdGeomPrimvar pv = api.GetPrimvar(name);
    if (pv.IsDefined()) {
        pv.Get(&primvar.values);
        pv.GetIndices(&primvar.indices);
        primvar.interpolation = pv.GetInterpolation();
        return true;
    }
    return false;
}

#define ASSERT_ARRAY(...) assertArray(__VA_ARGS__)
template<typename T>
void
assertArray(VtArray<T>& actual, const ArrayData<T>& expected, const std::string& name) // test a subset of the array
{
    ASSERT_EQ(actual.size(), expected.size);
    ASSERT_GE(actual.size(), expected.values.size());
    bool arraysMatch = true;
    size_t i;
    for (i = 0; i < expected.values.size(); i++) {
        if (actual[i] != expected.values[i]) {
            arraysMatch = false;
            break;
        }
    }
    ASSERT_TRUE(arraysMatch) << "Variable: " << name << ". Elements at [" << i << "] differ. Actual = " << actual[i]
                             << ", Expected = " << expected.values[i];
}

bool
floatsEqual(float a, float b, float epsilon = 1e-6)
{
    // Ensure that floating point comparison doesn't result in a false negative
    return std::abs(a - b) < epsilon;
}

bool
doublesEqual(double a, double b, double epsilon = 1e-6)
{
    // Ensure that floating point comparison doesn't result in a false negative
    return std::abs(a - b) < epsilon;
}

#define ASSERT_VEC2F(...) assertVec2f(__VA_ARGS__)
void
assertVec2f(const PXR_NS::GfVec2f& actual,
            const PXR_NS::GfVec2f& expected,
            std::string msg = "") // test a vector of 2 floats
{
    bool valuesMatch = true;
    size_t i;
    for (i = 0; i < 2; ++i) {
        if (!floatsEqual(actual[i], expected[i])) {
            valuesMatch = false;
            break;
        }
    }

    if (msg != "") {
        // Add a space after the message if it's not empty
        msg += ": ";
    }
    ASSERT_TRUE(valuesMatch) << msg << "Elements at [" << i << "] differ. Actual = " << actual[i]
                             << ", Expected = " << expected[i];
}

#define ASSERT_QUATF(...) assertQuatf(__VA_ARGS__)
void
assertQuatf(const PXR_NS::GfQuatf& actual,
            const PXR_NS::GfQuatf& expected,
            std::string msg = "") // test a quaternion of floats
{
    bool valuesMatch = true;
    if (msg != "") {
        // Add a space after the message if it's not empty
        msg += ": ";
    }

    ASSERT_TRUE(floatsEqual(actual.GetReal(), expected.GetReal()))
      << msg << "Real elements differ. Actual = " << actual.GetReal()
      << ", Expected = " << expected.GetReal();
    size_t i;
    for (i = 0; i < 3; ++i) {
        if (!floatsEqual(actual.GetImaginary()[i], expected.GetImaginary()[i])) {
            valuesMatch = false;
            break;
        }
    }
    ASSERT_TRUE(valuesMatch) << msg << "Imaginary elements at [" << i
                             << "] differ. Actual = " << actual.GetImaginary()[i]
                             << ", Expected = " << expected.GetImaginary()[i];
}

#define ASSERT_VEC3F(...) assertVec3f(__VA_ARGS__)
void
assertVec3f(const PXR_NS::GfVec3f& actual,
            const PXR_NS::GfVec3f& expected,
            std::string msg = "") // test a vector of 3 floats
{
    bool valuesMatch = true;
    size_t i;
    for (i = 0; i < 3; ++i) {
        if (!floatsEqual(actual[i], expected[i])) {
            valuesMatch = false;
            break;
        }
    }

    if (msg != "") {
        // Add a space after the message if it's not empty
        msg += ": ";
    }
    ASSERT_TRUE(valuesMatch) << msg << "Elements at [" << i << "] differ. Actual = " << actual[i]
                             << ", Expected = " << expected[i];
}

#define ASSERT_VEC3D(...) assertVec3d(__VA_ARGS__)
void
assertVec3d(const PXR_NS::GfVec3d& actual,
            const PXR_NS::GfVec3d& expected,
            std::string msg = "") // test a vector of 3 doubles
{
    bool valuesMatch = true;
    size_t i;
    for (i = 0; i < 3; ++i) {
        if (!doublesEqual(actual[i], expected[i])) {
            valuesMatch = false;
            break;
        }
    }

    if (msg != "") {
        // Add a space after the message if it's not empty
        msg += ": ";
    }
    ASSERT_TRUE(valuesMatch) << msg << "Elements at [" << i << "] differ. Actual = " << actual[i]
                             << ", Expected = " << expected[i];
}

void
assertPrim(PXR_NS::UsdStageRefPtr stage, const std::string& path)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_TRUE(prim);
}

void
assertNode(UsdStageRefPtr stage, const std::string& path)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_TRUE(prim);
    UsdGeomXform xform(prim);
    ASSERT_TRUE(xform);
}

void
assertMesh(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MeshData& data)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_TRUE(prim);
    UsdGeomMesh geomMesh(prim);
    ASSERT_TRUE(geomMesh);
    UsdGeomPrimvarsAPI primvarsAPI(geomMesh);

    VtIntArray faceVertexCounts;
    VtIntArray faceVertexIndices;
    VtVec3fArray points;
    Primvar<GfVec3f> normals;
    Primvar<GfVec2f> uvs;
    Primvar<PXR_NS::GfVec3f> displayColor;
    Primvar<float> displayOpacity;
    geomMesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts, 0);
    geomMesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices, 0);
    geomMesh.GetPointsAttr().Get(&points, 0);
    UsdAttribute normalsAttr = geomMesh.GetNormalsAttr();
    if (readPrimvar(primvarsAPI, UsdGeomTokens->normals, normals)) {
    } else if (normalsAttr.IsAuthored()) {
        normalsAttr.Get(&normals.values, 0);
        normals.interpolation = geomMesh.GetNormalsInterpolation();
    }
    if (readPrimvar(primvarsAPI, TfToken("st"), uvs)) {
    } else if (readPrimvar(primvarsAPI, TfToken("st0"), uvs)) {
    } else if (readPrimvar(primvarsAPI, TfToken("UVMap"), uvs)) {
    }
    readPrimvar(primvarsAPI, UsdGeomTokens->primvarsDisplayColor, displayColor);
    readPrimvar(primvarsAPI, UsdGeomTokens->primvarsDisplayOpacity, displayOpacity);

    ASSERT_ARRAY(faceVertexCounts, data.faceVertexCounts, "faceVertexCounts");
    ASSERT_ARRAY(faceVertexIndices, data.faceVertexIndices, "faceVertexIndices");
    ASSERT_ARRAY(points, data.points, "points");
    ASSERT_ARRAY(normals.values, data.normals.values, "normals.values");
    ASSERT_ARRAY(normals.indices, data.normals.indices, "normals.indices");
    ASSERT_ARRAY(uvs.values, data.uvs.values, "uvs.values");
    ASSERT_ARRAY(uvs.indices, data.uvs.indices, "uvs.indices");
    ASSERT_ARRAY(displayColor.values, data.displayColor.values, "displayColor.values");
    ASSERT_ARRAY(displayColor.indices, data.displayColor.indices, "displayColor.indices");
    ASSERT_ARRAY(displayOpacity.values, data.displayOpacity.values, "displayOpacity.values");
    ASSERT_ARRAY(displayOpacity.indices, data.displayOpacity.indices, "displayOpacity.indices");
    if (normals.indices.size()) {
        ASSERT_EQ(normals.interpolation, data.normals.interpolation);
    }
    if (uvs.indices.size()) {
        ASSERT_EQ(uvs.interpolation, data.uvs.interpolation);
    }
    if (displayColor.indices.size()) {
        ASSERT_EQ(displayColor.interpolation, data.displayColor.interpolation);
    }
    if (displayOpacity.indices.size()) {
        ASSERT_EQ(displayOpacity.interpolation, data.displayOpacity.interpolation);
    }
}

void
assertAnimation(PXR_NS::UsdStageRefPtr stage, const std::string& path, const AnimationData& data)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_TRUE(prim);

    // We only test the animation samples given in the data. This means that the data can have fewer
    // samples than the actual USD, so that the entire animation doesn't have to be copied into the
    // test

    for (auto pair = data.orient.begin(); pair != data.orient.end(); pair++) {
        float time = pair->first;

        GfQuatf orientValue;
        extractUsdAttribute<GfQuatf>(
          prim, TfToken("xformOp:orient"), &orientValue, UsdTimeCode(time));
        ASSERT_QUATF(orientValue,
                     data.orient.at(time),
                     std::string("xformOp:orient[") + std::to_string(time) + "]");
    }

    for (auto pair = data.scale.begin(); pair != data.scale.end(); pair++) {
        float time = pair->first;

        GfVec3f scaleValue;
        extractUsdAttribute<GfVec3f>(
          prim, TfToken("xformOp:scale"), &scaleValue, UsdTimeCode(time));
        ASSERT_VEC3F(scaleValue,
                     data.scale.at(time),
                     std::string("xformOp:scale[") + std::to_string(time) + "]");
    }

    for (auto pair = data.translate.begin(); pair != data.translate.end(); pair++) {
        float time = pair->first;

        GfVec3d translateValue;
        extractUsdAttribute<GfVec3d>(
          prim, TfToken("xformOp:translate"), &translateValue, UsdTimeCode(time));
        ASSERT_VEC3D(translateValue,
                     data.translate.at(time),
                     std::string("xformOp:translate[") + std::to_string(time) + "]");
    }
}

void
assertCamera(PXR_NS::UsdStageRefPtr stage, const std::string& path, const CameraData& cameraData)
{
    const bool WARN_IF_ATTRIBUTE_NOT_FOUND = false;

    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_TRUE(prim);

    // The transformations for cameras in our USD assets tend to be stored by a parent node. We
    // first verify that the camera's transform is correct by extracting it from the prim's parent

    UsdPrim parent = prim.GetParent();
    ASSERT_TRUE(parent);

    GfVec3d translation;
    GfQuatf rotation;
    GfVec3f scale;

    if (extractUsdAttribute<GfVec3d>(parent, TfToken("xformOp:translate"), &translation)) {
        ASSERT_VEC3D(
          translation, cameraData.translate, path + "'s parent translation does not match\n");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No translation attribute found for %s\n", path.c_str());
    }
    if (extractUsdAttribute<GfQuatf>(parent, TfToken("xformOp:orient"), &rotation)) {
        ASSERT_QUATF(rotation, cameraData.orient, path + "'s parent rotation does not match\n");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No rotation attribute found for %s\n", path.c_str());
    }
    if (extractUsdAttribute<GfVec3f>(parent, TfToken("xformOp:scale"), &scale)) {
        ASSERT_VEC3F(scale, cameraData.scale, path + "'s parent scale does not match\n");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No scale attribute found for %s\n", path.c_str());
    }

    // Next, we check the camera data itself

    UsdGeomCamera camera(prim);
    ASSERT_TRUE(camera) << path << " could not be cast to camera\n";

    GfVec2f clippingRange;
    if (camera.GetClippingRangeAttr().Get(&clippingRange)) {
        ASSERT_VEC2F(clippingRange, cameraData.clippingRange, path + "'s clipping range does not match\n");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No clipping range attribute found for %s\n", path.c_str());
    }

    float focalLength;
    if (camera.GetFocalLengthAttr().Get(&focalLength)) {
        ASSERT_FLOAT_EQ(focalLength, cameraData.focalLength) << path << " focal length does not match\n";
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No focal length attribute found for %s\n", path.c_str());
    }

    float focusDistance;
    if (camera.GetFocusDistanceAttr().Get(&focusDistance)) {
        ASSERT_FLOAT_EQ(focusDistance, cameraData.focusDistance) << path << " focus distance does not match\n";
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No focus distance attribute found for %s\n", path.c_str());
    }

    float fStop;
    if (camera.GetFStopAttr().Get(&fStop)) {
        ASSERT_FLOAT_EQ(fStop, cameraData.fStop) << path << " fStop does not match\n";
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No fStop attribute found for %s\n", path.c_str());
    }

    float horizontalAperture;
    if (camera.GetHorizontalApertureAttr().Get(&horizontalAperture)) {
        ASSERT_FLOAT_EQ(horizontalAperture, cameraData.horizontalAperture)
          << path << " horizontal aperture does not match\n";
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No horizontal aperture attribute found for %s\n", path.c_str());
    }

    TfToken projection;
    if (camera.GetProjectionAttr().Get(&projection)) {
        ASSERT_EQ(projection, TfToken(cameraData.projection)) << path << " projection does not match\n";
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No projection attribute found for %s\n", path.c_str());
    }

    float verticalAperture;
    if (camera.GetVerticalApertureAttr().Get(&verticalAperture)) {
        ASSERT_FLOAT_EQ(verticalAperture, cameraData.verticalAperture)
          << path << " vertical aperture does not match\n";
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No vertical aperture attribute found for %s\n", path.c_str());
    }
}

void
assertLight(PXR_NS::UsdStageRefPtr stage, const std::string& path, const LightData& lightData)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_TRUE(prim);

    // The transformations for lights in our USD assets tend to be stored by a parent node. We
    // first verify that the light's transform is correct by extracting it from the prim's parent

    UsdPrim parent = prim.GetParent();
    ASSERT_TRUE(parent);

    GfVec3d translation;
    GfQuatf rotation;
    GfVec3f scale;

    // The transformations for lights in our USD assets tend to be stored by a parent node
    if (lightData.translation) {
        ASSERT_TRUE(
          extractUsdAttribute<GfVec3d>(parent, TfToken("xformOp:translate"), &translation))
          << "Expected translation attribute not found for " << path << "\n";
        ASSERT_VEC3D(translation,
                     lightData.translation.value(),
                     path + "'s parent translation does not match\n");
    }
    if (lightData.rotation) {
        ASSERT_TRUE(extractUsdAttribute<GfQuatf>(parent, TfToken("xformOp:orient"), &rotation))
          << "Expected orient attribute not found for " << path << "\n";
        ASSERT_QUATF(
          rotation, lightData.rotation.value(), path + "'s parent rotation does not match\n");
    }
    if (lightData.scale) {
        ASSERT_TRUE(extractUsdAttribute<GfVec3f>(parent, TfToken("xformOp:scale"), &scale))
          << "Expected scale attribute not found for " << path << "\n";
        ASSERT_VEC3F(scale, lightData.scale.value(), path + "'s parent scale does not match\n");
    }

    // Next, we check the light data itself

    if (prim.IsA<UsdLuxSphereLight>()) {
        UsdLuxSphereLight sphereLight(prim);
        ASSERT_TRUE(sphereLight) << path << " could not be cast to sphere light\n";

        if (lightData.color) {
            PXR_NS::GfVec3f color;
            ASSERT_TRUE(sphereLight.GetColorAttr().Get(&color))
              << path << " is missing expected color attribute\n";
            ASSERT_VEC3F(color, lightData.color.value(), path + " color does not match\n");
        }

        if (lightData.intensity) {
            float intensity;
            ASSERT_TRUE(sphereLight.GetIntensityAttr().Get(&intensity))
              << path << " is missing expected intensity attribute\n";
            ASSERT_FLOAT_EQ(intensity, lightData.intensity.value())
              << path << " intensity does not match\n";
        }

        if (lightData.radius) {
            float radius;
            ASSERT_TRUE(sphereLight.GetRadiusAttr().Get(&radius))
              << path << " is missing expected radius attribute\n";
            ASSERT_FLOAT_EQ(radius, lightData.radius.value()) << path << " radius does not match\n";
        }

    } else if (prim.IsA<UsdLuxDistantLight>()) {
        UsdLuxDistantLight distantLight(prim);
        ASSERT_TRUE(distantLight) << path << " could not be cast to distant light\n";

        if (lightData.color) {
            PXR_NS::GfVec3f color;
            ASSERT_TRUE(distantLight.GetColorAttr().Get(&color))
              << path << " is missing expected color attribute\n";
            ASSERT_VEC3F(color, lightData.color.value(), path + " color does not match\n");
        }

        if (lightData.intensity) {
            float intensity;
            ASSERT_TRUE(distantLight.GetIntensityAttr().Get(&intensity))
              << path << " is missing expected intensity attribute\n";
            ASSERT_FLOAT_EQ(intensity, lightData.intensity.value())
              << path << " intensity does not match\n";
        }

        // Distant lights don't have a radius

    } else if (prim.IsA<UsdLuxDiskLight>()) {
        UsdLuxDiskLight diskLight(prim);
        ASSERT_TRUE(diskLight) << path << " could not be cast to disk light\n";

        if (lightData.color) {
            PXR_NS::GfVec3f color;
            ASSERT_TRUE(diskLight.GetColorAttr().Get(&color))
              << path << " is missing expected color attribute\n";
            ASSERT_VEC3F(color, lightData.color.value(), path + " color does not match\n");
        }

        if (lightData.intensity) {
            float intensity;
            ASSERT_TRUE(diskLight.GetIntensityAttr().Get(&intensity))
              << path << " is missing expected intensity attribute\n";
            ASSERT_FLOAT_EQ(intensity, lightData.intensity.value())
              << path << " intensity does not match\n";
        }

        if (lightData.radius) {
            float radius;
            ASSERT_TRUE(diskLight.GetRadiusAttr().Get(&radius))
              << path << " is missing expected radius attribute\n";
            ASSERT_FLOAT_EQ(radius, lightData.radius.value()) << path << " radius does not match\n";
        }

    } else if (prim.IsA<UsdLuxRectLight>()) {
        ASSERT_TRUE(false) << "Rectangle lights are not supported yet on import\n";

        /*
        Uncomment this once we support import of rectangle lights

        UsdLuxRectLight rectLight(prim);
        ASSERT_TRUE(rectLight);

        ASSERT_VEC3F(rectLight.color, lightData.color);
        ASSERT_FLOAT_EQ(rectLight.intensity, lightData.intensity);

        // Rectangle specific attributes
        ASSERT_FLOAT_EQ(rectLight.length[0], lightData.length[0]);
        ASSERT_FLOAT_EQ(rectLight.length[1], lightData.length[1]);
        */
    } else if (prim.IsA<UsdLuxDomeLight>()) {
        ASSERT_TRUE(false) << "Dome lights are not supported yet on import\n";

        /*
        Uncomment this once we support import of dome lights

        UsdLuxDomeLight domeLight(prim);
        ASSERT_TRUE(domeLight);

        ASSERT_VEC3F(domeLight.color, lightData.color);
        ASSERT_FLOAT_EQ(domeLight.intensity, lightData.intensity);

        // Add texture test once we support this on import
        */
    } else {
        ASSERT_TRUE(false) << "Expected a supported light, but encountered a prim of type \""
                           << prim.GetTypeName().GetString() << "\" at \"" << path << "\"\n";
    }

    // Spotlights use shaping APIs
    if (prim.HasAPI<UsdLuxShapingAPI>()) {
        UsdLuxShapingAPI shapingAPI(prim);

        // Shaping API specific attributes

        if (lightData.coneAngle) {
            float coneAngle;
            ASSERT_TRUE(shapingAPI.GetShapingConeAngleAttr().Get(&coneAngle))
              << path << " is missing expected angle attribute\n";
            ASSERT_FLOAT_EQ(coneAngle, lightData.coneAngle.value())
              << path << " cone angle does not match\n";
        }

        if (lightData.coneFalloff) {
            float coneFalloff;
            ASSERT_TRUE(shapingAPI.GetShapingConeSoftnessAttr().Get(&coneFalloff))
              << path << " is missing expected softness attribute\n";
            ASSERT_FLOAT_EQ(coneFalloff, lightData.coneFalloff.value())
              << path << " cone falloff does not match\n";
        }
    }
}

void
assertDisplayName(PXR_NS::UsdStageRefPtr stage,
                  const std::string& primPath,
                  const std::string& displayName)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(primPath));
    ASSERT_TRUE(prim) << primPath << " not found when verifying prim had proper display name\n";
    ASSERT_EQ(prim.GetDisplayName(), displayName)
      << primPath << " has incorrect display name; expected \"" << displayName << "\" but got \""
      << prim.GetDisplayName() << "\"\n ";
}

void
assertVisibility(PXR_NS::UsdStageRefPtr stage,
                 const std::string& path,
                 bool expectedVisibilityAttr,
                 bool expectedActualVisibility)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));

    UsdGeomImageable imageable(prim);
    ASSERT_TRUE(imageable) << "Test setup error: " << path << " is not an imageable prim";

    // Visibility attribute should always be present, even if it's not written explicitly
    ASSERT_TRUE(imageable.GetVisibilityAttr().HasValue())
      << "Unexpected error: " << path << " missing visibility attribute";

    // Check visibility attribute
    TfToken visibility;
    imageable.GetVisibilityAttr().Get<TfToken>(&visibility);
    ASSERT_EQ(expectedVisibilityAttr, visibility == UsdGeomTokens->inherited)
      << path << " has visibility attribute "
      << (expectedVisibilityAttr ? "inherited" : "invisible") << " that is expected to be "
      << visibility.GetString();

    // Check actual visibility
    visibility = imageable.ComputeVisibility();
    ASSERT_EQ(expectedActualVisibility, visibility == UsdGeomTokens->inherited)
      << path << " is computed as " << visibility.GetString() << " but is expected to be "
      << (expectedVisibilityAttr ? "visible" : "invisible");
}

void
assertPoints(PXR_NS::UsdStageRefPtr stage, const std::string& path, const PointsData& data)
{
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_TRUE(prim);
    UsdGeomPoints geomPoints(prim);
    ASSERT_TRUE(geomPoints);

    VtVec3fArray points;
    geomPoints.GetPointsAttr().Get(&points, 0);
    ASSERT_EQ(points.size(), data.pointsCount);
}

#define ASSERT_INPUT(...) assertInput(__VA_ARGS__)
#define ASSERT_INPUT_FIELD(...) assertInputField(__VA_ARGS__)
#define ASSERT_INPUT_PATH(...) assertInputPath(__VA_ARGS__)

template<typename T>
void
assertInputField(const UsdShadeShader& shader, const std::string& name, const T& value)
{
    auto attr = shader.GetInput(TfToken(name));
    if (attr) {
        auto valueAttrs = attr.GetValueProducingAttributes();
        if (valueAttrs.size()) {
            T actual;
            valueAttrs.front().Get(&actual);
            ASSERT_EQ(actual, value);
            return;
        }
    }
    // TODO check if attr missing
}

// Cannot reuse assertInputField for this since SdfAssetPath equality fails for ::resolvedPath,
// so here we match only the ::assetPath.
void
assertInputPath(const UsdShadeShader& shader, const std::string& name, const std::string& value)
{
    auto attr = shader.GetInput(TfToken(name));
    if (attr) {
        auto valueAttrs = attr.GetValueProducingAttributes();
        if (valueAttrs.size()) {
            SdfAssetPath actualAssetPath;
            valueAttrs.front().Get(&actualAssetPath);
            const std::string actual = TfNormPath(actualAssetPath.GetAssetPath());
            ASSERT_EQ(actual, value);
            return;
        }
    }
    // TODO check if attr missing
}

void
assertMaterial(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MaterialData& data)
{
    const std::string currentDir = TfAbsPath(".");
    const SdfPath materialPath = SdfPath(path);
    UsdPrim prim = stage->GetPrimAtPath(materialPath);
    ASSERT_TRUE(prim);
    UsdShadeMaterial material(prim);
    ASSERT_TRUE(material);

    SdfPathVector connections;
    UsdShadeShader shader;
    UsdAttribute surface = material.GetSurfaceAttr();
    surface.GetConnections(&connections);
    ASSERT_TRUE(connections.size() > 0);

    const SdfPath shaderPath = connections[0].GetPrimPath();
    shader = UsdShadeShader(stage->GetPrimAtPath(shaderPath));
    TfToken shaderId;
    shader.GetShaderId(&shaderId);
    ASSERT_TRUE(shaderId == TfToken("UsdPreviewSurface"));

    auto assertInput = [&](const TfToken& name, const InputData& data) {
        UsdShadeInput shadeInput = shader.GetInput(name);
        if (!shadeInput)
            return;
        if (shadeInput.HasConnectedSource()) {
            UsdShadeInput::SourceInfoVector sources = shadeInput.GetConnectedSources();
            for (UsdShadeConnectionSourceInfo source : sources) {
                const SdfPath sourcePath = source.source.GetPath();
                const UsdShadeShader textureShader =
                  UsdShadeShader(stage->GetPrimAtPath(sourcePath));
                if (sourcePath == materialPath) {
                    ASSERT_INPUT_FIELD(textureShader, name, data.value);
                } else {
                    const SdfPath textureShaderPath = textureShader.GetPath();
                    TfToken uvTextureId;
                    textureShader.GetShaderId(&uvTextureId);
                    ASSERT_EQ(uvTextureId.GetString(), std::string("UsdUVTexture"));

                    const std::string assetPath = TfNormPath(currentDir + "/" + data.file);
                    ASSERT_INPUT_PATH(textureShader, "file", assetPath);
                    // TODO? ASSERT_IMAGE(ctx, assetPath, input.image);
                    ASSERT_INPUT_FIELD(textureShader, "wrapS", data.wrapS);
                    ASSERT_INPUT_FIELD(textureShader, "wrapT", data.wrapT);
                    ASSERT_INPUT_FIELD(textureShader, "scale", data.scale);
                    ASSERT_INPUT_FIELD(textureShader, "bias", data.bias);
                    ASSERT_INPUT_FIELD(textureShader, "fallback", data.value);
                    ASSERT_EQ(data.channel, source.sourceName);

                    UsdShadeInput stInput = textureShader.GetInput(TestTokens->st);
                    if (!stInput)
                        return;
                    if (stInput.HasConnectedSource()) {
                        UsdShadeInput::SourceInfoVector sources = stInput.GetConnectedSources();
                        for (UsdShadeConnectionSourceInfo source : sources) {
                            const SdfPath sourcePath = source.source.GetPath();
                            UsdShadeShader stShader =
                              UsdShadeShader(stage->GetPrimAtPath(sourcePath));
                            TfToken shaderId;
                            stShader.GetShaderId(&shaderId);
                            if (!data.transformRotation.IsEmpty() ||
                                !data.transformScale.IsEmpty() ||
                                !data.transformTranslation.IsEmpty()) {
                                ASSERT_TRUE(shaderId == TestTokens->UsdTransform2d);
                                ASSERT_INPUT_FIELD(stShader, "rotation", data.transformRotation);
                                ASSERT_INPUT_FIELD(stShader, "scale", data.transformScale);
                                ASSERT_INPUT_FIELD(
                                  stShader, "translation", data.transformTranslation);
                            } else {
                                std::string shaderName = stShader.GetPrim().GetName().GetString();
                                if (shaderName == "texCoordReader") {
                                    ASSERT_TRUE(shaderId == TestTokens->UsdPrimvarReader_float2);
                                } else {
                                    ASSERT_TRUE(shaderId == TestTokens->UsdTransform2d);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            VtValue actualValue;
            shadeInput.Get(&actualValue);
            ASSERT_TRUE(actualValue == data.value);
        }
    };
    ASSERT_INPUT(TestTokens->useSpecularWorkflow, data.useSpecularWorkflow);
    ASSERT_INPUT(TestTokens->diffuseColor, data.diffuseColor);
    ASSERT_INPUT(TestTokens->emissiveColor, data.emissiveColor);
    ASSERT_INPUT(TestTokens->specularColor, data.specularColor);
    ASSERT_INPUT(TestTokens->normal, data.normal);
    ASSERT_INPUT(TestTokens->metallic, data.metallic);
    ASSERT_INPUT(TestTokens->roughness, data.roughness);
    ASSERT_INPUT(TestTokens->clearcoat, data.clearcoat);
    ASSERT_INPUT(TestTokens->clearcoatRoughness, data.clearcoatRoughness);
    ASSERT_INPUT(TestTokens->opacity, data.opacity);
    ASSERT_INPUT(TestTokens->opacityThreshold, data.opacityThreshold);
    ASSERT_INPUT(TestTokens->displacement, data.displacement);
    ASSERT_INPUT(TestTokens->occlusion, data.occlusion);
    ASSERT_INPUT(TestTokens->ior, data.ior);
}

void
assertRender(const std::string& filename, const std::string& imageFilename)
{
    const std::string imageParentPath = TfGetPathName(imageFilename);
    TfMakeDirs(imageParentPath, -1, true);
    const std::string command = "usdrecord \"" + filename + "\" \"" + imageFilename + "\"";
    int result = archSystem(command);
    ASSERT_EQ(result, 0);
}