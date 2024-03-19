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
#include "test.h"
#include <gtest/gtest.h>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
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
assertArray(VtArray<T>& actual, const ArrayData<T>& expected) // test a subset of the array
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
    ASSERT_TRUE(arraysMatch) << "Elements at [" << i << "] differ. Actual = " << actual[i]
                             << ", Expected = " << expected.values[i];
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

    ASSERT_ARRAY(faceVertexCounts, data.faceVertexCounts);
    ASSERT_ARRAY(faceVertexIndices, data.faceVertexIndices);
    ASSERT_ARRAY(points, data.points);
    ASSERT_ARRAY(normals.values, data.normals.values);
    ASSERT_ARRAY(normals.indices, data.normals.indices);
    ASSERT_ARRAY(uvs.values, data.uvs.values);
    ASSERT_ARRAY(uvs.indices, data.uvs.indices);
    ASSERT_ARRAY(displayColor.values, data.displayColor.values);
    ASSERT_ARRAY(displayColor.indices, data.displayColor.indices);
    ASSERT_ARRAY(displayOpacity.values, data.displayOpacity.values);
    ASSERT_ARRAY(displayOpacity.indices, data.displayOpacity.indices);
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
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
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
                const std::string assetPath = TfNormPath(currentDir + "/" + data.file);
                ASSERT_INPUT_PATH(textureShader, "file", assetPath);
                // TODO? ASSERT_IMAGE(ctx, assetPath, input.image);
                ASSERT_INPUT_FIELD(textureShader, "wrapS", data.wrapS);
                ASSERT_INPUT_FIELD(textureShader, "wrapT", data.wrapT);
                ASSERT_INPUT_FIELD(textureShader, "scale", data.scale);
                ASSERT_INPUT_FIELD(textureShader, "bias", data.bias);
                ASSERT_INPUT_FIELD(textureShader, "fallback", data.value);
                ASSERT_TRUE(data.channel == source.sourceName);

                UsdShadeInput stInput = textureShader.GetInput(TestTokens->st);
                if (!stInput)
                    return;
                if (stInput.HasConnectedSource()) {
                    UsdShadeInput::SourceInfoVector sources = stInput.GetConnectedSources();
                    for (UsdShadeConnectionSourceInfo source : sources) {
                        const SdfPath sourcePath = source.source.GetPath();
                        UsdShadeShader stShader = UsdShadeShader(stage->GetPrimAtPath(sourcePath));
                        TfToken shaderId;
                        stShader.GetShaderId(&shaderId);
                        if (!data.transformRotation.IsEmpty() || !data.transformScale.IsEmpty() ||
                            !data.transformTranslation.IsEmpty()) {
                            ASSERT_TRUE(shaderId == TestTokens->UsdTransform2d);
                            ASSERT_INPUT_FIELD(stShader, "rotation", data.transformRotation);
                            ASSERT_INPUT_FIELD(stShader, "scale", data.transformScale);
                            ASSERT_INPUT_FIELD(stShader, "translation", data.transformTranslation);
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