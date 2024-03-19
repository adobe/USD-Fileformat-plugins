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
#include "layerWriteSdfData.h"

#include "common.h"
#include "debugCodes.h"
#include "geometry.h"
#include "layerWriteMaterial.h"
#include "layerWriteMaterialX.h"
#include "sdfMaterialUtils.h"
#include "sdfUtils.h"
#include "usdData.h"

#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdSkel/tokens.h>
#include <pxr/usd/usdVol/tokens.h>

#include <fstream>

using namespace PXR_NS;

namespace adobe::usd {

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    // Xform ops
    ((xformOpTranslate, "xformOp:translate"))
    ((xformOpOrient, "xformOp:orient"))
    ((xformOpScale, "xformOp:scale"))
    ((xformOpTransform, "xformOp:transform"))
    // Skeletal animation
    ((anim, "anim"))
    // NGP
    ((ngp, "ngp"))
    ((vol, "vol"))
);
// clang-format on

bool
_writeMetadata(SdfAbstractData* sdfData, const UsdData& usdData, const SdfPath& rootNodePath)
{
    setLayerMetadata(sdfData, SdfFieldKeys->DefaultPrim, VtValue(rootNodePath.GetNameToken()));
    setLayerMetadata(sdfData, SdfFieldKeys->CustomLayerData, VtValue(usdData.metadata));
    if (!usdData.upAxis.IsEmpty()) {
        setLayerMetadata(sdfData, UsdGeomTokens->upAxis, VtValue(usdData.upAxis));
    }
    if (usdData.metersPerUnit != 0.0) {
        setLayerMetadata(sdfData, UsdGeomTokens->metersPerUnit, VtValue(usdData.metersPerUnit));
    }
    if (usdData.hasAnimations) {
        setLayerMetadata(sdfData, SdfFieldKeys->StartTimeCode, VtValue((double)usdData.minTime));
        setLayerMetadata(sdfData, SdfFieldKeys->EndTimeCode, VtValue((double)usdData.maxTime));
        setLayerMetadata(
          sdfData, SdfFieldKeys->TimeCodesPerSecond, VtValue(usdData.timeCodesPerSecond));
    }

    // Debug print below
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "layer::write metadata {\n");
    for (const auto& metadatum : usdData.metadata) {
        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "    %s:", metadatum.first.c_str());
        const VtValue& value = metadatum.second;
        if (value.IsHolding<std::string>()) {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL, " \"%s\"\n", value.Get<std::string>().c_str());
        } else if (value.IsHolding<bool>()) {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL, " %s\n", value.Get<bool>() ? "true" : "false");
        } else if (value.IsHolding<int>()) {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL, " %s\n", std::to_string(value.Get<int>()).c_str());
        } else if (value.IsHolding<float>()) {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL, " %s\n", std::to_string(value.Get<float>()).c_str());
        } else if (value.IsHolding<VtArray<std::string>>()) {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL, "\n");
            const VtArray<std::string>& values = value.Get<VtArray<std::string>>();
            for (unsigned int i = 0; i < values.size(); i++) {
                TF_DEBUG_MSG(FILE_FORMAT_UTIL, "        %s\n", values[i].c_str());
            }
        }
    }
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "}\n");

    return true;
}

void
_writeCamera(SdfAbstractData* sdfData, const SdfPath& parentPath, const Camera& camera)
{
    SdfPath primPath =
      createPrimSpec(sdfData, parentPath, TfToken(camera.name), UsdGeomTokens->Camera);

    auto createAttr = [&](const TfToken& name, const SdfValueTypeName& type, const auto& value) {
        SdfPath p = createAttributeSpec(sdfData, primPath, name, type);
        setAttributeDefaultValue(sdfData, p, value);
    };

    const TfToken& proj = camera.projection == GfCamera::Perspective ? UsdGeomTokens->perspective
                                                                     : UsdGeomTokens->orthographic;
    createAttr(UsdGeomTokens->projection, SdfValueTypeNames->Token, proj);
    createAttr(
      UsdGeomTokens->horizontalAperture, SdfValueTypeNames->Float, camera.horizontalAperture);
    createAttr(UsdGeomTokens->verticalAperture, SdfValueTypeNames->Float, camera.verticalAperture);
    createAttr(UsdGeomTokens->focalLength, SdfValueTypeNames->Float, camera.f);
    GfVec2f clippingRange(camera.nearZ, camera.farZ);
    createAttr(UsdGeomTokens->clippingRange, SdfValueTypeNames->Float2, clippingRange);
}

void
_writeNgp(SdfAbstractData* sdfData, const SdfPath& parentPath, const NgpData& ngp)
{
    SdfPath volPrimPath = createPrimSpec(sdfData, parentPath, _tokens->vol, UsdVolTokens->Volume);
    SdfPath ngpPrimPath = createPrimSpec(sdfData, volPrimPath, _tokens->ngp, AdobeNgpTokens->Ngp);

    auto createAttr = [&](const SdfPath& primPath,
                          const TfToken& name,
                          const SdfValueTypeName& type,
                          const auto& value,
                          bool uniform = false) {
        SdfPath p = createAttributeSpec(sdfData,
                                        primPath,
                                        name,
                                        type,
                                        uniform ? PXR_NS::SdfVariabilityUniform
                                                : PXR_NS::SdfVariabilityVarying);
        setAttributeDefaultValue(sdfData, p, value);
    };

    createAttr(ngpPrimPath,
               AdobeNgpTokens->densityMlpLayer0Weight,
               SdfValueTypeNames->FloatArray,
               ngp.densityMlpLayer0Weight);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->densityMlpLayer0Bias,
               SdfValueTypeNames->FloatArray,
               ngp.densityMlpLayer0Bias);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->densityMlpLayer1Weight,
               SdfValueTypeNames->FloatArray,
               ngp.densityMlpLayer1Weight);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->densityMlpLayer1Bias,
               SdfValueTypeNames->FloatArray,
               ngp.densityMlpLayer1Bias);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->colorMlpLayer0Weight,
               SdfValueTypeNames->FloatArray,
               ngp.colorMlpLayer0Weight);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->colorMlpLayer0Bias,
               SdfValueTypeNames->FloatArray,
               ngp.colorMlpLayer0Bias);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->colorMlpLayer1Weight,
               SdfValueTypeNames->FloatArray,
               ngp.colorMlpLayer1Weight);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->colorMlpLayer1Bias,
               SdfValueTypeNames->FloatArray,
               ngp.colorMlpLayer1Bias);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->colorMlpLayer2Weight,
               SdfValueTypeNames->FloatArray,
               ngp.colorMlpLayer2Weight);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->colorMlpLayer2Bias,
               SdfValueTypeNames->FloatArray,
               ngp.colorMlpLayer2Bias);
    createAttr(
      ngpPrimPath, AdobeNgpTokens->densityGrid, SdfValueTypeNames->FloatArray, ngp.densityGrid);
    createAttr(
      ngpPrimPath, AdobeNgpTokens->distanceGrid, SdfValueTypeNames->FloatArray, ngp.distanceGrid);
    createAttr(ngpPrimPath, AdobeNgpTokens->hashGrid, SdfValueTypeNames->FloatArray, ngp.hashGrid);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->densityThreshold,
               SdfValueTypeNames->Float,
               ngp.densityThreshold);
    createAttr(ngpPrimPath,
               AdobeNgpTokens->hashGridResolution,
               SdfValueTypeNames->UIntArray,
               ngp.hashGridResolution);

    if (ngp.hasTransform) {
        createAttr(
          volPrimPath, _tokens->xformOpTransform, SdfValueTypeNames->Matrix4d, ngp.transform);
        VtArray<TfToken> xformOpOrder = { _tokens->xformOpTransform };
        createAttr(volPrimPath,
                   UsdGeomTokens->xformOpOrder,
                   SdfValueTypeNames->TokenArray,
                   xformOpOrder,
                   true);
    }

    // Set the extent of volume.
    PXR_NS::VtVec3fArray extent = { PXR_NS::GfVec3f(-1.0f, -1.0f, -1.0f),
                                    PXR_NS::GfVec3f(1.0f, 1.0f, 1.0f) };
    createAttr(volPrimPath, UsdGeomTokens->extent, SdfValueTypeNames->Float3Array, extent);

    SdfPath ngpRelPath = createRelationshipSpec(sdfData, volPrimPath, AdobeNgpTokens->fieldNgp);
    appendRelationshipTarget(sdfData, ngpRelPath, ngpPrimPath);
}

template<typename T, typename CT = T>
void
_writeTimeSamples(SdfAbstractData* sdfData,
                  const SdfPath& propertyPath,
                  const TimeValues<T>& timeValues)
{
    if (!timeValues.times.empty()) {
        SdfTimeSampleMap timeSamples;
        for (size_t i = 0; i < timeValues.times.size(); ++i) {
            timeSamples.emplace(timeValues.times[i], VtValue(CT(timeValues.values[i])));
        }
        setAttributeTimeSampledValues(sdfData, propertyPath, timeSamples);
    }
}

void
_writeXformAttributes(SdfAbstractData* sdfData, const SdfPath& primPath, const Node& node)
{
    VtArray<TfToken> xformOpOrder;
    xformOpOrder.reserve(3);
    bool hasTranslation = node.translation != GfVec3d(0);
    if (hasTranslation || node.translations.times.size()) {
        SdfPath p = createAttributeSpec(
          sdfData, primPath, _tokens->xformOpTranslate, SdfValueTypeNames->Double3);
        xformOpOrder.push_back(_tokens->xformOpTranslate);

        if (hasTranslation) {
            setAttributeDefaultValue(sdfData, p, node.translation);
        }
        // XXX currently the translations is stored as GfVec3f, but needs to be authored as GfVec3d
        _writeTimeSamples<GfVec3f, GfVec3d>(sdfData, p, node.translations);
    }
    bool hasRotation = node.rotation != GfQuatf(0);
    if (hasRotation || node.rotations.times.size()) {
        SdfPath p =
          createAttributeSpec(sdfData, primPath, _tokens->xformOpOrient, SdfValueTypeNames->Quatf);
        xformOpOrder.push_back(_tokens->xformOpOrient);

        if (hasRotation) {
            setAttributeDefaultValue(sdfData, p, node.rotation);
        }
        _writeTimeSamples(sdfData, p, node.rotations);
    }
    bool hasScale = node.scale != GfVec3f(1);
    if (hasScale || node.scales.times.size()) {
        SdfPath p =
          createAttributeSpec(sdfData, primPath, _tokens->xformOpScale, SdfValueTypeNames->Float3);
        xformOpOrder.push_back(_tokens->xformOpScale);

        if (hasScale) {
            setAttributeDefaultValue(sdfData, p, node.scale);
        }
        _writeTimeSamples(sdfData, p, node.scales);
    }
    if (node.hasTransform && node.transform != GfMatrix4d().SetIdentity()) {
        SdfPath p = createAttributeSpec(
          sdfData, primPath, _tokens->xformOpTransform, SdfValueTypeNames->Matrix4d);
        setAttributeDefaultValue(sdfData, p, node.transform);
        xformOpOrder.push_back(_tokens->xformOpTransform);
    }

    if (!xformOpOrder.empty()) {
        SdfPath p = createAttributeSpec(sdfData,
                                        primPath,
                                        UsdGeomTokens->xformOpOrder,
                                        SdfValueTypeNames->TokenArray,
                                        SdfVariabilityUniform);
        setAttributeDefaultValue(sdfData, p, xformOpOrder);
    }
}

void
_bindMaterial(SdfAbstractData* sdfData, const SdfPath& primPath, const SdfPath& materialPath)
{
    prependApiSchema(sdfData, primPath, UsdShadeTokens->MaterialBindingAPI);
    SdfPath bindingRelPath =
      createRelationshipSpec(sdfData, primPath, UsdShadeTokens->materialBinding);
    appendRelationshipTarget(sdfData, bindingRelPath, materialPath);
}

SdfPath
_createGeomSubset(SdfAbstractData* sdfData,
                  const SdfPath& primPath,
                  const TfToken& subsetName,
                  const Subset& subset)
{
    SdfPath p;
    SdfPath subsetPath = createPrimSpec(sdfData, primPath, subsetName, UsdGeomTokens->GeomSubset);
    // Element type = face
    p = createAttributeSpec(sdfData,
                            subsetPath,
                            UsdGeomTokens->elementType,
                            SdfValueTypeNames->Token,
                            SdfVariabilityUniform);
    setAttributeDefaultValue(sdfData, p, UsdGeomTokens->face);
    // Face indices
    p =
      createAttributeSpec(sdfData, subsetPath, UsdGeomTokens->indices, SdfValueTypeNames->IntArray);
    setAttributeDefaultValue(sdfData, p, subset.faces);
    // family type = materialBind
    p = createAttributeSpec(sdfData,
                            subsetPath,
                            UsdGeomTokens->familyName,
                            SdfValueTypeNames->Token,
                            SdfVariabilityUniform);
    setAttributeDefaultValue(sdfData, p, UsdShadeTokens->materialBind);
    return subsetPath;
}

template<typename T>
SdfPath
_writePrimvar(SdfAbstractData* sdfData,
              const SdfPath& primPath,
              const std::string& primvarName,
              const PXR_NS::SdfValueTypeName& typeName,
              const Primvar<T>& primvar)
{
    if (primvar.values.empty()) {
        return SdfPath();
    }

    SdfPath primvarAttrPath =
      createAttributeSpec(sdfData, primPath, TfToken("primvars:" + primvarName), typeName);
    setAttributeMetadata(
      sdfData, primvarAttrPath, UsdGeomTokens->interpolation, VtValue(primvar.interpolation));

    setAttributeDefaultValue(sdfData, primvarAttrPath, primvar.values);

    if (!primvar.indices.empty()) {
        // The indices are stored in a sibling attribute
        TfToken indicesAttrName("primvars:" + primvarName + ":indices");
        SdfPath primvarIndicesAttrPath =
          createAttributeSpec(sdfData, primPath, indicesAttrName, SdfValueTypeNames->IntArray);
        setAttributeDefaultValue(sdfData, primvarIndicesAttrPath, primvar.indices);
    }

    return primvarAttrPath;
}

void
_writePrimvars(SdfAbstractData* sdfData,
               const SdfPath& primPath,
               const Mesh& mesh,
               bool onlyColors = false)
{
    if (!onlyColors) {
        _writePrimvar(sdfData, primPath, "st", SdfValueTypeNames->TexCoord2fArray, mesh.uvs);
        _writePrimvar(sdfData, primPath, "normals", SdfValueTypeNames->Normal3fArray, mesh.normals);
        _writePrimvar(sdfData, primPath, "tangents", SdfValueTypeNames->Float4Array, mesh.tangents);
    }

    auto indexedName = [](const std::string& baseName, int index) -> std::string {
        return index == 0 ? baseName : baseName + std::to_string(index);
    };

    for (size_t i = 0; i < mesh.colors.size(); i++) {
        const Primvar<GfVec3f>& color = mesh.colors[i];
        std::string name = indexedName("displayColor", i);
        _writePrimvar(sdfData, primPath, name, SdfValueTypeNames->Color3fArray, color);
    }
    for (size_t i = 0; i < mesh.opacities.size(); i++) {
        const Primvar<float>& opacity = mesh.opacities[i];
        std::string name = indexedName("displayOpacity", i);
        _writePrimvar(sdfData, primPath, name, SdfValueTypeNames->FloatArray, opacity);
    }
}

SdfPath
_writePoints(SdfAbstractData* sdfData, const SdfPath& parentPath, const Mesh& mesh)
{
    SdfPath primPath =
      createPrimSpec(sdfData, parentPath, TfToken(mesh.name), UsdGeomTokens->Points);

    auto createAttr = [&](const TfToken& name, const SdfValueTypeName& type, const auto& value) {
        SdfPath p = createAttributeSpec(sdfData, primPath, name, type);
        setAttributeDefaultValue(sdfData, p, value);
        return p;
    };

    createAttr(UsdGeomTokens->points, SdfValueTypeNames->Point3fArray, mesh.points);
    // TODO: why is constant interpolation not working for point widths?
    VtFloatArray widths(mesh.points.size(), mesh.pointWidth);
    SdfPath widthsAttrPath =
      createAttr(UsdGeomTokens->widths, SdfValueTypeNames->FloatArray, widths);
    setAttributeMetadata(
      sdfData, widthsAttrPath, UsdGeomTokens->interpolation, VtValue(UsdGeomTokens->vertex));

    // Primvars. Note, for points we currently do not emit texcoords, normals and tangents
    _writePrimvars(sdfData, primPath, mesh, /*onlyColors*/ true);

    return primPath;
}

SdfPath
_writeMesh(SdfAbstractData* sdfData,
           const SdfPath& parentPath,
           const SdfPathVector& materialMap,
           const Mesh& mesh)
{
    SdfPath primPath = createPrimSpec(sdfData, parentPath, TfToken(mesh.name), UsdGeomTokens->Mesh);
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "write mesh: path=%s\n", primPath.GetString().c_str());

    auto createAttr = [&](const TfToken& name,
                          const SdfValueTypeName& type,
                          const auto& value,
                          bool uniform = false) {
        SdfVariability variability =
          uniform ? PXR_NS::SdfVariabilityUniform : PXR_NS::SdfVariabilityVarying;
        SdfPath p = createAttributeSpec(sdfData, primPath, name, type, variability);
        setAttributeDefaultValue(sdfData, p, value);
    };

    // UsdMesh basics
    createAttr(UsdGeomTokens->points, SdfValueTypeNames->Point3fArray, mesh.points);
    createAttr(UsdGeomTokens->faceVertexCounts, SdfValueTypeNames->IntArray, mesh.faces);
    createAttr(UsdGeomTokens->faceVertexIndices, SdfValueTypeNames->IntArray, mesh.indices);
    // Subdivision rules
    createAttr(
      UsdGeomTokens->subdivisionScheme, SdfValueTypeNames->Token, UsdGeomTokens->none, true);
    createAttr(
      UsdGeomTokens->triangleSubdivisionRule, SdfValueTypeNames->Token, UsdGeomTokens->none);

    // Double sided
    createAttr(UsdGeomTokens->doubleSided, SdfValueTypeNames->Bool, mesh.doubleSided, true);

    // Primvars
    _writePrimvars(sdfData, primPath, mesh);

    // UsdSkelBindingAPI
    if (!mesh.joints.empty()) {
        prependApiSchema(sdfData, primPath, UsdSkelTokens->SkelBindingAPI);

        Primvar<int> jointIndices;
        // XXX The interpolation should be either constant or vertex, but is hard coded to vertex
        // in the old code. This should be investigated. A rigid mesh has the same joint for all
        // vertices and a single weight of 1.0 for all vertices.
        // jointIndices.interpolation = mesh.isRigid ? UsdGeomTokens->constant :
        // UsdGeomTokens->vertex;
        jointIndices.interpolation = UsdGeomTokens->vertex;
        jointIndices.values = mesh.joints;
        SdfPath p = _writePrimvar(
          sdfData, primPath, "skel:jointIndices", SdfValueTypeNames->IntArray, jointIndices);
        setAttributeMetadata(sdfData, p, UsdGeomTokens->elementSize, VtValue(mesh.influenceCount));

        Primvar<float> jointWeights;
        // XXX Same note as above
        // jointWeights.interpolation = mesh.isRigid ? UsdGeomTokens->constant :
        // UsdGeomTokens->vertex;
        jointWeights.interpolation = UsdGeomTokens->vertex;
        jointWeights.values = mesh.weights;
        p = _writePrimvar(
          sdfData, primPath, "skel:jointWeights", SdfValueTypeNames->FloatArray, jointWeights);
        setAttributeMetadata(sdfData, p, UsdGeomTokens->elementSize, VtValue(mesh.influenceCount));

        // The geomBindTransform is in the primvar namespace, but is just a single attribute value
        createAttr(UsdSkelTokens->primvarsSkelGeomBindTransform,
                   SdfValueTypeNames->Matrix4d,
                   mesh.geomBindTransform);
    }

    // Material binding
    if (mesh.material >= 0) {
        _bindMaterial(sdfData, primPath, materialMap[mesh.material]);
    }

    // Subsets
    if (mesh.subsets.size()) {
        for (size_t i = 0; i < mesh.subsets.size(); i++) {
            const Subset& subset = mesh.subsets[i];
            TfToken subsetName = TfToken("sub" + std::to_string(i));
            SdfPath subsetPath = _createGeomSubset(sdfData, primPath, subsetName, subset);

            _bindMaterial(sdfData, subsetPath, materialMap[subset.material]);
        }
    }

    return primPath;
}

bool
_writePointsOrInstancedMesh(WriteSdfContext& ctx,
                            const SdfPath& parentPath,
                            const Mesh& mesh,
                            int meshIdx,
                            int childIdx)
{
    if (mesh.asPoints) {
        _writePoints(ctx.sdfData, parentPath, mesh);
    } else if (mesh.instanceable) {
        // XXX Note, slightly awkward name generator to match old behavior. Once the old code has
        // been removed, this can be cleaned up to any scheme that produces unique names
        std::string scopeNameStr =
          "GeomScope" + (childIdx == 0 ? "" : std::to_string(childIdx - 1));

        SdfPath scopePath =
          createPrimSpec(ctx.sdfData, parentPath, TfToken(scopeNameStr), UsdGeomTokens->Scope);

        const SdfPath& prototypePath = ctx.meshPrototypeMap[meshIdx];
        if (!prototypePath.IsEmpty()) {
            addPrimReference(ctx.sdfData, scopePath, SdfReference("", prototypePath));
            setPrimMetadata(ctx.sdfData, scopePath, SdfFieldKeys->Instanceable, VtValue(true));
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "layer::write gScope %s, Instance of %s\n",
                         scopePath.GetText(),
                         prototypePath.GetText());
        } else {
            _writeMesh(ctx.sdfData, scopePath, ctx.materialMap, mesh);
            // Add this first instance to the list of prototypes
            ctx.meshPrototypeMap[meshIdx] = scopePath;
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "layer::write gScope %s (add new prototype)\n",
                         scopePath.GetText());
        }
    } else {
        _writeMesh(ctx.sdfData, parentPath, ctx.materialMap, mesh);
    }

    return true;
}

void
_writeSkinnedMeshes(WriteSdfContext& ctx,
                    const SdfPath& parentPath,
                    const std::string& nodeName,
                    int skinnedMeshIdx,
                    const Skeleton& skeleton,
                    const SdfPath& skeletonPath,
                    const std::vector<int>& meshIndices)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "write skinned mesh: parent path=%s nodeName=%s\n",
                 parentPath.GetString().c_str(),
                 nodeName.c_str());

    // We don't create a primSpec here like we do in other functions as we are just adding
    // a SkelRoot as a child of the prim created in _writeNode.

    // XXX Note, slightly awkward name generator to match old behavior. Once the old code has been
    // removed, this can be cleaned up to any scheme that produces unique names
    std::string primNameStr =
      nodeName + "SkelRoot" + (skinnedMeshIdx == 0 ? "" : std::to_string(skinnedMeshIdx - 1));
    SdfPath skelRootPath =
      createPrimSpec(ctx.sdfData, parentPath, TfToken(primNameStr), UsdSkelTokens->SkelRoot);
    prependApiSchema(ctx.sdfData, skelRootPath, UsdSkelTokens->SkelBindingAPI);

    SdfPath skelPrimPath = createPrimSpec(ctx.sdfData, skelRootPath, TfToken(skeleton.name));
    addPrimReference(ctx.sdfData, skelPrimPath, SdfReference({}, skeletonPath));

    SdfPath p = createRelationshipSpec(ctx.sdfData, skelRootPath, UsdSkelTokens->skelSkeleton);
    prependRelationshipTarget(ctx.sdfData, p, skelPrimPath);

    if (!skeleton.animations.empty()) {
        SdfPath skelAnimPath = createPrimSpec(ctx.sdfData, skelRootPath, _tokens->anim);

        // XXX Hard coded to the first animation currently
        if (!ctx.animationMap.empty()) {
            SdfPath animationPath = ctx.animationMap[0];
            addPrimReference(ctx.sdfData, skelAnimPath, SdfReference({}, animationPath));

            p =
              createRelationshipSpec(ctx.sdfData, skelRootPath, UsdSkelTokens->skelAnimationSource);
            prependRelationshipTarget(ctx.sdfData, p, skelAnimPath);
        }
    }
    int i = 0;
    for (int meshIndex : meshIndices) {
        const Mesh& mesh = ctx.usdData->meshes[meshIndex];
        _writePointsOrInstancedMesh(ctx, skelRootPath, mesh, meshIndex, i++);
    }
}

// Layout of control points in USD is: row-major with U considered rows, and V columns.
// So: u0v0, u0v1, ... u0vx, u1v0, ...
// but after tests, seems USD is really column-major, as are its transforms.
// So really: u0v0, u1v0, ... uxv0, u0v1, ...
SdfPath
_writeNurb(SdfAbstractData* sdfData, const SdfPath& parentPath, NurbData& nurb)
{
    SdfPath primPath =
      createPrimSpec(sdfData, parentPath, TfToken(nurb.name), UsdGeomTokens->NurbsPatch);

    auto createAttr = [&](const TfToken& name, const SdfValueTypeName& type, const auto& value) {
        SdfPath p = createAttributeSpec(sdfData, primPath, name, type);
        setAttributeDefaultValue(sdfData, p, value);
    };

    createAttr(UsdGeomTokens->uOrder, SdfValueTypeNames->Int, nurb.uOrder);
    createAttr(UsdGeomTokens->vOrder, SdfValueTypeNames->Int, nurb.vOrder);
    createAttr(UsdGeomTokens->uKnots, SdfValueTypeNames->DoubleArray, nurb.uKnots);
    createAttr(UsdGeomTokens->vKnots, SdfValueTypeNames->DoubleArray, nurb.vKnots);
    createAttr(UsdGeomTokens->uVertexCount, SdfValueTypeNames->Int, nurb.uControlPointCount);
    createAttr(UsdGeomTokens->vVertexCount, SdfValueTypeNames->Int, nurb.vControlPointCount);
    createAttr(UsdGeomTokens->points, SdfValueTypeNames->Point3fArray, nurb.controlPoints);

    // According to USD, ranges must comply xKnots[xOrder-1] <= Xmin < Xmax <= xKnots.back()
    // Here we take the full range, do other file formats encode a range themselves?
    GfVec2d uRange(nurb.uKnots[nurb.uOrder - 1], nurb.uKnots.back());
    GfVec2d vRange(nurb.vKnots[nurb.vOrder - 1], nurb.vKnots.back());
    createAttr(UsdGeomTokens->uRange, SdfValueTypeNames->Double2, uRange);
    createAttr(UsdGeomTokens->vRange, SdfValueTypeNames->Double2, vRange);

    if (nurb.weights.size() > 0) {
        createAttr(UsdGeomTokens->pointWeights, SdfValueTypeNames->DoubleArray, nurb.weights);
    }
    if (nurb.trimCurveCounts.size()) {
        createAttr(
          UsdGeomTokens->trimCurveCounts, SdfValueTypeNames->IntArray, nurb.trimCurveCounts);
        createAttr(
          UsdGeomTokens->trimCurveKnots, SdfValueTypeNames->DoubleArray, nurb.trimCurveKnots);
        createAttr(
          UsdGeomTokens->trimCurveOrders, SdfValueTypeNames->IntArray, nurb.trimCurveOrders);
        createAttr(
          UsdGeomTokens->trimCurvePoints, SdfValueTypeNames->Double3Array, nurb.trimCurvePoints);
        createAttr(
          UsdGeomTokens->trimCurveRanges, SdfValueTypeNames->Double2Array, nurb.trimCurveRanges);
        createAttr(UsdGeomTokens->trimCurveVertexCounts,
                   SdfValueTypeNames->IntArray,
                   nurb.trimCurveVertexCounts);
    }

    // Display color
    VtArray<GfVec3f> color = { GfVec3f(0.9f, 0.9f, 0.9f) };
    createAttr(UsdGeomTokens->primvarsDisplayColor, SdfValueTypeNames->Color3fArray, color);
    createAttr(UsdGeomTokens->doubleSided, SdfValueTypeNames->Bool, true);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "layer::write nurb { %s, knotType: %d, srfcForm: %d, order: {%d, %d}, knots: "
                 "{%zu, %zu}, ctrlPts: {%d, %d}, w: %zu, trims: %zu",
                 primPath.GetText(),
                 nurb.knotType,
                 nurb.surfaceForm,
                 nurb.uOrder,
                 nurb.vOrder,
                 nurb.uKnots.size(),
                 nurb.vKnots.size(),
                 nurb.uControlPointCount,
                 nurb.vControlPointCount,
                 nurb.weights.size(),
                 nurb.trimCurveCounts.size());

    return primPath;
}

// forward declaration
void
_writeNodes(WriteSdfContext& ctx,
            const SdfPath& parentPath,
            const std::vector<int>& childNodeIndices);

// Creates a prim spec without adding the prim as a child of the parent. The list of children to be
// added to the parent is accumulated and then added to the parent once all the children are
// created. This provides a significant improvement in load performance, especially when the number
// of children is large.
void
_createNode(WriteSdfContext& ctx,
            const SdfPath& parentPath,
            const Node& node,
            std::vector<SdfPath>& childPaths,
            std::vector<TfToken>& children)
{
    TfToken child(node.name);
    SdfPath primPath = createPrimSpec(ctx.sdfData,
                                      parentPath,
                                      TfToken(node.name),
                                      UsdGeomTokens->Xform,
                                      PXR_NS::SdfSpecifier::SdfSpecifierDef,
                                      /* append = */ false);
    childPaths.push_back(primPath);
    children.push_back(child);
}

// Writes XForm prims with transform data into the stage.
// Note when the node cache data contains SkelMesh data, it spawns an extra UsdSkelRoot prim
// with its associated relationships/prims.
bool
_writeNode(WriteSdfContext& ctx, const SdfPath& primPath, const Node& node)
{
    _writeXformAttributes(ctx.sdfData, primPath, node);

    if (node.camera >= 0) {
        _writeCamera(ctx.sdfData, primPath, ctx.usdData->cameras[node.camera]);
    }

    if (node.ngp >= 0) {
        _writeNgp(ctx.sdfData, primPath, ctx.usdData->ngps[node.ngp]);
    }

    int i = 0;
    for (int meshIndex : node.staticMeshes) {
        const Mesh& mesh = ctx.usdData->meshes[meshIndex];
        _writePointsOrInstancedMesh(ctx, primPath, mesh, meshIndex, i++);
    }

    // Note that this will author UsdSkelRoots as siblings to the node just authored above.
    // This is because the above node is supposed to be a skeleton root, and we don't want its
    // transform to take effect 2 times.
    int skinnedMeshIdx = 0;
    for (const auto& [skeletonIndex, meshIndices] : node.skinnedMeshes) {
        const Skeleton& skeleton = ctx.usdData->skeletons[skeletonIndex];
        const SdfPath& skeletonPath = ctx.skeletonMap[skeletonIndex];
        // write the skeleton root as a child of this prim
        _writeSkinnedMeshes(
          ctx, primPath, node.name, skinnedMeshIdx++, skeleton, skeletonPath, meshIndices);
    }

    _writeNodes(ctx, primPath, node.children);

    return true;
}

void
_writeNodes(WriteSdfContext& ctx, const SdfPath& parentPath, const std::vector<const Node*>& nodes)
{
    if (nodes.empty())
        return;

    std::vector<SdfPath> childPaths;
    std::vector<TfToken> childTokens;
    std::vector<const Node*> nodesCreated;
    const size_t numChildren = nodes.size();
    childPaths.reserve(numChildren);
    childTokens.reserve(numChildren);
    nodesCreated.reserve(numChildren);

    // Create all the child prims first and then add them all as children. This is
    // much more efficient than creating each child and adding each child to the parent.
    for (const Node* node : nodes) {
        if (ctx.options->pruneJoints && node->isJoint) {
            TF_DEBUG_MSG(
              FILE_FORMAT_UTIL, "sdfData::write pruned joint node %s\n", node->name.c_str());
            continue;
        }
        _createNode(ctx, parentPath, *node, childPaths, childTokens);
        nodesCreated.push_back(node);
    }

    if (nodesCreated.size() > 0) {
        // Add all the children to the parent in one shot
        appendToChildList(ctx.sdfData, parentPath, childTokens);

        // write/convert each child node to USD
        const size_t numNewChildren = nodesCreated.size();
        for (size_t i = 0; i < numNewChildren; ++i) {
            const Node* childNode = nodesCreated[i];
            _writeNode(ctx, childPaths[i], *childNode);
        }
    }
}

void
_writeNodes(WriteSdfContext& ctx,
            const SdfPath& parentPath,
            const std::vector<int>& childNodeIndices)
{
    if (childNodeIndices.empty())
        return;
    std::vector<const Node*> childNodes;
    childNodes.reserve(childNodeIndices.size());
    for (int childNodeIndex : childNodeIndices) {
        const Node& childNode = ctx.usdData->nodes[childNodeIndex];
        childNodes.push_back(&childNode);
    }
    _writeNodes(ctx, parentPath, childNodes);
}

void
_writeNonParentedNodes(WriteSdfContext& ctx,
                       const SdfPath& parentPath,
                       const std::vector<Node>& nodes)
{
    if (nodes.empty())
        return;

    std::vector<const Node*> filteredNodes;
    filteredNodes.reserve(nodes.size());
    for (const Node& node : nodes) {
        // Skip nodes with a parent, since they are a child of another node
        if (node.parent != -1) {
            continue;
        }
        filteredNodes.push_back(&node);
    }
    _writeNodes(ctx, parentPath, filteredNodes);
}

SdfPath
_writeSkeleton(SdfAbstractData* sdfData, const SdfPath& parentPath, const Skeleton& skeleton)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "write skeleton: parent path=%s, node=%s\n",
                 parentPath.GetString().c_str(),
                 skeleton.name.c_str());

    SdfPath primPath =
      createPrimSpec(sdfData, parentPath, TfToken(skeleton.name), UsdSkelTokens->Skeleton);
    prependApiSchema(sdfData, primPath, UsdSkelTokens->SkelBindingAPI);

    auto createAttr = [&](const TfToken& name,
                          const SdfValueTypeName& type,
                          const auto& value,
                          bool uniform = false) {
        SdfVariability variability =
          uniform ? PXR_NS::SdfVariabilityUniform : PXR_NS::SdfVariabilityVarying;
        SdfPath p = createAttributeSpec(sdfData, primPath, name, type, variability);
        setAttributeDefaultValue(sdfData, p, value);
    };

    createAttr(UsdSkelTokens->joints, SdfValueTypeNames->TokenArray, skeleton.joints, true);
    createAttr(UsdSkelTokens->jointNames, SdfValueTypeNames->TokenArray, skeleton.jointNames, true);
    createAttr(UsdSkelTokens->restTransforms,
               SdfValueTypeNames->Matrix4dArray,
               skeleton.restTransforms,
               true);
    createAttr(UsdSkelTokens->bindTransforms,
               SdfValueTypeNames->Matrix4dArray,
               skeleton.bindTransforms,
               true);

    // Mark the skeleton prim as invisible, otherwise it will render it as a visualization
    createAttr(UsdGeomTokens->visibility, SdfValueTypeNames->Token, UsdGeomTokens->invisible);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "sdfData::write skel { %s, joints: %zu, jointNames: %zu, restTransforms: %zu, "
                 "bindTransforms: %zu }\n",
                 primPath.GetText(),
                 skeleton.joints.size(),
                 skeleton.jointNames.size(),
                 skeleton.restTransforms.size(),
                 skeleton.bindTransforms.size());

    return primPath;
}

SdfPath
_writeAnimation(SdfAbstractData* sdfData, const SdfPath& parentPath, const Animation& animation)
{
    SdfPath primPath =
      createPrimSpec(sdfData, parentPath, TfToken(animation.name), UsdSkelTokens->SkelAnimation);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "layer::write animation '%s' at path %s\n",
                 animation.name.c_str(),
                 primPath.GetText());

    SdfPath p;
    p = createAttributeSpec(sdfData,
                            primPath,
                            UsdSkelTokens->joints,
                            SdfValueTypeNames->TokenArray,
                            SdfVariabilityUniform);
    setAttributeDefaultValue(sdfData, p, animation.joints);

    SdfPath rotAttrPath = createAttributeSpec(
      sdfData, primPath, UsdSkelTokens->rotations, SdfValueTypeNames->QuatfArray);
    SdfPath transAttrPath = createAttributeSpec(
      sdfData, primPath, UsdSkelTokens->translations, SdfValueTypeNames->Float3Array);
    SdfPath scaleAttrPath =
      createAttributeSpec(sdfData, primPath, UsdSkelTokens->scales, SdfValueTypeNames->Half3Array);

    // Note, setAttributeTimeSampledValues can lead to slight different numerical results in the
    // time sampled data for some reason. To match the old output 100% we use this form of the API.
    for (size_t i = 0; i < animation.times.size(); ++i) {
        double time = animation.times[i];
        sdfData->SetTimeSample(rotAttrPath, time, VtValue(animation.rotations[i]));
        sdfData->SetTimeSample(transAttrPath, time, VtValue(animation.translations[i]));
        sdfData->SetTimeSample(scaleAttrPath, time, VtValue(animation.scales[i]));
    }

    return primPath;
}

SdfPath
_writeMaterial(WriteSdfContext& ctx, const SdfPath& parentPath, const Material& material)
{
    SdfPath materialPath = createMaterialPrimSpec(ctx.sdfData, parentPath, TfToken(material.name));

    printMaterial("layer::write", materialPath, material, ctx.debugTag);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "layer::write material '%s' to %s\n",
                 material.name.c_str(),
                 materialPath.GetText());

    SdfPath p =
      createShaderInput(ctx.sdfData, materialPath, stPrimvarNameAttrName, SdfValueTypeNames->Token);
    setAttributeDefaultValue(ctx.sdfData, p, AdobeTokens->st);

    // Generate a UsdPreviewSurface based material network
    writeUsdPreviewSurface(ctx, materialPath, material);

#ifdef USD_FILEFORMATS_ENABLE_ASM
    // Generate a ASM based material network
    writeAsmMaterial(ctx, materialPath, material);
#endif // USD_FILEFORMATS_ENABLE_ASM

    if (ctx.options->writeMaterialX) {
      // Generate a MaterialX based material network
      writeMaterialX(ctx, materialPath, material);
    }

    return materialPath;
}

void
_writeImage(const std::string& assetsPath, const ImageAsset& image)
{
    std::string filename = assetsPath + "/" + image.uri;
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (!file.is_open())
        return;
    file.write(reinterpret_cast<const char*>(image.image.data()), image.image.size());
    file.close();
}

bool
_writeLayerSdfData(const WriteLayerOptions& options,
                   const UsdData& usdData,
                   const std::string& layerName,
                   const std::string& resolvedPath,
                   const SdfAbstractDataRefPtr& sdfDataPtr,
                   const std::string& debugTag)
{
    // Get the raw pointer for efficiency while we hold the ref pointer
    SdfAbstractData* sdfData = get_pointer(sdfDataPtr);

    WriteSdfContext ctx;
    ctx.options = &options;
    ctx.usdData = &usdData;
    ctx.sdfData = sdfData;
    // If assetsPaths is valid, we write all images to disk and relative texture paths will be
    // authored to those files. Otherwise we reference the assets with paths from within the source
    // asset.
    ctx.srcAssetFilename = options.assetsPath.empty() ? TfAbsPath(resolvedPath) : std::string();
    ctx.debugTag = debugTag;

    createPseudoRootSpec(sdfData);

    std::string layerStem = TfStringGetBeforeSuffix(TfGetBaseName(layerName));
    TfToken rootNodeName = TfToken(TfMakeValidIdentifier(layerStem));
    SdfPath rootNodePath =
      createPrimSpec(sdfData, SdfPath::AbsoluteRootPath(), rootNodeName, UsdGeomTokens->Xform);

    _writeMetadata(sdfData, usdData, rootNodePath);

    if (!usdData.materials.empty()) {
        ctx.materialMap.resize(usdData.materials.size());
        TfToken materialsPrimName("Materials");
        SdfPath materialsPath = createPrimSpec(sdfData, rootNodePath, materialsPrimName);
        int i = 0;
        for (const Material& material : usdData.materials) {
            ctx.materialMap[i++] = _writeMaterial(ctx, materialsPath, material);
        }
    }

    if (!usdData.skeletons.empty()) {
        ctx.skeletonMap.resize(usdData.skeletons.size());
        TfToken skeletonsPrimName("Skeletons");
        SdfPath skeletonsPath = createPrimSpec(sdfData, rootNodePath, skeletonsPrimName);
        int i = 0;
        for (const Skeleton& skeleton : usdData.skeletons) {
            ctx.skeletonMap[i++] = _writeSkeleton(sdfData, skeletonsPath, skeleton);
        }
    }

    if (!usdData.animations.empty()) {
        ctx.animationMap.resize(usdData.animations.size());
        TfToken animationsPrimName("Animations");
        SdfPath animationsPath = createPrimSpec(sdfData, rootNodePath, animationsPrimName);
        int i = 0;
        for (const Animation& animation : usdData.animations) {
            ctx.animationMap[i++] = _writeAnimation(sdfData, animationsPath, animation);
        }
    }

    // This map is filled with paths to prototypes as we process instanceable meshes
    ctx.meshPrototypeMap.resize(usdData.meshes.size());

    if (usdData.nodes.size()) {
        if (!usdData.rootNodes.empty()) {
            _writeNodes(ctx, rootNodePath, usdData.rootNodes);
        } else {
            // XXX fallback for when the file format does not designate root notes. The GLTF plugin
            // used to do this, but that should be fixed for all plugins!
            TF_WARN("Writing of UsdData to layer %s without explicit root nodes",
                    resolvedPath.c_str());
            _writeNonParentedNodes(ctx, rootNodePath, usdData.nodes);
        }
    }

    // If requested, write the images to files on disk
    if (!options.assetsPath.empty() && usdData.images.size()) {
        TfMakeDirs(options.assetsPath, -1, true);
        for (const ImageAsset& image : usdData.images) {
            _writeImage(options.assetsPath, image);
        }
    }

    return true;
}

bool
writeLayer(const WriteLayerOptions& options,
           UsdData& data,
           SdfLayer* layer,
           PXR_NS::SdfAbstractDataRefPtr& sdfData,
           const std::string& debugTag,
           SetLayerDataFn setLayerDataFn)
{
    TfStopwatch layerWriteSW;
    layerWriteSW.Start();

    // These checks are only active when the the FILE_FORMAT_UTIL TfDebug flag is on
    checkAndPrintMeshIssues(data);

    // Make sure all names in the data are unique and suitable as prim names
    // Note, this potentially modifies the usdData
    uniquifyNames(data);

    GUARD(_writeLayerSdfData(options,
                             data,
                             layer->GetDisplayName(),
                             layer->GetResolvedPath().GetPathString(),
                             sdfData,
                             debugTag),
          "Error writing to the SdfData\n");

    setLayerDataFn(layer, sdfData);

    layerWriteSW.Stop();
    TF_DEBUG_MSG(
      FILE_FORMAT_UTIL, "Write layer via Sdf API: %ld ms\n",
        static_cast<long int>(layerWriteSW.GetMilliseconds()));

    return true;
}
}
