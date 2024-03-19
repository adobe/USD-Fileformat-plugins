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
#include "layerRead.h"
#include "common.h"
#include "debugCodes.h"
#include "geometry.h"
#include "usdData.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/pcp/cache.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/typed.h>
#include <pxr/usd/usd/zipFile.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/hermiteCurves.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/nurbsCurves.h>
#include <pxr/usd/usdGeom/nurbsPatch.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/utils.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/usdVol/volume.h>
#include <string>
#include <vector>

using namespace PXR_NS;

namespace adobe::usd {

struct ReadLayerContext
{
    UsdStageRefPtr stage;
    UsdData* usd;
    const ReadLayerOptions* options;
    std::unordered_map<std::string, int> prototypes;
    std::unordered_map<std::string, int> images;
    std::unordered_map<std::string, int> imageNames;
    std::unordered_map<std::string, int> materials;
    std::unordered_map<std::string, int> ngps;
    std::vector<std::string> materialBindings;
    std::vector<std::vector<std::string>> subsetMaterialBindings;
    UsdGeomXformCache xformCache;
    std::string debugTag;
};

// Gets the UsdData parent node with index 'parent', with the condition that if 'prim' has a
// transform, like a UsdGeomMesh or a UsdCamera, then we extract that transform and put it
// in a child node of the original parent. This is so native file formats which cannot put
// transform data into objects like meshes and cameras, can still import said transform data,
// only now as part of the node hierarchy.
Node&
getParentOrNewTransformParent(ReadLayerContext& ctx,
                              const UsdPrim& prim,
                              int parent,
                              const std::string& newParentName)
{
    GfMatrix4d transform;
    UsdGeomXformable xformable{ prim };
    bool resetXFormStack = false;
    xformable.GetLocalTransformation(&transform, &resetXFormStack);
    if (transform != GfMatrix4d(0.0f) && transform != GfMatrix4d(1.0f)) {
        auto [nodeIndex, node] = ctx.usd->addNode(parent);
        node.name = newParentName;
        node.transform = transform;
        node.hasTransform = true;
        GfMatrix4d parentWorldTransform =
          parent != -1 ? ctx.usd->nodes[parent].worldTransform : GfMatrix4d(1);
        node.worldTransform = node.transform * parentWorldTransform;
        return node;
    } else {
        auto [nodeIndex, node] = ctx.usd->getParent(parent);
        return node;
    }
}

bool
readPrim(ReadLayerContext& ctx, const UsdPrim& prim, int parent);

int
readTransform(ReadLayerContext& ctx, const UsdPrim& prim, Node& node, int parent)
{
    UsdGeomXformable xformable{ prim };
    bool resetXFormStack = false;
    xformable.GetLocalTransformation(
      &node.transform, &resetXFormStack, UsdTimeCode::EarliestTime());
    node.hasTransform = node.transform != GfMatrix4d(0.0f) && node.transform != GfMatrix4d(1.0f);
    GfMatrix4d parentWorldTransform =
      parent != -1 ? ctx.usd->nodes[parent].worldTransform : GfMatrix4d(1);
    node.worldTransform = node.transform * parentWorldTransform;
    return true;
}

bool
readScope(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read  scope   { %s }\n",
                 ctx.debugTag.c_str(),
                 prim.GetPath().GetText());
    auto [nodeIndex, node] = ctx.usd->addNode(parent);
    node.name = prim.GetName().GetString();
    node.path = prim.GetPath().GetString();
    readTransform(ctx, prim, node, parent);
    UsdPrimSiblingRange children =
      prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));
    for (const UsdPrim& p : children) {
        readPrim(ctx, p, nodeIndex);
    }
    return true;
}

bool
readUnknown(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read %s { %s }\n",
                 ctx.debugTag.c_str(),
                 prim.GetTypeName().GetText(),
                 prim.GetName().GetText());
    auto [nodeIndex, node] = ctx.usd->addNode(parent);
    node.name = prim.GetName().GetString();
    node.path = prim.GetPath().GetString();
    readTransform(ctx, prim, node, parent);
    UsdPrimSiblingRange children =
      prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));
    for (const UsdPrim& p : children) {
        readPrim(ctx, p, nodeIndex);
    }
    return true;
}

bool
readNode(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    auto [nodeIndex, node] = ctx.usd->addNode(parent);
    node.name = prim.GetName().GetString();
    node.path = prim.GetPath().GetString();
    readTransform(ctx, prim, node, parent);
    UsdGeomXformable xformable{ prim };
    // TODO: Set individual operations
    // node.rotation =
    // node.translation =
    // node.scale =
    bool reset;
    auto ops = xformable.GetOrderedXformOps(&reset);
    std::vector<UsdGeomXformOp::Type> opTypes(ops.size());
    for (unsigned int i = 0; i < ops.size(); i++) {
        opTypes[i] = ops[i].GetOpType();
    }
    bool hasTranslation = false;
    bool hasRotation = false;
    bool hasScale = false;
    UsdGeomXformOp translationOp;
    UsdGeomXformOp rotationOp;
    UsdGeomXformOp scaleOp;
    // TODO review if we covered xformOperation possibilites correctly
    std::vector<std::vector<UsdGeomXformOp::Type>> opTests = {
        { UsdGeomXformOp::TypeTranslate, UsdGeomXformOp::TypeOrient, UsdGeomXformOp::TypeScale },
        { UsdGeomXformOp::TypeTranslate, UsdGeomXformOp::TypeOrient },
        { UsdGeomXformOp::TypeTranslate, UsdGeomXformOp::TypeScale },
        { UsdGeomXformOp::TypeOrient, UsdGeomXformOp::TypeScale },
        { UsdGeomXformOp::TypeTranslate },
        { UsdGeomXformOp::TypeOrient },
        { UsdGeomXformOp::TypeScale },
    };
    for (unsigned int i = 0; i < opTests.size(); i++) {
        if (opTypes == opTests[i]) {
            for (unsigned int j = 0; j < opTypes.size(); j++) {
                if (opTypes[j] == UsdGeomXformOp::TypeTranslate) {
                    hasTranslation = true;
                    translationOp = ops[j];
                } else if (opTypes[j] == UsdGeomXformOp::TypeOrient) {
                    hasRotation = true;
                    rotationOp = ops[j];
                } else if (opTypes[j] == UsdGeomXformOp::TypeScale) {
                    hasScale = true;
                    scaleOp = ops[j];
                }
            }
            break;
        }
    }
    if (hasTranslation) {
        std::vector<double> times;
        translationOp.GetTimeSamples(&times);
        node.translations.times.resize(times.size());
        node.translations.values.resize(times.size());
        for (unsigned int i = 0; i < times.size(); i++) {
            node.translations.times[i] = times[i];
            translationOp.Get(&node.translations.values[i], node.translations.times[i]);
        }
    }
    if (hasRotation) {
        std::vector<double> times;
        rotationOp.GetTimeSamples(&times);
        node.rotations.times.resize(times.size());
        node.rotations.values.resize(times.size());
        for (unsigned int i = 0; i < times.size(); i++) {
            node.rotations.times[i] = times[i];
            rotationOp.Get(&node.rotations.values[i], node.rotations.times[i]);
        }
    }
    if (hasScale) {
        std::vector<double> times;
        scaleOp.GetTimeSamples(&times);
        node.scales.times.resize(times.size());
        node.scales.values.resize(times.size());
        for (unsigned int i = 0; i < times.size(); i++) {
            node.scales.times[i] = times[i];
            scaleOp.Get(&node.scales.values[i], node.scales.times[i]);
        }
    }
    UsdPrimSiblingRange children =
      prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));
    for (const UsdPrim& p : children) {
        readPrim(ctx, p, nodeIndex);
    }
    return true;
}

template<typename T>
static bool
readPrimvar(UsdGeomPrimvarsAPI& api, const TfToken& name, Primvar<T>& primvar)
{
    UsdGeomPrimvar pv = api.GetPrimvar(name);
    if (pv.IsDefined()) {
        pv.Get(&primvar.values, 0);
        pv.GetIndices(&primvar.indices, 0);
        primvar.interpolation = pv.GetInterpolation();
        return true;
    }
    return false;
}

TfToken
findPrimaryTextureCoordinatePrimvar(const UsdGeomPrimvarsAPI& api)
{
    TfTokenVector texCoordPrimvarNames;
    for (const UsdGeomPrimvar& primvar : api.GetPrimvarsWithAuthoredValues()) {
        SdfValueTypeName typeName = primvar.GetTypeName();
        // TODO add support for TexCoord2hArray/Half2Array
        if (typeName == SdfValueTypeNames->TexCoord2fArray ||
            typeName == SdfValueTypeNames->Float2Array) {
            TfToken primvarName = primvar.GetPrimvarName();
            // We always take 'st' as the default primvar if it exists
            if (primvarName == AdobeTokens->st) {
                return AdobeTokens->st;
            }
            texCoordPrimvarNames.push_back(primvarName);
        }
    }
    // If we didn't find 'st' we use the first valid texture coordinate
    TfToken result = texCoordPrimvarNames.empty() ? TfToken() : texCoordPrimvarNames[0];
    // ... and warn if we had multiple choices.
    if (texCoordPrimvarNames.size() > 1) {
        std::stringstream ss;
        bool first = true;
        for (const TfToken& primvarName : texCoordPrimvarNames) {
            if (first) {
                first = false;
            } else {
                ss << ", ";
            }
            ss << primvarName;
        }

        TF_WARN("Mesh %s has multiple UV coordinates: [%s]. Using %s for export",
                api.GetPrim().GetPath().GetText(),
                ss.str().c_str(),
                result.GetText());
    }
    return result;
}

bool
readMeshData(ReadLayerContext& ctx, Mesh& mesh, int meshIndex, const UsdPrim& prim)
{
    ctx.materialBindings.push_back("");
    ctx.subsetMaterialBindings.push_back({});

    mesh.name = prim.GetName();
    UsdGeomMesh usdMesh(prim);
    UsdGeomPrimvarsAPI primvarsAPI(usdMesh);

    usdMesh.GetDoubleSidedAttr().Get(&mesh.doubleSided);
    usdMesh.GetFaceVertexCountsAttr().Get(&mesh.faces, 0);
    usdMesh.GetFaceVertexIndicesAttr().Get(&mesh.indices, 0);
    usdMesh.GetPointsAttr().Get(&mesh.points, 0);
    usdMesh.GetSubdivisionSchemeAttr().Get(&mesh.subdivisionScheme, 0);

    UsdAttribute normalsAttr = usdMesh.GetNormalsAttr();
    if (readPrimvar(primvarsAPI, UsdGeomTokens->normals, mesh.normals)) {
    } else if (normalsAttr.IsAuthored()) {
        normalsAttr.Get(&mesh.normals.values, 0);
        mesh.normals.interpolation = usdMesh.GetNormalsInterpolation();
    }

    TfToken primvaryTexCoordPrimvar = findPrimaryTextureCoordinatePrimvar(primvarsAPI);
    if (primvaryTexCoordPrimvar.IsEmpty()) {
        TF_WARN("No texture coordinates for mesh %s", prim.GetPath().GetText());
    } else {
        readPrimvar(primvarsAPI, primvaryTexCoordPrimvar, mesh.uvs);
    }

    Primvar<PXR_NS::GfVec3f> displayColor;
    Primvar<float> displayOpacity;
    readPrimvar(primvarsAPI, UsdGeomTokens->primvarsDisplayColor, displayColor);
    readPrimvar(primvarsAPI, UsdGeomTokens->primvarsDisplayOpacity, displayOpacity);
    if (displayColor.values.size()) {
        auto [colorSetIndex, colorSet] = ctx.usd->addColorSet(meshIndex);
        colorSet.indices = displayColor.indices;
        colorSet.values = displayColor.values;
        colorSet.interpolation = displayColor.interpolation;
    }
    if (displayOpacity.values.size()) {
        auto [opacitySetIndex, opacitySet] = ctx.usd->addOpacitySet(meshIndex);
        opacitySet.indices = displayOpacity.indices;
        opacitySet.values = displayOpacity.values;
        opacitySet.interpolation = displayOpacity.interpolation;
    }

    const auto& materialBinding = UsdShadeMaterialBindingAPI(prim);
    const auto& material = materialBinding.ComputeBoundMaterial();
    if (material) {
        ctx.materialBindings[meshIndex] = material.GetPath().GetString();
    }
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
    UsdPrimSiblingRange children =
      prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));
    for (const UsdPrim& child : children) {
        if (child.IsA<UsdGeomSubset>()) {
            ctx.subsetMaterialBindings.back().push_back("");
            const auto& materialBinding = UsdShadeMaterialBindingAPI(child);
            const auto& material = materialBinding.ComputeBoundMaterial();
            auto [subsetIndex, subset] = ctx.usd->addSubset(meshIndex);
            UsdGeomSubset usdSubset = UsdGeomSubset(child);
            usdSubset.GetIndicesAttr().Get(&subset.faces);
            if (material) {
                ctx.subsetMaterialBindings[meshIndex][subsetIndex] = material.GetPath().GetString();
            }
        }
    }

    if (ctx.options->triangulate) {
        triangulateMesh(mesh);
        // Separate flag for this?
        forceVertexInterpolation(mesh);
    }

    // After reading the geometry subsets and potentially triangulating and expanding the mesh to
    // force vertex interpolation we pre-compute a set of face vertex indices for each subset that
    // index into the points buffer of the main mesh
    for (Subset& subset : mesh.subsets) {
        // Compute the face vertex indices of the subset based on the face indices that define the
        // subset
        computeFaceVertexIndicesForSubset(mesh.faces, mesh.indices, subset.faces, subset.indices);
    }

    return true;
}

bool
readSkinData(ReadLayerContext& ctx, Mesh& mesh, const PXR_NS::UsdSkelSkinningQuery& skinningQuery)
{
    skinningQuery.ComputeJointInfluences(&mesh.joints, &mesh.weights);
    mesh.geomBindTransform = skinningQuery.GetGeomBindTransform();
    bool isRigid = skinningQuery.IsRigidlyDeformed();
    if (isRigid) {
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Is rigid, computing varying joint influences on %lu points\n",
                     ctx.debugTag.c_str(),
                     mesh.points.size());
        skinningQuery.ComputeVaryingJointInfluences(
          mesh.points.size(), &mesh.joints, &mesh.weights);
    }
    mesh.influenceCount = skinningQuery.GetNumInfluencesPerComponent();
    if (ctx.options->maxMeshInfluenceCount > 0 &&
        mesh.influenceCount > ctx.options->maxMeshInfluenceCount) {
        UsdSkelResizeInfluences(
          &mesh.joints, mesh.influenceCount, ctx.options->maxMeshInfluenceCount);
        UsdSkelResizeInfluences(
          &mesh.weights, mesh.influenceCount, ctx.options->maxMeshInfluenceCount);
        mesh.influenceCount = ctx.options->maxMeshInfluenceCount;
    }

    return true;
}

bool
readMesh(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    const std::string& path = prim.GetPrimInPrototype().GetPath().GetString();
    if (prim.IsInstanceProxy()) {
        auto it = ctx.prototypes.find(path);
        if (it != ctx.prototypes.end()) {
            int meshIndex = ctx.prototypes[path];
            Node& node = getParentOrNewTransformParent(ctx, prim, parent, "MeshTransform");
            node.staticMeshes.push_back(meshIndex);
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "%s: layer::read Found prototype %d: %s\n",
                         ctx.debugTag.c_str(),
                         meshIndex,
                         path.c_str());
            return true;
        }
    }
    auto [meshIndex, mesh] = ctx.usd->addMesh();
    Node& node = getParentOrNewTransformParent(ctx, prim, parent, "MeshTransform");
    node.staticMeshes.push_back(meshIndex);

    readMeshData(ctx, mesh, meshIndex, prim);
    if (prim.IsInstanceProxy()) {
        ctx.prototypes[path] = meshIndex;
        mesh.instanceable = true;
    }
    printMesh("layer::read", mesh, ctx.debugTag);
    return true;
}

// Reads a UsdSkelRoot prim into the the UsdData cache.
//
// This function discovers and processes, for all bindings in a UsdSKelRoot:
// * a UsdSkelSkeleton
// * a UsdSkelAnimation
// * several skinning targets (only UsdGeomMesh)
// The discovery of the associated prims is done via queries from the Skeleton API,
// instead of visiting children and checking manually, because it's easier and standard.
//
// The data is dumped into the following in the UsdData cache:
// * an Animation struct
// * several Mesh structs
// * a Skeleton struct, linked to the previous animation and meshes.
// * a Node struct, linked to the previous skeleton.
//
// UsdGeomMesh targets need to have their world transform (up to the UsdSkelRoot prim) applied,
// before being handed over.
//
// Could we benefit from uniquely caching the found UsdSkelSkeleton, UsdSkelAnimation &
// UsdGeomMesh data?
bool
readSkelRoot(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read skelRoot begin %s\n",
                 ctx.debugTag.c_str(),
                 prim.GetPath().GetText());
    auto [nodeIndex, node] = ctx.usd->addNode(parent);
    node.name = prim.GetName().GetString();
    node.path = prim.GetPath().GetString();

    PXR_NS::UsdSkelCache skelCache; // to hoist later to see performance improvement
    PXR_NS::UsdSkelRoot skelRoot(prim);
    skelCache.Populate(skelRoot, PXR_NS::UsdTraverseInstanceProxies());
    std::vector<PXR_NS::UsdSkelBinding> bindings;
    skelCache.ComputeSkelBindings(skelRoot, &bindings, PXR_NS::UsdTraverseInstanceProxies());
    for (const PXR_NS::UsdSkelBinding& binding : bindings) {

        // Process skeleton data
        auto [skeletonIndex, skeleton] = ctx.usd->addSkeleton();
        const UsdSkelSkeleton& skelSkeleton = binding.GetSkeleton();
        const UsdSkelSkeletonQuery& skelQuery = skelCache.GetSkelQuery(skelSkeleton);
        const UsdSkelTopology& topology = skelQuery.GetTopology();
        skeleton.joints = skelQuery.GetJointOrder();
        skelSkeleton.GetRestTransformsAttr().Get(&skeleton.restTransforms, 0);
        skelSkeleton.GetBindTransformsAttr().Get(&skeleton.bindTransforms, 0);
        skeleton.parents.resize(skeleton.joints.size());
        skeleton.inverseBindTransforms.resize(skeleton.joints.size());
        for (unsigned int i = 0; i < skeleton.joints.size(); i++) {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "%s: layer::read %-10s %s\n",
                         ctx.debugTag.c_str(),
                         "SkelJoint",
                         skeleton.joints[i].GetText());
            skeleton.parents[i] = topology.GetParent(i);
            skeleton.inverseBindTransforms[i] = skeleton.bindTransforms[i].GetInverse();
            // printMatrix("Bind matrix " + std::to_string(i), skeleton.bindTransforms[i]);
            // printMatrix("Inverse bind matrix " + std::to_string(i),
            // skeleton.inverseBindTransforms[i]);
        }
        printSkeleton("layer::read", prim.GetPath(), skeleton, ctx.debugTag);

        // Process skinning targets
        GfMatrix4d skelRootTransform = ctx.xformCache.GetLocalToWorldTransform(prim);
        GfMatrix4d inverseSkelRootTransform = skelRootTransform.GetInverse();
        const VtArray<UsdSkelSkinningQuery>& targets = binding.GetSkinningTargets();
        skeleton.targets.resize(targets.size());
        for (unsigned int i = 0; i < targets.size(); i++) {
            const UsdSkelSkinningQuery& skinningQuery = targets[i];
            const PXR_NS::UsdPrim& meshPrim = skinningQuery.GetPrim();
            if (meshPrim.IsA<PXR_NS::UsdGeomMesh>()) {
                auto [meshIndex, mesh] = ctx.usd->addMesh();
                readSkinData(ctx, mesh, skinningQuery);
                readMeshData(ctx, mesh, meshIndex, meshPrim);

                GfMatrix4d localToWorld = ctx.xformCache.GetLocalToWorldTransform(meshPrim);
                GfMatrix4d localToSkelRoot = inverseSkelRootTransform * localToWorld;
                transformMesh(mesh, localToSkelRoot);

                printMesh("layer::read", mesh, ctx.debugTag);
                skeleton.targets[i] = meshIndex;
                node.skinnedMeshes[skeletonIndex].push_back(meshIndex);
            }
        }

        // Process animation data
        int boneCount = skeleton.restTransforms.size();
        const PXR_NS::UsdSkelAnimQuery& skelAnimQuery = skelQuery.GetAnimQuery();
        std::vector<double> times;
        skelAnimQuery.GetJointTransformTimeSamples(&times);
        if (times.size()) {
            auto [animationIndex, animation] = ctx.usd->addAnimation();
            skeleton.animations.push_back(animationIndex);
            unsigned int timesCount = times.size();
            animation.times.resize(timesCount);
            animation.translations.resize(timesCount);
            animation.rotations.resize(timesCount);
            animation.scales.resize(timesCount);
            for (unsigned int i = 0; i < timesCount; i++) {
                animation.times[i] = times[i];
                animation.translations[i].resize(boneCount);
                animation.rotations[i].resize(boneCount);
                animation.scales[i].resize(boneCount);
                PXR_NS::VtMatrix4dArray transforms;
                if (!skelQuery.ComputeJointLocalTransforms(&transforms, times[i])) {
                    continue;
                }
                for (int j = 0; j < boneCount; j++) {
                    PXR_NS::UsdSkelDecomposeTransform(transforms[j],
                                                      &animation.translations[i][j],
                                                      &animation.rotations[i][j],
                                                      &animation.scales[i][j]);
                }
            }
        }
    }
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read skelRoot end %s\n",
                 ctx.debugTag.c_str(),
                 prim.GetPath().GetText());
    return true;
}

bool
readPointInstancer(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read PointInstancer { %s }\n",
                 ctx.debugTag.c_str(),
                 prim.GetName().GetText());
    auto [nodeIndex, node] = ctx.usd->addNode(parent);
    node.name = prim.GetName().GetString();
    node.path = prim.GetPath().GetString();
    readTransform(ctx, prim, node, parent);

    UsdTimeCode time = UsdTimeCode::EarliestTime();
    UsdGeomPointInstancer pointInstancer(prim);
    const UsdAttribute positionsAttr = pointInstancer.GetPositionsAttr();
    VtVec3fArray positions;
    positionsAttr.Get(&positions, time);

    VtArray<GfMatrix4d> xforms;
    pointInstancer.ComputeInstanceTransformsAtTime(&xforms, time, time);

    const UsdAttribute protoInstanceAttr = pointInstancer.GetProtoIndicesAttr();
    VtIntArray protoIndices;
    protoInstanceAttr.Get(&protoIndices, time);

    const int meshesBeforePrototypesAdded = ctx.usd->meshes.size();
    UsdPrimSiblingRange children =
      prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));
    for (const UsdPrim& p : children) {
        readPrim(ctx, p, nodeIndex);
    }

    for (unsigned int i = 0; i < protoIndices.size(); i++) {
        const int protoIndex = meshesBeforePrototypesAdded + protoIndices[i];
        const GfMatrix4d transform = xforms[i];
        if (transform != GfMatrix4d(0.0f) && transform != GfMatrix4d(1.0f)) {
            auto [nodeIndex, node] = ctx.usd->addNode(parent);
            node.name = "MeshTransform" + std::to_string(i);
            node.transform = transform;
            node.hasTransform = true;
            GfMatrix4d parentWorldTransform =
              parent != -1 ? ctx.usd->nodes[parent].worldTransform : GfMatrix4d(1);
            node.worldTransform = node.transform * parentWorldTransform;
            node.staticMeshes.push_back(protoIndex);
        } else {
            auto [nodeIndex, node] = ctx.usd->getParent(parent);
            node.staticMeshes.push_back(protoIndex);
        }
    }
    return true;
}

bool
readNgp(ReadLayerContext& ctx, const UsdPrim& primNgp, const UsdPrim& primVol, int& index)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read Ngp { %s }\n",
                 ctx.debugTag.c_str(),
                 primNgp.GetName().GetText());
    // check if the ngp has been read before
    auto ngpItr = ctx.ngps.find(primNgp.GetPath().GetAsString());
    if (ngpItr != ctx.ngps.end()) {
        index = ngpItr->second;
        return true;
    }

    index = ctx.usd->ngps.size();
    ctx.usd->ngps.push_back(NgpData());
    ctx.ngps[primNgp.GetPath().GetAsString()] = index;

    NgpData& ngpData = ctx.usd->ngps[index];
    auto getAttrIfExist = [&](const TfToken& token, auto& dst) {
        if (auto attribute = primNgp.GetAttribute(token)) {
            attribute.Get(&dst);
        }
    };

    getAttrIfExist(AdobeNgpTokens->densityMlpLayer0Weight, ngpData.densityMlpLayer0Weight);
    getAttrIfExist(AdobeNgpTokens->densityMlpLayer0Bias, ngpData.densityMlpLayer0Bias);
    getAttrIfExist(AdobeNgpTokens->densityMlpLayer1Weight, ngpData.densityMlpLayer1Weight);
    getAttrIfExist(AdobeNgpTokens->densityMlpLayer1Bias, ngpData.densityMlpLayer1Bias);
    getAttrIfExist(AdobeNgpTokens->colorMlpLayer0Weight, ngpData.colorMlpLayer0Weight);
    getAttrIfExist(AdobeNgpTokens->colorMlpLayer0Bias, ngpData.colorMlpLayer0Bias);
    getAttrIfExist(AdobeNgpTokens->colorMlpLayer1Weight, ngpData.colorMlpLayer1Weight);
    getAttrIfExist(AdobeNgpTokens->colorMlpLayer1Bias, ngpData.colorMlpLayer1Bias);
    getAttrIfExist(AdobeNgpTokens->colorMlpLayer2Weight, ngpData.colorMlpLayer2Weight);
    getAttrIfExist(AdobeNgpTokens->colorMlpLayer2Bias, ngpData.colorMlpLayer2Bias);
    getAttrIfExist(AdobeNgpTokens->densityGrid, ngpData.densityGrid);
    getAttrIfExist(AdobeNgpTokens->densityThreshold, ngpData.densityThreshold);
    getAttrIfExist(AdobeNgpTokens->distanceGrid, ngpData.distanceGrid);
    getAttrIfExist(AdobeNgpTokens->hashGrid, ngpData.hashGrid);
    getAttrIfExist(AdobeNgpTokens->hashGridResolution, ngpData.hashGridResolution);

    UsdGeomXformable xformable{ primVol };
    bool resetXFormStack = false;
    xformable.GetLocalTransformation(
      &ngpData.transform, &resetXFormStack, UsdTimeCode::EarliestTime());
    ngpData.hasTransform =
      ngpData.transform != GfMatrix4d(0.0f) && ngpData.transform != GfMatrix4d(1.0f);

    return true;
}

bool
readVolume(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read Volume { %s }\n",
                 ctx.debugTag.c_str(),
                 prim.GetName().GetText());

    // Currently, we only support NGP volume.
    if (UsdRelationship rNgp = prim.GetRelationship(AdobeNgpTokens->fieldNgp)) {
        SdfPathVector relToNgps;
        rNgp.GetTargets(&relToNgps);

        if (relToNgps.size()) {
            if (UsdPrim primNgp = ctx.stage->GetPrimAtPath(relToNgps[0])) {
                if (primNgp.IsA(AdobeNgpTokens->Ngp)) {
                    int indexNgp = -1;

                    readNgp(ctx, primNgp, prim, indexNgp);
                    if (parent >= 0 && static_cast<int>(ctx.usd->nodes.size()) > parent) {
                        ctx.usd->nodes[parent].ngp = indexNgp;
                    }
                }
            }
        }
    }

    return true;
}

bool
readImage(ReadLayerContext& ctx, const SdfAssetPath& path, int& index)
{
    const std::string& uri = path.GetAssetPath();
    std::string name = TfStringGetBeforeSuffix(TfGetBaseName(uri));
    std::string extension = TfGetExtension(uri);
    // If asset path originates from a custom resolver, fix name and extension:
    size_t pos = name.find_first_of('[');
    if (name.length() > 1 && pos != std::string::npos) {
        name = name.substr(pos + 1, name.size());
    }
    if (extension.length() > 1 && extension.back() == ']') {
        extension = extension.substr(0, extension.size() - 1);
    }
    const std::string& absPath = path.GetResolvedPath().empty()
                                   ? PXR_NS::ArGetResolver().Resolve(path.GetAssetPath())
                                   : path.GetResolvedPath();
    if (const auto& it = ctx.images.find(uri); it != ctx.images.end()) {
        index = it->second;
        TF_DEBUG_MSG(
          FILE_FORMAT_UTIL, "%s: Image (cached): %s\n", ctx.debugTag.c_str(), uri.c_str());
    } else {

        // Deduplicate name
        if (const auto& itName = ctx.imageNames.find(name); itName != ctx.imageNames.end()) {
            itName->second++;
            name = name + "_" + std::to_string(itName->second);
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "%s: Deduplicated image name: %s\n",
                         ctx.debugTag.c_str(),
                         name.c_str());
        } else {
            ctx.imageNames[name] = 1;
        }

        ArResolver& ar = ArGetResolver();
        std::shared_ptr<ArAsset> asset = ar.OpenAsset(ArResolvedPath(absPath));
        if (!asset)
            return false;
        int length = asset->GetSize();
        auto [imageIndex, image] = ctx.usd->addImage();
        image.name = name;
        image.uri = name + "." + extension;
        image.format = getFormat(extension);
        image.image.resize(length);
        memcpy(image.image.data(), asset->GetBuffer().get(), length);
        ctx.images[uri] = imageIndex;
        index = imageIndex;
        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%s: Image (new): %s\n", ctx.debugTag.c_str(), uri.c_str());
    }
    return true;
}

void
applyInputMult(Input& input, float mult)
{
    if (mult == 1.0f) {
        return;
    }

    if (input.image != -1) {
        GfVec4f s =
          input.scale.IsHolding<GfVec4f>() ? input.scale.UncheckedGet<GfVec4f>() : GfVec4f(1.0f);
        input.scale = s * mult;
    } else if (input.value.IsHolding<GfVec3f>()) {
        GfVec3f v = input.value.UncheckedGet<GfVec3f>();
        v *= mult;
        input.value = v;
    } else if (input.value.IsHolding<float>()) {
        float v = input.value.UncheckedGet<float>();
        v *= mult;
        input.value = v;
    }
}

template<typename T>
bool
getShaderInputValue(const UsdShadeShader& shader, const TfToken& name, T& value)
{
    UsdShadeInput input = shader.GetInput(name);
    if (input) {
        UsdShadeAttributeVector valueAttrs = input.GetValueProducingAttributes();
        if (!valueAttrs.empty()) {
            const UsdAttribute& attr = valueAttrs.front();
            if (UsdShadeUtils::GetType(attr.GetName()) == UsdShadeAttributeType::Input) {
                valueAttrs.front().Get(&value);
                return true;
            }
        }
    }
    return false;
}

void
readInput(ReadLayerContext& ctx, const UsdShadeShader& surface, const TfToken& name, Input& input)
{
    UsdShadeInput shadeInput = surface.GetInput(name);
    if (!shadeInput)
        return;

    if (shadeInput.HasConnectedSource()) {
        UsdShadeSourceInfoVector sources = shadeInput.GetConnectedSources();
        if (sources.empty()) {
            return;
        }
        // We do not handle multiple input connections, so we only process the first source
        UsdShadeConnectionSourceInfo source = sources[0];

        UsdShadeShader textureReadShader(source.source.GetPrim());
        TfToken infoIdToken;
        textureReadShader.GetShaderId(&infoIdToken);
        if (infoIdToken != AdobeTokens->UsdUVTexture) {
            return;
        }

        // The name of the output on the texture reader determines which channel(s) of the
        // texture we read
        input.channel = source.sourceName;

        SdfAssetPath assetPath;
        if (getShaderInputValue(textureReadShader, AdobeTokens->file, assetPath)) {
            readImage(ctx, assetPath, input.image);
        }
        getShaderInputValue(textureReadShader, AdobeTokens->wrapS, input.wrapS);
        getShaderInputValue(textureReadShader, AdobeTokens->wrapT, input.wrapT);
        getShaderInputValue(textureReadShader, AdobeTokens->scale, input.scale);
        getShaderInputValue(textureReadShader, AdobeTokens->bias, input.bias);
        getShaderInputValue(textureReadShader, AdobeTokens->sourceColorSpace, input.colorspace);

        // Currently we always use the 0th UVs
        input.uvIndex = 0;

        // Gather information about UV coordinates used
        UsdShadeInput stInput = textureReadShader.GetInput(AdobeTokens->st);
        UsdShadeSourceInfoVector stSources = stInput.GetConnectedSources();
        if (!stSources.empty()) {
            UsdShadeConnectionSourceInfo stSource = stSources[0];
            UsdShadeShader stShader(stSource.source.GetPrim());
            stShader.GetShaderId(&infoIdToken);
            if (infoIdToken == AdobeTokens->UsdTransform2d) {
                // Extract the UV transform parameters
                getShaderInputValue(stShader, AdobeTokens->rotation, input.transformRotation);
                getShaderInputValue(stShader, AdobeTokens->scale, input.transformScale);
                getShaderInputValue(stShader, AdobeTokens->translation, input.transformTranslation);

                // Get the connection for the UV reader
                UsdShadeSourceInfoVector stSources = shadeInput.GetConnectedSources();
                if (!stSources.empty()) {
                    UsdShadeConnectionSourceInfo stSource = stSources[0];
                    stShader = UsdShadeShader(stSource.source.GetPrim());
                    stShader.GetShaderId(&infoIdToken);
                }
            }
            // This is not an "else if", since we can move the stShader if we encounter a UV
            // transform
            if (infoIdToken == AdobeTokens->UsdPrimvarReader_float2) {
                TfToken texCoordPrimvar;
                getShaderInputValue(stShader, AdobeTokens->varname, texCoordPrimvar);
                if (texCoordPrimvar != AdobeTokens->st) {
                    TF_WARN("Texture reader %s is reading primvar %s. Only 'st' is supported",
                            stShader.GetPrim().GetPath().GetText(),
                            texCoordPrimvar.GetText());
                }
            }
        }
    } else {
        getShaderInputValue(surface, name, input.value);
    }
}

bool
readUsdPreviewSurfaceMaterial(ReadLayerContext& ctx,
                              Material& material,
                              const UsdShadeShader& surface)
{
    TfToken infoIdToken;
    surface.GetShaderId(&infoIdToken);
    if (infoIdToken != AdobeTokens->UsdPreviewSurface) {
        return false;
    }

    readInput(ctx, surface, AdobeTokens->useSpecularWorkflow, material.useSpecularWorkflow);
    readInput(ctx, surface, AdobeTokens->diffuseColor, material.diffuseColor);
    readInput(ctx, surface, AdobeTokens->emissiveColor, material.emissiveColor);
    readInput(ctx, surface, AdobeTokens->specularColor, material.specularColor);
    readInput(ctx, surface, AdobeTokens->normal, material.normal);
    readInput(ctx, surface, AdobeTokens->metallic, material.metallic);
    readInput(ctx, surface, AdobeTokens->roughness, material.roughness);
    readInput(ctx, surface, AdobeTokens->clearcoat, material.clearcoat);
    readInput(ctx, surface, AdobeTokens->clearcoatRoughness, material.clearcoatRoughness);
    readInput(ctx, surface, AdobeTokens->opacity, material.opacity);
    readInput(ctx, surface, AdobeTokens->opacityThreshold, material.opacityThreshold);
    readInput(ctx, surface, AdobeTokens->displacement, material.displacement);
    readInput(ctx, surface, AdobeTokens->occlusion, material.occlusion);
    readInput(ctx, surface, AdobeTokens->ior, material.ior);

    return true;
}

bool
_readClearcoatModelsTransmissionTint(const UsdShadeShader& surface)
{
    bool value = false;
    // Check for a custom attribute that carries an indicator where the clearcoat came from
    surface.GetPrim().GetAttribute(AdobeTokens->clearcoatModelsTransmissionTint).Get(&value);
    return value;
}

bool
readASMMaterial(ReadLayerContext& ctx, Material& material, const UsdShadeShader& surface)
{
    TfToken infoIdToken;
    surface.GetShaderId(&infoIdToken);
    if (infoIdToken != AdobeTokens->adobeStandardMaterial) {
        return false;
    }

    material.clearcoatModelsTransmissionTint = _readClearcoatModelsTransmissionTint(surface);

    // Note, we currently only support fixed values for emissiveIntensity and sheenOpacity
    // No texture support yet.
    float emissiveIntensity = 0.0f;
    float sheenOpacity = 0.0f;
    bool scatter = false;

    auto getConstShaderInput = [&](const TfToken& inputName, auto& var) {
        VtValue val;
        if (getShaderInputValue(surface, inputName, val)) {
            if (val.IsHolding<std::remove_reference_t<decltype(var)>>()) {
                var = val.UncheckedGet<std::remove_reference_t<decltype(var)>>();
            }
        }
    };

    getConstShaderInput(AdobeTokens->emissiveIntensity, emissiveIntensity);
    getConstShaderInput(AdobeTokens->sheenOpacity, sheenOpacity);
    getConstShaderInput(AdobeTokens->scatter, scatter);

    readInput(ctx, surface, AdobeTokens->baseColor, material.diffuseColor);
    readInput(ctx, surface, AdobeTokens->roughness, material.roughness);
    readInput(ctx, surface, AdobeTokens->metallic, material.metallic);
    readInput(ctx, surface, AdobeTokens->opacity, material.opacity);
    readInput(ctx, surface, AdobeTokens->opacityThreshold, material.opacityThreshold);
    readInput(ctx, surface, AdobeTokens->specularLevel, material.specularLevel);
    readInput(ctx, surface, AdobeTokens->specularEdgeColor, material.specularColor);
    readInput(ctx, surface, AdobeTokens->normal, material.normal);
    readInput(ctx, surface, AdobeTokens->height, material.displacement);
    readInput(ctx, surface, AdobeTokens->anisotropyLevel, material.anisotropyLevel);
    readInput(ctx, surface, AdobeTokens->anisotropyAngle, material.anisotropyAngle);
    if (emissiveIntensity > 0.0f) {
        readInput(ctx, surface, AdobeTokens->emissive, material.emissiveColor);
        applyInputMult(material.emissiveColor, emissiveIntensity);
    }
    if (sheenOpacity > 0.0f) {
        readInput(ctx, surface, AdobeTokens->sheenColor, material.sheenColor);
        // XXX sheenOpacity can't really be multiplied into the color. We currently drop this value
    }
    readInput(ctx, surface, AdobeTokens->sheenRoughness, material.sheenRoughness);
    readInput(ctx, surface, AdobeTokens->translucency, material.transmission);
    readInput(ctx, surface, AdobeTokens->IOR, material.ior);
    readInput(ctx, surface, AdobeTokens->absorptionColor, material.absorptionColor);
    readInput(ctx, surface, AdobeTokens->absorptionDistance, material.absorptionDistance);
    if (scatter) {
        readInput(ctx, surface, AdobeTokens->scatteringColor, material.scatteringColor);
        readInput(ctx, surface, AdobeTokens->scatteringDistance, material.scatteringDistance);
    }
    readInput(ctx, surface, AdobeTokens->coatOpacity, material.clearcoat);
    readInput(ctx, surface, AdobeTokens->coatColor, material.clearcoatColor);
    readInput(ctx, surface, AdobeTokens->coatRoughness, material.clearcoatRoughness);
    readInput(ctx, surface, AdobeTokens->coatIOR, material.clearcoatIor);
    readInput(ctx, surface, AdobeTokens->coatSpecularLevel, material.clearcoatSpecular);
    readInput(ctx, surface, AdobeTokens->coatNormal, material.clearcoatNormal);
    readInput(ctx, surface, AdobeTokens->ambientOcclusion, material.occlusion);
    readInput(ctx, surface, AdobeTokens->volumeThickness, material.thickness);

    return true;
}

bool
readMaterial(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    auto [materialIndex, material] = ctx.usd->addMaterial();
    ctx.materials[prim.GetPath().GetString()] = materialIndex;
    material.name = prim.GetPath().GetName();
    UsdShadeMaterial usdMaterial(prim);

    // We give preference to the Adobe ASM surface, if present, and fallback to the standard
    // UsdPreviewSurface
    UsdShadeShader surface = usdMaterial.ComputeSurfaceSource({ AdobeTokens->adobe });
    bool success = false;
    if (surface) {
        success = readASMMaterial(ctx, material, surface);
        if (!success) {
            success = readUsdPreviewSurfaceMaterial(ctx, material, surface);
        }
    } else {
        TF_WARN("No surface shader for material %s", prim.GetPath().GetText());
    }

    printMaterial("layer::read", prim.GetPath(), material, ctx.debugTag);
    return success;
}

bool
readCamera(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    auto [cameraIndex, camera] = ctx.usd->addCamera();
    Node& parentNode = getParentOrNewTransformParent(ctx, prim, parent, "CameraTransform");
    parentNode.camera = cameraIndex;

    const auto& usdCamera = UsdGeomCamera(prim);
    camera.name = prim.GetName();
    GfCamera gfCamera = usdCamera.GetCamera(0);
    camera.projection = gfCamera.GetProjection();
    camera.f = gfCamera.GetFocalLength(); // f in mm
    camera.fov = gfCamera.GetFieldOfView(GfCamera::FOVDirection::FOVVertical);
    camera.horizontalAperture = gfCamera.GetHorizontalAperture();
    camera.verticalAperture = gfCamera.GetVerticalAperture();
    GfRange1f clippingRange = gfCamera.GetClippingRange();
    camera.nearZ = clippingRange.GetMin();
    camera.farZ = clippingRange.GetMax();
    camera.camera = gfCamera;
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read camera { %s }\n",
                 ctx.debugTag.c_str(),
                 prim.GetName().GetText());
    return true;
}

bool
readPrim(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    if (!prim.IsValid()) {
        TF_DEBUG_MSG(
          FILE_FORMAT_UTIL, "%s: layer::read prim: invalid prim\n", ctx.debugTag.c_str());
        return false;
    }
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read %-10s %s\n",
                 ctx.debugTag.c_str(),
                 prim.GetTypeName().GetText(),
                 prim.GetPath().GetText());
    std::function<bool(ReadLayerContext&, UsdPrim, int)> f;
    if (prim.IsA<UsdGeomScope>())
        f = readScope;
    else if (prim.IsA<UsdGeomXform>())
        f = readNode;
    else if (prim.IsA<UsdGeomMesh>())
        f = readMesh;
    else if (prim.IsA<UsdSkelRoot>())
        f = readSkelRoot;
    else if (prim.IsA<UsdShadeMaterial>())
        f = readMaterial;
    else if (prim.IsA<UsdGeomCamera>())
        f = readCamera;
    else if (prim.IsA<UsdGeomPointInstancer>())
        f = readPointInstancer;
    else if (prim.IsA<UsdVolVolume>())
        f = readVolume;
    else
        f = readUnknown;
    return f(ctx, prim, parent);
}

void
resolveMaterialBindings(ReadLayerContext& ctx)
{
    for (unsigned int i = 0; i < ctx.usd->meshes.size(); i++) {
        std::string name = ctx.materialBindings[i];
        if (!name.empty()) {
            if (ctx.materials.find(name) == ctx.materials.end()) {
                // If the material bound hasn't been included in the export, we
                // try to include it here
                UsdPrim prim = ctx.stage->GetPrimAtPath(SdfPath(name));
                readPrim(ctx, prim, -1);
            }
            const auto& it = ctx.materials.find(name);
            if (it != ctx.materials.end()) {
                int index = it->second;
                ctx.usd->meshes[i].material = index;
                TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                             "%s: mesh[%d].material = %d: %s\n",
                             ctx.debugTag.c_str(),
                             i,
                             index,
                             name.c_str());
            } else {
                TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                             "%s: Couldn't find material: %s\n",
                             ctx.debugTag.c_str(),
                             name.c_str());
                // If the material can't be found, invalidate the material link
                ctx.usd->meshes[i].material = -1;
            }
        }
        for (unsigned int j = 0; j < ctx.subsetMaterialBindings[i].size(); j++) {
            std::string name = ctx.subsetMaterialBindings[i][j];
            if (!name.empty()) {
                if (ctx.materials.find(name) == ctx.materials.end()) {
                    // If the material bound hasn't been included in the export, we
                    // try to include it here
                    UsdPrim prim = ctx.stage->GetPrimAtPath(SdfPath(name));
                    readPrim(ctx, prim, -1);
                }
                const auto& it = ctx.materials.find(name);
                if (it != ctx.materials.end()) {
                    int index = it->second;
                    ctx.usd->meshes[i].subsets[j].material = index;
                    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                                 "%s: mesh[%d].subset[%d].material = %d: %s\n",
                                 ctx.debugTag.c_str(),
                                 i,
                                 j,
                                 index,
                                 name.c_str());
                } else {
                    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                                 "%s: Couldn't find material: %s\n",
                                 ctx.debugTag.c_str(),
                                 name.c_str());
                    // If the material can't be found, invalidate the material link
                    ctx.usd->meshes[i].subsets[j].material = -1;
                }
            }
        }
    }
}

bool
readLayer(const ReadLayerOptions& options,
          const SdfLayer& constLayer,
          UsdData& usd,
          const std::string& debugTag)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%s: layer::read Start\n", debugTag.c_str());
    auto layer = PXR_NS::SdfCreateNonConstHandle<PXR_NS::SdfLayer>(&constLayer);
    auto stage = UsdStage::Open(layer);
    ReadLayerContext ctx;
    ctx.stage = stage;
    ctx.usd = &usd;
    ctx.options = &options;
    ctx.debugTag = debugTag;
    usd.upAxis = UsdGeomGetStageUpAxis(ctx.stage);
    if (UsdGeomStageHasAuthoredMetersPerUnit(ctx.stage)) {
        usd.metersPerUnit = UsdGeomGetStageMetersPerUnit(ctx.stage);
    }
    usd.metadata = stage->GetRootLayer()->GetCustomLayerData();
    usd.timeCodesPerSecond = stage->GetTimeCodesPerSecond();

    UsdPrim defaultPrim;
    if (ctx.stage->HasDefaultPrim()) {
        defaultPrim = ctx.stage->GetDefaultPrim();
        if (!defaultPrim.IsValid()) {
            TF_WARN("Stage has default prim %s, which is not valid",
                    ctx.stage->GetRootLayer()->GetDefaultPrim().GetText());
        }
    }
    if (defaultPrim) {
        readPrim(ctx, defaultPrim, -1);
    } else {
        for (const UsdPrim& rootPrim : ctx.stage->GetPseudoRoot().GetChildren()) {
            readPrim(ctx, rootPrim, -1);
        }
    }
    resolveMaterialBindings(ctx);
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%s: layer::read End\n", ctx.debugTag.c_str());

    // These checks are only active when the the FILE_FORMAT_UTIL TfDebug flag is on
    checkAndPrintMeshIssues(usd);

    return true;
}

}
