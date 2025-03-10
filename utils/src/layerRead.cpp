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
#include <fileformatutils/layerRead.h>

#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>
#include <fileformatutils/geometry.h>
#include <fileformatutils/images.h>
#include <fileformatutils/layerWriteShared.h>
#include <fileformatutils/usdData.h>

#include <pxr/base/tf/pathUtils.h>
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
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
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

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
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

/**
 * Check whether the given prim is explicitly marked invisible. For this to be the case, it must:
 * 1. Be a UsdGeomImageable
 * 2. Have a visibility attribute
 * 3. The visibility attribute must be set to UsdGeomTokens->invisible
 *
 * If not all of these are the case, false is returned.
 *
 * Note that false doesn't mean that the prim is visible, it means that the prim is not explicitly
 * marked invisible. It may still inherit invisibility from a parent.
 */
bool
isMarkedInvisible(ReadLayerContext& ctx, const UsdPrim& prim)
{
    UsdGeomImageable imageable(prim);
    if (imageable && imageable.GetVisibilityAttr().HasValue()) {
        TfToken visibility;
        imageable.GetVisibilityAttr().Get<TfToken>(&visibility);
        return visibility == UsdGeomTokens->invisible;
        // visibility will otherwise be UsdGeomTokens->inherited
    }
    return false;
}

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
        node.displayName = prim.GetDisplayName();
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
    node.displayName = prim.GetDisplayName();
    node.path = prim.GetPath().GetString();
    node.markedInvisible = isMarkedInvisible(ctx, prim);
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

    UsdPrimSiblingRange children =
      prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));

    bool skipAddingNode = false;
    if (prim.GetPrimTypeInfo() == UsdPrimTypeInfo::GetEmptyPrimType()) {
        bool allChildrenAreMaterials = true;
        for (const UsdPrim& p : children) {
            if (!p.IsA<UsdShadeMaterial>()) {
                allChildrenAreMaterials = false;
                break;
            }
        }

        // If all children are materials, skip adding this node.
        // This node does not need to be added to the node hierarchy, as materials don't live within
        // the node hierarchy.
        skipAddingNode = allChildrenAreMaterials;
    }

    int parentIndexForChildren = parent;
    if (!skipAddingNode) {
        auto [nodeIndex, node] = ctx.usd->addNode(parent);
        parentIndexForChildren = nodeIndex;

        node.name = prim.GetName().GetString();
        node.displayName = prim.GetDisplayName();
        node.path = prim.GetPath().GetString();
        node.markedInvisible = isMarkedInvisible(ctx, prim);
        readTransform(ctx, prim, node, parent);
    }
    for (const UsdPrim& p : children) {
        readPrim(ctx, p, parentIndexForChildren);
    }
    return true;
}

void
readXformInternal(ReadLayerContext& ctx, Node& node, const UsdPrim& prim, int parent)
{
    node.name = prim.GetName().GetString();
    node.displayName = prim.GetDisplayName();
    node.path = prim.GetPath().GetString();
    node.markedInvisible = isMarkedInvisible(ctx, prim);

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

    auto ensureNodeAnimation = [&ctx](Node& node) -> NodeAnimation& {
        ctx.usd->hasAnimations = true;
        node.animations.resize(1);
        return node.animations.front();
    };

    if (hasTranslation) {
        std::vector<double> times;
        translationOp.GetTimeSamples(&times);
        if (!times.empty()) {
            NodeAnimation& nodeAnimation = ensureNodeAnimation(node);
            nodeAnimation.translations.times.resize(times.size());
            nodeAnimation.translations.values.resize(times.size());
            for (unsigned int i = 0; i < times.size(); i++) {
                nodeAnimation.translations.times[i] = times[i];

                // Translation is stored as a vector of doubles. To extract it properly, we must
                // fill a vec3d before converting it to a vec3f, as node.translations stores
                GfVec3d vec3d;
                translationOp.Get(&vec3d, nodeAnimation.translations.times[i]);
                nodeAnimation.translations.values[i] = GfVec3f(vec3d);
            }
        }
    }
    if (hasRotation) {
        std::vector<double> times;
        rotationOp.GetTimeSamples(&times);
        if (!times.empty()) {
            NodeAnimation& nodeAnimation = ensureNodeAnimation(node);
            nodeAnimation.rotations.times.resize(times.size());
            nodeAnimation.rotations.values.resize(times.size());
            for (unsigned int i = 0; i < times.size(); i++) {
                nodeAnimation.rotations.times[i] = times[i];
                rotationOp.Get(&nodeAnimation.rotations.values[i],
                               nodeAnimation.rotations.times[i]);
            }
        }
    }
    if (hasScale) {
        std::vector<double> times;
        scaleOp.GetTimeSamples(&times);
        if (!times.empty()) {
            NodeAnimation& nodeAnimation = ensureNodeAnimation(node);
            nodeAnimation.scales.times.resize(times.size());
            nodeAnimation.scales.values.resize(times.size());
            for (unsigned int i = 0; i < times.size(); i++) {
                nodeAnimation.scales.times[i] = times[i];
                scaleOp.Get(&nodeAnimation.scales.values[i], nodeAnimation.scales.times[i]);
            }
        }
    }
}

bool
readXform(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    int nodeIndex = -1;
    // Collapse this node if it is the default prim, and has an indentity transform.
    // Often the default prim is present as a parent for all the root nodes of the scene.
    // To preserve those root nodes during export, it is helpful to avoid creating a UsdData node
    // for the default prim.
    if (parent == -1 && ctx.stage->GetDefaultPrim() == prim) {
        Node tempNode;
        readXformInternal(ctx, tempNode, prim, parent);

        if (tempNode.hasTransform || !tempNode.animations.empty()) {
            // Only add the node if it has a transform
            std::pair<int, Node&> pair = ctx.usd->addNode(parent);

            nodeIndex = pair.first;
            pair.second = std::move(tempNode);
        }
    } else {
        std::pair<int, Node&> pair = ctx.usd->addNode(parent);

        nodeIndex = pair.first;
        readXformInternal(ctx, pair.second, prim, parent);
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
    std::string str = name.GetString();
    UsdGeomPrimvar pv = api.GetPrimvar(name);
    if (pv.IsDefined()) {
        pv.Get(&primvar.values, 0);
        pv.GetIndices(&primvar.indices, 0);
        primvar.interpolation = pv.GetInterpolation();
        return true;
    }
    return false;
}

TfTokenVector
findTextureCoordinatePrimvars(const UsdGeomPrimvarsAPI& api)
{
    TfTokenVector texCoordPrimvarNames;
    for (const UsdGeomPrimvar& primvar : api.GetPrimvarsWithAuthoredValues()) {
        SdfValueTypeName typeName = primvar.GetTypeName();
        // TODO add support for TexCoord2hArray/Half2Array
        if (typeName == SdfValueTypeNames->TexCoord2fArray ||
            typeName == SdfValueTypeNames->Float2Array) {
            TfToken primvarName = primvar.GetPrimvarName();
            texCoordPrimvarNames.push_back(primvarName);
        }
    }
    if (texCoordPrimvarNames.size() > 1) {
        // If there is more than one primvar name (token), we need to return a sorted list of
        // tokens. The sort is based on first separating the non-numeric part and numeric parts of
        // the token string and then using the parts for comparison. The  list to tokens is then
        // updated based on the sort.
        struct Item
        {
            TfToken token;
            std::string prefix;
            int number;
        };
        std::vector<Item> sortables;
        sortables.reserve(texCoordPrimvarNames.size());
        for (auto token : texCoordPrimvarNames) {
            std::string str = token.GetString();
            auto index = str.find_first_of("0123456789");
            if (index == std::string::npos) {
                // We want to ensure that if the token "st" appears in the list, it will always be
                // placed at the front of the sorted list. This is easily done by using an empty
                // string as a primary key comparitor for the token.
                if (str == "st")
                    sortables.push_back(Item{ token, "", -1 });
                else
                    sortables.push_back(Item{ token, str, -1 });
            } else {
                int val = parseIntEnding(str.substr(index));
                if (val < 0)
                    sortables.push_back(Item{ token, str, -1 });
                else
                    sortables.push_back(Item{ token, str.substr(0, index), val });
            }
        }
        std::sort(sortables.begin(), sortables.end(), [](Item& a, Item& b) {
            return a.prefix < b.prefix || (a.prefix == b.prefix && a.number < b.number);
        });

        for (size_t i = 0; i < texCoordPrimvarNames.size(); ++i) {
            texCoordPrimvarNames[i] = sortables[i].token;
        }
    }

    return texCoordPrimvarNames;
}

bool
readMeshOrPointsData(ReadLayerContext& ctx, Mesh& mesh, int meshIndex, const UsdPrim& prim)
{
    ctx.materialBindings.push_back("");
    ctx.subsetMaterialBindings.push_back({});

    mesh.name = prim.GetName();
    mesh.displayName = prim.GetDisplayName();
    mesh.markedInvisible = isMarkedInvisible(ctx, prim);
    UsdGeomPrimvarsAPI primvarsAPI(prim);

    if (prim.IsA<UsdGeomMesh>()) {
        UsdGeomMesh usdMesh(prim);
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
    } else if (prim.IsA<UsdGeomPoints>()) {
        UsdGeomPoints usdPoints(prim);
        usdPoints.GetPointsAttr().Get(&mesh.points, 0);
        usdPoints.GetWidthsAttr().Get(&mesh.pointWidths, 0);
        UsdAttribute normalsAttr = usdPoints.GetNormalsAttr();
        if (readPrimvar(primvarsAPI, UsdGeomTokens->normals, mesh.normals)) {
        } else if (normalsAttr.IsAuthored()) {
            normalsAttr.Get(&mesh.normals.values, 0);
            mesh.normals.interpolation = usdPoints.GetNormalsInterpolation();
        }
    } else {
        TF_CODING_ERROR("Shouldn't reach here. Prim %s is neither a mesh nor points.",
                        mesh.name.c_str());
        return false;
    }

    TfTokenVector uvTokens = findTextureCoordinatePrimvars(primvarsAPI);

    if (uvTokens.empty()) {
        auto path = prim.GetPath();
        if (path.IsEmpty()) {
            TF_WARN("No texture coordinates for mesh with an empty path");
        } else {
            const char* pathText = path.GetText();
            if (pathText == nullptr) {
                TF_WARN("No texture coordinates for mesh with a null path text");
            } else {
                TF_WARN("No texture coordinates for mesh %s", pathText);
            }
        }
    } else {
        readPrimvar(primvarsAPI, uvTokens[0], mesh.uvs);
        for (size_t i = 1; i < uvTokens.size(); ++i) {
            mesh.extraUVSets.push_back(Primvar<GfVec2f>());
            readPrimvar(primvarsAPI, uvTokens[i], mesh.extraUVSets[i - 1]);
        }
    }

    Primvar<GfVec3f> displayColor;
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

    if (prim.IsA<UsdGeomMesh>()) {
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
                    ctx.subsetMaterialBindings[meshIndex][subsetIndex] =
                      material.GetPath().GetString();
                }
            }
        }

        if (ctx.options->triangulate) {
            if (!triangulateMesh(mesh)) {
                return false;
            }
            // Separate flag for this?
            forceVertexInterpolation(mesh);
        }

        // After reading the geometry subsets and potentially triangulating and expanding the mesh
        // to force vertex interpolation we pre-compute a set of face vertex indices for each subset
        // that index into the points buffer of the main mesh
        for (Subset& subset : mesh.subsets) {
            // Compute the face vertex indices of the subset based on the face indices that define
            // the subset
            computeFaceVertexIndicesForSubset(
              mesh.faces, mesh.indices, subset.faces, subset.indices);
        }
    } else if (prim.IsA<UsdGeomPoints>()) {
        mesh.asPoints = true;

        // Check if the point cloud is a Gaussian splat, it is a Gaussian splat as long as it has
        // all the basic tokens.
        mesh.asGsplats = true;
        for (const TfToken& gsToken : AdobeGsplatBaseTokens->allTokens) {
            if (!primvarsAPI.HasPrimvar(gsToken)) {
                mesh.asGsplats = false;
                break;
            }
        }

        if (mesh.asGsplats) {
            for (const TfToken& gsToken : AdobeGsplatBaseTokens->allTokens) {
                if (gsToken == AdobeGsplatBaseTokens->rot) {
                    // Rotation token: 'rot'.
                    readPrimvar(primvarsAPI, gsToken, mesh.pointRotations);
                    if (!mesh.pointRotations.values.size()) {
                        TF_WARN("Invalid values for rot in Gaussian splat %s",
                                prim.GetPath().GetText());
                        mesh.asGsplats = false;
                        break;
                    }
                } else if (gsToken.GetString()[0] == 'w') {
                    // Width-related tokens: 'widths', 'widths1', and 'widths2'.
                    Primvar<float> extraWidths;
                    readPrimvar(primvarsAPI, gsToken, extraWidths);
                    if (extraWidths.values.size()) {
                        auto [extraPointWidthSetIndex, extraPointWidthSet] =
                          ctx.usd->addExtraPointWidthSet(meshIndex);
                        extraPointWidthSet.indices = extraWidths.indices;
                        extraPointWidthSet.values = extraWidths.values;
                        extraPointWidthSet.interpolation = extraWidths.interpolation;
                    } else {
                        TF_WARN("Invalid values for %s in Gaussian splat %s",
                                gsToken.GetText(),
                                prim.GetPath().GetText());
                        mesh.asGsplats = false;
                        break;
                    }
                }
            }
            for (const TfToken& gsToken : AdobeGsplatSHTokens->allTokens) {
                // SH-related tokens: fRest0 -- fRest44.
                Primvar<float> shCoeffs;
                readPrimvar(primvarsAPI, gsToken, shCoeffs);
                if (shCoeffs.values.size()) {
                    auto [pointSHCoeffSetIndex, pointSHCoeffSet] =
                      ctx.usd->addPointSHCoeffSet(meshIndex);
                    pointSHCoeffSet.indices = shCoeffs.indices;
                    pointSHCoeffSet.values = shCoeffs.values;
                    pointSHCoeffSet.interpolation = shCoeffs.interpolation;
                }
            }
        }
    }

    return true;
}

bool
readSkinData(ReadLayerContext& ctx, Mesh& mesh, const UsdSkelSkinningQuery& skinningQuery)
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
readMeshOrPoints(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    std::string path = prim.GetPrimInPrototype().GetPath().GetString();
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

    if (!readMeshOrPointsData(ctx, mesh, meshIndex, prim)) {
        return false;
    }

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
// XXX Because we aren't visiting children manually, this does mean we might miss other nodes added
// to a SkelRoot, if such nodes exist. If no tools output such nodes within SkelRots, this may be
// acceptable.
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
    node.displayName = prim.GetDisplayName();
    node.path = prim.GetPath().GetString();
    node.markedInvisible = isMarkedInvisible(ctx, prim);

    UsdSkelCache skelCache; // to hoist later to see performance improvement
    UsdSkelRoot skelRoot(prim);
    skelCache.Populate(skelRoot, UsdTraverseInstanceProxies());
    std::vector<UsdSkelBinding> bindings;
    skelCache.ComputeSkelBindings(skelRoot, &bindings, UsdTraverseInstanceProxies());
    for (const UsdSkelBinding& binding : bindings) {

        // Process skeleton data
        auto [skeletonIndex, skeleton] = ctx.usd->addSkeleton();
        const UsdSkelSkeleton& skelSkeleton = binding.GetSkeleton();
        const UsdSkelSkeletonQuery& skelQuery = skelCache.GetSkelQuery(skelSkeleton);
        const UsdSkelTopology& topology = skelQuery.GetTopology();
        skeleton.parent = parent;
        skeleton.joints = skelQuery.GetJointOrder();
        skelSkeleton.GetRestTransformsAttr().Get(&skeleton.restTransforms, 0);
        skelSkeleton.GetBindTransformsAttr().Get(&skeleton.bindTransforms, 0);
        skeleton.jointParents.resize(skeleton.joints.size());
        skeleton.inverseBindTransforms.resize(skeleton.joints.size());
        for (unsigned int i = 0; i < skeleton.joints.size(); i++) {
            TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                         "%s: layer::read %-10s %s\n",
                         ctx.debugTag.c_str(),
                         "SkelJoint",
                         skeleton.joints[i].GetText());
            skeleton.jointParents[i] = topology.GetParent(i);
            skeleton.inverseBindTransforms[i] = skeleton.bindTransforms[i].GetInverse();
            // printMatrix("Bind matrix " + std::to_string(i), skeleton.bindTransforms[i]);
            // printMatrix("Inverse bind matrix " + std::to_string(i),
            // skeleton.inverseBindTransforms[i]);
        }

        const UsdPrim& skeletonPrim = skelSkeleton.GetPrim();
        skeleton.name = skeletonPrim.GetName().GetString();
        skeleton.displayName = skeletonPrim.GetDisplayName();
        printSkeleton("layer::read", skeletonPrim.GetPath(), skeleton, ctx.debugTag);

        // Process skinning targets
        const VtArray<UsdSkelSkinningQuery>& targets = binding.GetSkinningTargets();
        skeleton.meshSkinningTargets.resize(targets.size());
        for (unsigned int i = 0; i < targets.size(); i++) {
            const UsdSkelSkinningQuery& skinningQuery = targets[i];
            const UsdPrim& meshPrim = skinningQuery.GetPrim();
            if (meshPrim.IsA<UsdGeomMesh>()) {
                auto [meshIndex, mesh] = ctx.usd->addMesh();
                readSkinData(ctx, mesh, skinningQuery);
                readMeshOrPointsData(ctx, mesh, meshIndex, meshPrim);

                printMesh("layer::read", mesh, ctx.debugTag);
                skeleton.meshSkinningTargets[i] = meshIndex;

                // Add skeleton/mesh to the node.skinnedMeshes vector as well.
                // This is redundant info with skeleton.meshSkinningTargets & skeleton.parent
                // but it helps the exporters to be able to have this info present on the node.
                int searchIndex = skeletonIndex;
                auto it = std::find_if(node.skinnedMeshes.begin(),
                                       node.skinnedMeshes.end(),
                                       [searchIndex](const auto& skinnedMesh) {
                                           return skinnedMesh.first == searchIndex;
                                       });
                if (it == node.skinnedMeshes.end()) {
                    std::pair<int, std::vector<int>> skinnedMesh;
                    skinnedMesh.first = skeletonIndex;
                    skinnedMesh.second.push_back(meshIndex);
                    node.skinnedMeshes.push_back(std::move(skinnedMesh));
                } else {
                    it->second.push_back(meshIndex);
                }
            }
        }

        // Process animation data
        int boneCount = skeleton.restTransforms.size();
        const UsdSkelAnimQuery& skelAnimQuery = skelQuery.GetAnimQuery();
        if (skelAnimQuery.IsValid()) {
            std::vector<double> times;
            skelAnimQuery.GetJointTransformTimeSamples(&times);
            if (times.size()) {
                ctx.usd->hasAnimations = true;

                // The SkelAnimQuery may return joints not in the skeleton. Compute the intersection
                // of this joint array and the skeleton's joint array
                // Also, keep track of which animated joints are present in the skeleton, so that we
                // can identify which animated transforms are relevant.
                VtTokenArray allAnimatedJoints = skelAnimQuery.GetJointOrder();
                std::vector<bool> animatedJointPresent(allAnimatedJoints.size());
                int allAnimatedJointIndex = -1;
                for (const TfToken& joint : allAnimatedJoints) {
                    allAnimatedJointIndex++;
                    if (std::find(skeleton.joints.begin(), skeleton.joints.end(), joint) !=
                        skeleton.joints.end()) {
                        animatedJointPresent[allAnimatedJointIndex] = true;
                        skeleton.animatedJoints.push_back(joint);
                    }
                }

                skeleton.skeletonAnimations.resize(1);
                SkeletonAnimation& animation = skeleton.skeletonAnimations.front();
                unsigned int timesCount = times.size();
                animation.times.resize(timesCount);
                animation.translations.resize(timesCount);
                animation.rotations.resize(timesCount);
                animation.scales.resize(timesCount);
                for (unsigned int i = 0; i < timesCount; i++) {
                    VtMatrix4dArray transforms;
                    if (!skelAnimQuery.ComputeJointLocalTransforms(&transforms, times[i])) {
                        continue;
                    }

                    animation.times[i] = times[i];
                    animation.translations[i].reserve(skeleton.animatedJoints.size());
                    animation.rotations[i].reserve(skeleton.animatedJoints.size());
                    animation.scales[i].reserve(skeleton.animatedJoints.size());

                    // Add all transforms to the SkeletonAnimation as long as the transforms are
                    // for a joint referred to by skeleton.animatedJoints
                    for (int j = 0; j < transforms.size(); j++) {
                        if (animatedJointPresent[j]) {
                            GfVec3f translation;
                            GfQuatf rotation;
                            GfVec3h scale;
                            UsdSkelDecomposeTransform(
                              transforms[j], &translation, &rotation, &scale);

                            animation.translations[i].push_back(translation);
                            animation.rotations[i].push_back(rotation);
                            animation.scales[i].push_back(scale);
                        }
                    }
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
    node.displayName = prim.GetDisplayName();
    node.path = prim.GetPath().GetString();
    node.markedInvisible = isMarkedInvisible(ctx, prim);
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

    // TODO: read volume visibility

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

// Populates the absolute path, base name, and sanitized extension for an SBSAR asset by resolving
// the absolute path from the provided URI.
void
populatePathPartsFromAssetPath(const SdfAssetPath& path,
                               std::string& resolvedAssetPath,
                               std::string& name,
                               std::string& extension)
{
    // Make sure we have a resolved path, either coming from SdfAssetPath value or by running it
    // throught the resolver.
    resolvedAssetPath = path.GetResolvedPath().empty()
                          ? ArGetResolver().Resolve(path.GetAssetPath())
                          : path.GetResolvedPath();
    // This will extract the inner most path to the asset:
    // path/to/package.usdz[path/to/image.png] -> path/to/image.png
    std::string innerAssetPath = getLayerFilePath(resolvedAssetPath);
    // This helper function will detect "funky" paths, like those to SBSAR images and convert them
    // to good usable file paths
    std::string filePath = extractFilePathFromAssetPath(innerAssetPath);
    // Strip the path part since we only want the filename and the extension
    std::string baseName = TfGetBaseName(filePath);
    name = TfStringGetBeforeSuffix(baseName);
    extension = TfGetExtension(baseName);
}

bool
readImage(ReadLayerContext& ctx, const SdfAssetPath& assetPath, int& index)
{
    std::string resolvedAssetPath, name, extension;
    populatePathPartsFromAssetPath(assetPath, resolvedAssetPath, name, extension);

    // Check in the cache if we've processed this image before
    if (const auto& it = ctx.images.find(resolvedAssetPath); it != ctx.images.end()) {
        index = it->second;
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Image (cached): %s\n",
                     ctx.debugTag.c_str(),
                     resolvedAssetPath.c_str());
        return true;
    }

    // The image is new. Make sure we don't get name collisions in the short name
    if (const auto& itName = ctx.imageNames.find(name); itName != ctx.imageNames.end()) {
        itName->second++;
        name += "_" + std::to_string(itName->second);
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Deduplicated image name: %s\n",
                     ctx.debugTag.c_str(),
                     name.c_str());
    } else {
        ctx.imageNames[name] = 1;
    }

    auto [imageIndex, image] = ctx.usd->addImage();
    if (extension == "sbsarimage") {
        // SBSAR images are a special cases where the data is stored raw and must be transcoded to a
        // different image in memory
        extension = getSbsarImageExtension(resolvedAssetPath);
        image.uri = name + "." + extension;
        transcodeImageAssetToMemory(resolvedAssetPath, image.uri, image.image);
    } else {
        auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedAssetPath));
        if (!asset) {
            TF_WARN(
              "%s: Unable to open asset: %s\n", ctx.debugTag.c_str(), resolvedAssetPath.c_str());
            return false;
        }
        image.uri = name + "." + extension;
        image.image.resize(asset->GetSize());
        memcpy(image.image.data(), asset->GetBuffer().get(), asset->GetSize());
    }

    image.name = name;
    image.format = getFormat(extension);
    ctx.images[resolvedAssetPath] = imageIndex;
    index = imageIndex;

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: Image (new): index: %d uri: %s\n",
                 ctx.debugTag.c_str(),
                 imageIndex,
                 resolvedAssetPath.c_str());

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

// Fetches the first value-producing attribute connected to a given shader input.
// If 'expectShader' is true, verify that the connected source is a shader and that the connection
// exists. Returns true and sets outAttribute if a suitable attribute is found.
bool
fetchPrimaryConnectedAttribute(const UsdShadeInput& shadeInput,
                               UsdAttribute& outAttribute,
                               bool expectShader)
{
    if (expectShader) {
        if (!shadeInput.HasConnectedSource()) {
            TF_WARN("Input %s has no connected source.", shadeInput.GetFullName().GetText());
            return false;
        }
    }
    UsdShadeAttributeVector attrs = shadeInput.GetValueProducingAttributes();
    if (attrs.empty()) {
        return false;
    }
    if (attrs.size() > 1) {
        TF_WARN("Input %s is connected to multiple producing attributes, only the first will be "
                "processed.",
                shadeInput.GetFullName().GetText());
    }
    outAttribute = attrs[0];
    if (expectShader) {
        UsdShadeAttributeType attrType = UsdShadeUtils::GetType(outAttribute.GetName());
        if (attrType == UsdShadeAttributeType::Input) {
            TF_WARN("Input %s is connected to an attribute that is not a shader.",
                    shadeInput.GetFullName().GetText());
            return false;
        }
    }
    return true;
}

// Handle texture-related shader inputs such as file paths and wrapping modes.
void
handleTextureShader(ReadLayerContext& ctx, const UsdShadeShader& shader, Input& input)
{
    SdfAssetPath assetPath;
    if (getShaderInputValue(shader, AdobeTokens->file, assetPath)) {
        readImage(ctx, assetPath, input.image);
    }
    getShaderInputValue(shader, AdobeTokens->wrapS, input.wrapS);
    getShaderInputValue(shader, AdobeTokens->wrapT, input.wrapT);
    getShaderInputValue(shader, AdobeTokens->minFilter, input.minFilter);
    getShaderInputValue(shader, AdobeTokens->magFilter, input.magFilter);
    getShaderInputValue(shader, AdobeTokens->scale, input.scale);
    getShaderInputValue(shader, AdobeTokens->bias, input.bias);
    getShaderInputValue(shader, AdobeTokens->sourceColorSpace, input.colorspace);

    // Default to 0th UVs unless overridden in handlePrimvarReader
    input.uvIndex = 0;
}

UsdShadeShader
handleTransformShader(ReadLayerContext& ctx, const UsdShadeShader& shader, Input& input)
{

    UsdShadeShader nextShader;
    getShaderInputValue(shader, AdobeTokens->rotation, input.transformRotation);
    getShaderInputValue(shader, AdobeTokens->scale, input.transformScale);
    getShaderInputValue(shader, AdobeTokens->translation, input.transformTranslation);

    UsdShadeInput stInputCoordReader = shader.GetInput(AdobeTokens->in);
    UsdAttribute stSourcesInner;
    if (fetchPrimaryConnectedAttribute(stInputCoordReader, stSourcesInner, true)) {
        nextShader = UsdShadeShader(stSourcesInner.GetPrim());
    }
    return nextShader;
}

void
handlePrimvarReader(ReadLayerContext& ctx, const UsdShadeShader& shader, Input& input)
{
    TfToken texCoordPrimvar;
    std::string texCoordPrimvarStr;
    getShaderInputValue(shader, AdobeTokens->varname, texCoordPrimvarStr);

    // Supports both string and token type values for the varname
    // string is the correct type, but token was added to support slightly
    // incorrect assets.
    if (!texCoordPrimvarStr.empty()) {
        texCoordPrimvar = TfToken(texCoordPrimvarStr);
    } else {
        getShaderInputValue(shader, AdobeTokens->varname, texCoordPrimvar);
    }
    int uvIndex = getSTPrimvarTokenIndex(texCoordPrimvar);
    if (uvIndex >= 0) {
        input.uvIndex = uvIndex;
    } else {
        TF_WARN("Texture reader %s is reading primvar %s. Only 'st' or 'st1'..'stN' is supported",
                shader.GetPrim().GetPath().GetText(),
                texCoordPrimvar.GetText());
    }
}

void
readInput(ReadLayerContext& ctx, const UsdShadeShader& surface, const TfToken& name, Input& input)
{
    UsdShadeInput shadeInput = surface.GetInput(name);
    if (!shadeInput) {
        return;
    }

    UsdAttribute attr;
    if (fetchPrimaryConnectedAttribute(shadeInput, attr, false)) {
        UsdShadeSourceInfoVector sources = shadeInput.GetConnectedSources();

        // Attempt to retrieve the constant value from the attribute.
        auto [shadingAttrName, attrType] = UsdShadeUtils::GetBaseNameAndType(attr.GetName());
        if (attrType == UsdShadeAttributeType::Input) {
            if (!attr.Get(&input.value)) {
                TF_WARN("Failed to get constant value for input %s", name.GetText());
                return;
            }
        } else {
            // Process the shader connected to this attribute
            UsdShadeShader connectedShader(attr.GetPrim());
            TfToken shaderId;
            connectedShader.GetShaderId(&shaderId);

            if (shaderId == AdobeTokens->UsdUVTexture) {
                handleTextureShader(ctx, connectedShader, input);

                UsdShadeInput stInput = connectedShader.GetInput(AdobeTokens->st);

                // The name of the output on the texture reader determines which channel(s) of the
                // texture we read.
                input.channel = shadingAttrName;

                // Process the connected source of the 'st' input.
                if (fetchPrimaryConnectedAttribute(stInput, attr, true)) {
                    VtValue srcValue;
                    if (attr.Get(&srcValue)) {
                        TF_WARN(
                          "Texture read shader does not support a fixed UV value for input %s",
                          name.GetText());
                    } else {
                        // Handle the shader connected to the UV coordinate.
                        UsdShadeShader stShader(attr.GetPrim());
                        stShader.GetShaderId(&shaderId);

                        if (shaderId == AdobeTokens->UsdTransform2d) {
                            UsdShadeShader nextShader = handleTransformShader(ctx, stShader, input);
                            if (nextShader) {
                                stShader = nextShader;
                                stShader.GetShaderId(&shaderId);
                            }
                        }

                        // This is not an "else if", since we can move the stShader
                        // if we encounter a UV transform.
                        if (shaderId == AdobeTokens->UsdPrimvarReader_float2) {
                            handlePrimvarReader(ctx, stShader, input);
                        } else {
                            TF_WARN("Unsupported shader type %s for UV input %s",
                                    shaderId.GetText(),
                                    name.GetText());
                        }
                    }
                } else {
                    TF_WARN("Failed to fetch connected attribute for UV input %s", name.GetText());
                }
            } else {
                TF_WARN(
                  "Unsupported shader type %s for input %s", shaderId.GetText(), name.GetText());
            }
        }
    } else {
        // If no connections were found, get the shader's input value directly
        if (!getShaderInputValue(surface, name, input.value)) {
            TF_WARN("Failed to get input value for %s", name.GetText());
        }
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
_readUnlit(const UsdShadeShader& surface)
{
    bool value = false;
    // Check for a custom attribute that carries an indicator where the clearcoat came from
    surface.GetPrim().GetAttribute(AdobeTokens->unlit).Get(&value);
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
    material.isUnlit = _readUnlit(surface);

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
    readInput(ctx, surface, AdobeTokens->normalScale, material.normalScale);
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
    readInput(ctx, surface, AdobeTokens->volumeThickness, material.volumeThickness);

    return true;
}

bool
readMaterial(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    auto [materialIndex, material] = ctx.usd->addMaterial();
    ctx.materials[prim.GetPath().GetString()] = materialIndex;
    material.name = prim.GetPath().GetName();
    material.displayName = prim.GetDisplayName();
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
    camera.displayName = prim.GetDisplayName();
    camera.markedInvisible = isMarkedInvisible(ctx, prim);
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
    camera.fStop = gfCamera.GetFStop();
    camera.focusDistance = gfCamera.GetFocusDistance();
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: layer::read camera { %s }\n",
                 ctx.debugTag.c_str(),
                 prim.GetName().GetText());
    return true;
}

/**
 * Reads the attributes of a light that are common to all light types. This includes color and
 * intensity. Note that the resulting intensity value will be a combination of the USD light's
 * intensity and exposure values.
 *
 * @tparam T The type of the USD light. This should be a subclass of either
 * UsdLuxBoundableLightBase or UsdLuxNonboundableLightBase
 * @param usdLight The USD light to read from
 * @param light The Light object to write to. This object will be modified by this function
 */
template<typename T>
void
readCommonLightAttributes(const T& usdLight, Light& light)
{
    // Color
    if (!usdLight.GetColorAttr().Get(&light.color)) {
        TF_WARN("When reading USD layers, failed to read color of light %s", light.name.c_str());
    }

    // Intensity and exposure
    bool hasLightValue = false;
    float exposure = 0; // USD default exposure is 0
    if (usdLight.GetIntensityAttr().Get(&light.intensity)) {
        hasLightValue = true;
    } else {
        light.intensity = 1; // USD default intensity is 1
    }
    if (usdLight.GetExposureAttr().Get(&exposure)) {
        hasLightValue = true;
        light.intensity *= std::exp2(exposure);
    }
    if (!hasLightValue) {
        TF_WARN("When reading USD layers, failed to read either intensity or exposure of light %s",
                light.name.c_str());
    }
}

bool
readLight(ReadLayerContext& ctx, const UsdPrim& prim, int parent)
{
    auto [lightIndex, light] = ctx.usd->addLight();
    Node& parentNode = getParentOrNewTransformParent(ctx, prim, parent, "LightTransform");
    parentNode.light = lightIndex;

    light.name = prim.GetName();
    light.displayName = prim.GetDisplayName();
    light.markedInvisible = isMarkedInvisible(ctx, prim);

    // Light type specific attributes

    if (prim.IsA<UsdLuxDiskLight>()) {
        light.type = LightType::Disk;
        const UsdLuxDiskLight usdLight(prim);
        bool hasShapingAPI = prim.HasAPI<UsdLuxShapingAPI>();

        readCommonLightAttributes(usdLight, light);

        // Radius
        if (!usdLight.GetRadiusAttr().Get(&light.radius)) {
            TF_WARN("When reading USD layers, failed to read radius of disk light %s",
                    light.name.c_str());
        }

        if (hasShapingAPI) {
            const UsdLuxShapingAPI usdShapingAPI(prim);

            // Cone Angle
            if (!usdShapingAPI.GetShapingConeAngleAttr().Get(&light.coneAngle)) {
                TF_WARN("When reading USD layers, failed to read cone angle of disk light %s",
                        light.name.c_str());
            }

            // Cone Falloff
            if (!usdShapingAPI.GetShapingConeSoftnessAttr().Get(&light.coneFalloff)) {
                TF_WARN("When reading USD layers, failed to read cone falloff of disk light %s",
                        light.name.c_str());
            }
        } else {
            TF_WARN("When reading USD layers, disk light %s has no shaping API. Ignoring cone "
                    "angle and falloff",
                    light.name.c_str());
        }

        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: layer::read disk light { %s }\n",
                     ctx.debugTag.c_str(),
                     prim.GetName().GetText());
    } else if (prim.IsA<UsdLuxRectLight>()) {
        light.type = LightType::Rectangle;
        const UsdLuxRectLight usdLight(prim);

        readCommonLightAttributes(usdLight, light);

        // Length (width)
        if (!usdLight.GetWidthAttr().Get(&light.length[0])) {
            TF_WARN("When reading USD layers, failed to read width of rectangle light %s",
                    light.name.c_str());
        }

        // Length (height)
        if (!usdLight.GetHeightAttr().Get(&light.length[1])) {
            TF_WARN("When reading USD layers, failed to read height of rectangle light %s",
                    light.name.c_str());
        }

        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: layer::read rectangle light { %s }\n",
                     ctx.debugTag.c_str(),
                     prim.GetName().GetText());
    } else if (prim.IsA<UsdLuxSphereLight>()) {
        light.type = LightType::Sphere;
        const UsdLuxSphereLight usdLight(prim);

        readCommonLightAttributes(usdLight, light);

        // Radius
        if (!usdLight.GetRadiusAttr().Get(&light.radius)) {
            TF_WARN("When reading USD layers, failed to read radius of sphere light %s",
                    light.name.c_str());
        }

        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: layer::read sphere light { %s }\n",
                     ctx.debugTag.c_str(),
                     prim.GetName().GetText());
    } else if (prim.IsA<UsdLuxDomeLight>()) {
        light.type = LightType::Environment;
        const UsdLuxDomeLight usdLight(prim);

        readCommonLightAttributes(usdLight, light);

        // TODO: Add support for texture

        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: layer::read dome light { %s }\n",
                     ctx.debugTag.c_str(),
                     prim.GetName().GetText());
    } else if (prim.IsA<UsdLuxDistantLight>()) {
        light.type = LightType::Sun;
        UsdLuxDistantLight usdLight(prim);

        readCommonLightAttributes(usdLight, light);

        // Angle
        if (!usdLight.GetAngleAttr().Get(&light.angle)) {
            TF_WARN("When reading USD layers, failed to read angle of distant light %s",
                    light.name.c_str());
        }

        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: layer::read rectangle light { %s }\n",
                     ctx.debugTag.c_str(),
                     prim.GetName().GetText());
    } else {
        TF_WARN(
          "Expected a supported light, but instead encountered a prim at \"%s\" of type \"%s\"\n",
          prim.GetPath().GetText(),
          prim.GetTypeName().GetText());

        return false;
    }

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
    if (ctx.options->ignoreInvisible && prim.IsA<UsdGeomImageable>()) {
        UsdGeomImageable imageable(prim);
        if (imageable && imageable.GetVisibilityAttr().HasValue()) {
            TfToken visibility;
            imageable.GetVisibilityAttr().Get<TfToken>(&visibility);
            if (visibility == UsdGeomTokens->invisible) {
                // visibility will otherwise be UsdGeomTokens->inherited
                TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                             "%s: layer::read prim: ignoring invisible prim \"%s\"\n",
                             ctx.debugTag.c_str(),
                             prim.GetName().GetString().c_str());
                return false;
            }
        }
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
        f = readXform;
    else if (prim.IsA<UsdGeomMesh>() || prim.IsA<UsdGeomPoints>())
        f = readMeshOrPoints;
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
    else if (prim.IsA<UsdLuxBoundableLightBase>() || prim.IsA<UsdLuxNonboundableLightBase>())
        f = readLight;
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

void
readAnimationTracks(UsdData& usd)
{
    if (!usd.hasAnimations) {
        return;
    }

    // Read the animation tracks from the dict
    if (VtDictionaryIsHolding<VtDictionary>(usd.metadata, "animationTracks")) {
        const VtDictionary& tracksDictionary =
          VtDictionaryGet<VtDictionary>(usd.metadata, "animationTracks");

        const std::string minTimeKey("minTime");
        const std::string maxTimeKey("maxTime");
        const std::string offsetKey("offset");
        const std::string displayNameKey("displayName");

        for (auto i : tracksDictionary) {
            if (!i.second.IsHolding<VtDictionary>()) {
                break;
            }

            const std::string& name = i.first;
            const VtDictionary& dict = i.second.UncheckedGet<VtDictionary>();

            if (!VtDictionaryIsHolding<float>(dict, minTimeKey) ||
                !VtDictionaryIsHolding<float>(dict, maxTimeKey) ||
                !VtDictionaryIsHolding<float>(dict, offsetKey)) {
                break;
            }

            AnimationTrack track;
            track.name = name;
            if (VtDictionaryIsHolding<std::string>(dict, displayNameKey)) {
                track.displayName = VtDictionaryGet<std::string>(dict, displayNameKey);
            }
            track.minTime = VtDictionaryGet<float>(dict, minTimeKey);
            track.maxTime = VtDictionaryGet<float>(dict, maxTimeKey);
            track.offsetToJoinedTimeline = VtDictionaryGet<float>(dict, offsetKey);

            usd.animationTracks.emplace_back(std::move(track));
        }
    }

    // If the dict is not found, read the first animation track from the string. This case can
    // happen if the USD file was imported without explicitly setting the fbxAnimationStacks or
    // gltfAnimationTracks parameter.
    if (usd.animationTracks.empty() &&
        VtDictionaryIsHolding<std::string>(usd.metadata, "defaultAnimationTrack")) {
        const std::string& name =
          VtDictionaryGet<std::string>(usd.metadata, "defaultAnimationTrack");

        AnimationTrack track;
        track.name = name;
        usd.animationTracks.emplace_back(std::move(track));
    }

    // Sort by offsetToJoinedTimeline to preserve the track ordering
    std::sort(usd.animationTracks.begin(),
              usd.animationTracks.end(),
              [](const AnimationTrack& a, const AnimationTrack& b) {
                  return a.offsetToJoinedTimeline > b.offsetToJoinedTimeline;
              });

    // If we couldn't find any tracks in the metadata, create one track by default
    if (usd.animationTracks.empty()) {
        AnimationTrack track;
        track.name = "Animation";
        usd.animationTracks.push_back(track);
    }
}

void
splitAnimationTracks(UsdData& usd)
{
    if (usd.animationTracks.size() <= 1) {
        // We don't have multiple tracks. Nothing to do!
        return;
    }

    // Split NodeAnimations
    for (Node& node : usd.nodes) {
        if (node.animations.empty()) {
            continue;
        }

        // First, duplicate the animation data
        NodeAnimation mainAnimation = std::move(node.animations.front());

        // Create an animation for each track, clearing out the first track
        node.animations.front() = {};
        node.animations.resize(usd.animationTracks.size());

        // For each track, filter all timepoints that are within range
        for (int animationTrackIndex = 0; animationTrackIndex < usd.animationTracks.size();
             animationTrackIndex++) {
            AnimationTrack& track = usd.animationTracks[animationTrackIndex];
            float mainMinTime = track.minTime + track.offsetToJoinedTimeline;
            float mainMaxTime = track.maxTime + track.offsetToJoinedTimeline;

            auto filterTimeValues = [&track, mainMinTime, mainMaxTime](const auto& srcTimeValues,
                                                                       auto& dstTimeValues) {
                int t = 0;

                for (const float time : srcTimeValues.times) {
                    if (srcTimeValues.values.size() <= t) {
                        break;
                    }

                    if (time >= mainMinTime && time <= mainMaxTime) {
                        track.hasTimepoints = true;
                        dstTimeValues.values.push_back(srcTimeValues.values[t]);
                        dstTimeValues.times.push_back(time - track.offsetToJoinedTimeline);
                    }

                    t++;
                }
            };

            NodeAnimation& nodeAnimation = node.animations[animationTrackIndex];
            filterTimeValues(mainAnimation.translations, nodeAnimation.translations);
            filterTimeValues(mainAnimation.rotations, nodeAnimation.rotations);
            filterTimeValues(mainAnimation.scales, nodeAnimation.scales);
        }
    }

    // Split SkeletonAnimations

    for (Skeleton& skeleton : usd.skeletons) {
        std::vector<SkeletonAnimation> splitSkeletonAnimations;

        for (SkeletonAnimation& skeletonAnimation : skeleton.skeletonAnimations) {
            for (int animationTrackIndex = 0; animationTrackIndex < usd.animationTracks.size();
                 animationTrackIndex++) {
                AnimationTrack& track = usd.animationTracks[animationTrackIndex];
                float mainMinTime = track.minTime + track.offsetToJoinedTimeline;
                float mainMaxTime = track.maxTime + track.offsetToJoinedTimeline;

                splitSkeletonAnimations.push_back(SkeletonAnimation());
                SkeletonAnimation& filteredAnimation = splitSkeletonAnimations.back();

                int t = 0;
                for (const float time : skeletonAnimation.times) {
                    if (skeletonAnimation.translations.size() <= t ||
                        skeletonAnimation.rotations.size() <= t ||
                        skeletonAnimation.scales.size() <= t) {
                        break;
                    }

                    if (time >= mainMinTime && time <= mainMaxTime) {
                        track.hasTimepoints = true;

                        filteredAnimation.times.push_back(time - track.offsetToJoinedTimeline);
                        filteredAnimation.translations.push_back(skeletonAnimation.translations[t]);
                        filteredAnimation.rotations.push_back(skeletonAnimation.rotations[t]);
                        filteredAnimation.scales.push_back(skeletonAnimation.scales[t]);
                    }

                    t++;
                }
            }
        }

        skeleton.skeletonAnimations = splitSkeletonAnimations;
    }

    // XXX Should we remove tracks with no animations present? If we do this we'll need to
    // remove the corresponding NodeAnimations and SkeletonAnimations. We can only get such tracks
    // if some other tool is adding the track metadata. Our importers will not do this.
}

bool
readLayer(const ReadLayerOptions& options,
          const SdfLayer& constLayer,
          UsdData& usd,
          const std::string& debugTag)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%s: layer::read Start\n", debugTag.c_str());
    auto layer = SdfCreateNonConstHandle<SdfLayer>(&constLayer);
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

    readAnimationTracks(usd);

    splitAnimationTracks(usd);

    resolveMaterialBindings(ctx);
    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%s: layer::read End\n", ctx.debugTag.c_str());

    // These checks are only active when the the FILE_FORMAT_UTIL TfDebug flag is on
    checkAndPrintMeshIssues(usd);

    return true;
}
}
