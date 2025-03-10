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
#include "fbxImport.h"
#include "debugCodes.h"
#include <fileformatutils/common.h>
#include <fileformatutils/images.h>
#include <fileformatutils/materials.h>
#include <fileformatutils/usdData.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdSkel/utils.h>

using namespace PXR_NS;
using namespace fbxsdk;
namespace adobe::usd {

struct ImportedFbxStack
{
    std::string name;
    FbxAnimStack* stack = nullptr;
    std::vector<FbxAnimLayer*> animLayers;
};

struct ImportedFbxSkeleton
{
    FbxNode* fbxParent = nullptr;
    std::vector<FbxSkeleton*> fbxSkeletons;
};

struct ImportFbxContext
{
    const ImportFbxOptions* options = nullptr;
    UsdStageRefPtr stage;
    UsdData* usd = nullptr;
    Fbx* fbx = nullptr;
    FbxScene* scene = nullptr;
    std::string originalColorSpace;

    // Maps an FbxNode to an index of usd->nodes
    std::unordered_map<FbxNode*, int> nodeMap;

    std::unordered_map<FbxMesh*, int> meshes;
    std::unordered_map<FbxObject*, int> materials;

    // Maps a mesh index to a skin index, if the mesh is skinned.
    std::unordered_map<int, int> meshSkinsMap;
    // Maps an FbxNode* to a joint index in a skeleton. We expect no repeated entries.
    std::unordered_map<FbxNode*, size_t> bonesMap;
    // Maps an FbxNode* to a skeleton index. No repeated entries expected.
    std::unordered_map<FbxNode*, size_t> skeletonsMap;

    // Stores Fbx data related to a skeleton. One per USD skeletonIndex
    std::vector<ImportedFbxSkeleton> skeletons;

    // Maps an fbxTexture to a UVSet string
    std::unordered_map<const FbxTexture*, FbxString> textureToUVSetMap;

    // Maps a Surface Material to a mesh. This is used when importing materials
    // as we need to find the mesh that a material is connected to so that
    // we can find the list of UVSets for the mesh.
    std::unordered_map<const FbxSurfaceMaterial*, FbxMesh*> materialToMeshMap;

    // Maps a mesh to its list of UVSet names. This is used to find the uvIndex
    // of the mesh when a material texture specifies a UVSet to use.
    std::unordered_map<const FbxMesh*, std::vector<FbxString>> meshToUvSetsMap;

    // Each ImportedFbxStack has a cache of all anim layers present in that animation stack
    std::vector<ImportedFbxStack> animationStacks;
};

// Metadata on USD will be stored uniformily in the CustomLayerData dictionary.
void
importMetadata(ImportFbxContext& ctx)
{
    ctx.usd->metadata.SetValueAtPath("generator", PXR_NS::VtValue("Adobe usdFbx 1.0"));
    if (!ctx.options->originalColorSpace.IsEmpty()) {
        ctx.usd->metadata.SetValueAtPath(AdobeTokens->originalColorSpace,
                                         PXR_NS::VtValue(ctx.options->originalColorSpace));
    }
}

void
importFbxSettings(ImportFbxContext& ctx)
{
    int sign = 0;
    FbxGlobalSettings& globalSettings = ctx.scene->GetGlobalSettings();
    FbxSystemUnit systemUnit = globalSettings.GetSystemUnit();
    FbxAxisSystem axis = globalSettings.GetAxisSystem();
    FbxAxisSystem::ECoordSystem coordSystem = axis.GetCoorSystem();
    FbxAxisSystem::EUpVector upVector = axis.GetUpVector(sign);

    ctx.usd->metersPerUnit = systemUnit.GetScaleFactor() * systemUnit.GetMultiplier() / 100;
    if (sign == -1) {
        TF_WARN("importFbx: negative up vector is not supported by USD\n");
    }
    switch (upVector) {
        case FbxAxisSystem::EUpVector::eYAxis:
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: up axis: +y\n");
            ctx.usd->upAxis = UsdGeomTokens->y;
            break;
        case FbxAxisSystem::EUpVector::eZAxis:
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: up axis: +z\n");
            ctx.usd->upAxis = UsdGeomTokens->z;
            break;
        case FbxAxisSystem::EUpVector::eXAxis:
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: up axis: +x\n");
            ctx.usd->upAxis = UsdGeomTokens->x;
            break;
        default:
            TF_WARN("importFbx: Unable to get up vector. Defaulting to +y\n");
            ctx.usd->upAxis = UsdGeomTokens->y;
            break;
    }
    if (coordSystem == FbxAxisSystem::ECoordSystem::eLeftHanded) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: coordinate system: left handed  \n");
    } else if (coordSystem == FbxAxisSystem::ECoordSystem::eRightHanded) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: coordinate system: right handed  \n");
    }
}

// Workaround to allow evaluating rest transforms with EvaluateLocalTransform or
// EvaluateGlobalTransform. It looks like the FBX SDK does not allow rest transforms to be computed
// unless all animation stacks are disconnected.
// https://forums.autodesk.com/t5/fbx-forum/evaluating-with-animation-turned-off/td-p/7052419
class ScopedAnimStackDisabler
{
  public:
    ScopedAnimStackDisabler(ImportFbxContext& ctx)
      : mCtx(ctx)
    {
        for (const ImportedFbxStack& fbxStack : ctx.animationStacks) {
            ctx.scene->DisconnectSrcObject(fbxStack.stack);
        }
    }

    ~ScopedAnimStackDisabler()
    {
        for (const ImportedFbxStack& fbxStack : mCtx.animationStacks) {
            mCtx.scene->ConnectSrcObject(fbxStack.stack);
        }
    }

  private:
    ImportFbxContext& mCtx;
};

void
importFbxTransform(ImportFbxContext& ctx,
                   FbxNode* fbxNode,
                   Node& node,
                   GfVec3d& t,
                   GfQuatf& r,
                   GfVec3f& s,
                   bool useGlobalTransform)
{
    // Helper function to decompose the transformation matrix into translation, rotation, and scale
    auto decomposeTransformation = [](GfVec3f& translation,
                                      GfQuatf& rotation,
                                      GfVec3f& scale,
                                      const FbxAMatrix& localTransform) {
        GfVec3h scaleH;
        GfMatrix4d usdLocalTransform = ConvertMatrix4<FbxAMatrix, GfMatrix4d>(localTransform);
        UsdSkelDecomposeTransform(usdLocalTransform, &translation, &rotation, &scaleH);
        rotation.Normalize();
        scale = scaleH;
    };

    for (int animationStackIndex = 0; animationStackIndex < ctx.animationStacks.size();
         animationStackIndex++) {
        // Set the current animation stack so that EvaluateLocalTransform will return the correct
        // value
        ctx.scene->SetCurrentAnimationStack(ctx.animationStacks[animationStackIndex].stack);

        AnimationTrack& track = ctx.usd->animationTracks[animationStackIndex];
        const ImportedFbxStack& fbxStack = ctx.animationStacks[animationStackIndex];

        std::set<FbxTime> keyFrameTimes;

        // Helper function to get the times of every keyframe from a particular animation curve
        auto addFrameTimes = [&keyFrameTimes](const FbxAnimCurve* curve) {
            if (curve != nullptr) {
                // We found animation data, so we extract every keyframe to process below
                int keyCount = curve->KeyGetCount();
                for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
                    keyFrameTimes.insert(curve->KeyGetTime(keyIndex));
                }
            }
        };

        // For each animation layer, check every property for animation curves and extract the
        // keyframes to process
        for (FbxAnimLayer* animLayer : fbxStack.animLayers) {
            for (auto property = fbxNode->GetFirstProperty(); property.IsValid();
                 property = fbxNode->GetNextProperty(property)) {

                if (!property.IsAnimated(animLayer)) {
                    continue;
                }

                FbxAnimCurve* curve = property.GetCurve(animLayer);
                FbxAnimCurveNode* curveNode = property.GetCurveNode(animLayer);

                // Usually the curve has animation data, but sometimes it is null but at least one
                // of the curveNode's channels do. For this reason, we check them all
                addFrameTimes(curve);
                int numChannels = curveNode ? curveNode->GetChannelsCount() : 0;
                for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex) {
                    addFrameTimes(curveNode->GetCurve(channelIndex));
                }
            }
        }

        size_t numKeyFrames = keyFrameTimes.size();
        if (numKeyFrames > 0) {
            node.animations.resize(ctx.animationStacks.size());
        } else {
            continue;
        }

        track.hasTimepoints = true;
        ctx.usd->hasAnimations = true;

        NodeAnimation& nodeAnimation = node.animations[animationStackIndex];

        nodeAnimation.translations.times.clear();
        nodeAnimation.translations.times.reserve(numKeyFrames);
        nodeAnimation.translations.values.reserve(numKeyFrames);

        nodeAnimation.rotations.times.clear();
        nodeAnimation.rotations.times.reserve(numKeyFrames);
        nodeAnimation.rotations.values.reserve(numKeyFrames);

        nodeAnimation.scales.times.clear();
        nodeAnimation.scales.times.reserve(numKeyFrames);
        nodeAnimation.scales.values.reserve(numKeyFrames);

        for (auto keyFrameTime : keyFrameTimes) {
            GfVec3f translation;
            GfQuatf rotation;
            GfVec3f scale;
            float time = keyFrameTime.GetSecondDouble();
            decomposeTransformation(translation,
                                    rotation,
                                    scale,
                                    useGlobalTransform
                                      ? fbxNode->EvaluateGlobalTransform(keyFrameTime)
                                      : fbxNode->EvaluateLocalTransform(keyFrameTime));

            nodeAnimation.translations.times.push_back(time);
            nodeAnimation.translations.values.push_back(translation);

            nodeAnimation.rotations.times.push_back(time);
            nodeAnimation.rotations.values.push_back(rotation);

            nodeAnimation.scales.times.push_back(time);
            nodeAnimation.scales.values.push_back(scale);
        }
    }

    GfVec3f translation;
    GfQuatf rotation;
    GfVec3f scale;
    {
        ScopedAnimStackDisabler animStackDisabler(ctx);
        decomposeTransformation(translation,
                                rotation,
                                scale,
                                useGlobalTransform ? fbxNode->EvaluateGlobalTransform()
                                                   : fbxNode->EvaluateLocalTransform());
    }
    node.translation = translation;
    node.rotation = rotation;
    node.scale = scale;

    // We do not set the local transform for the node, since that will be added to existing
    // animation data, and we already decompose the transformation matrix into the individual
    // channels

    // The GeometricRotation is a rotation in the order XYZ.
    // Refer to fbxsdk\include\fbxsdk\scene\geometry\fbxnode.h
    if (!FbxProperty::HasDefaultValue(fbxNode->GeometricTranslation)) {
        t = toVec3d(fbxNode->GeometricTranslation.Get());
    }
    if (!FbxProperty::HasDefaultValue(fbxNode->GeometricRotation)) {
        r = toQuatf(fbxNode->GeometricRotation.Get());
    }
    if (!FbxProperty::HasDefaultValue(fbxNode->GeometricScaling)) {
        s = toVec3f(fbxNode->GeometricScaling.Get());
    }
}

// Imports a mesh from fbx.
// Extracts data from a FbxMesh attribute into a Mesh cache and links it to its parent Node cache in
// UsdData, to drive the instantiation of a UsdGeomMesh later in layerWrite.
// If the FbxMesh contains skin deformers, then it will link both the associated Skeleton and Mesh
// caches to the Node cache in UsdData (in its field skelMeshes) to drive instantiation of a
// UsdSkelRoot instead.
// Also the `ctx.meshes` map is used to reuse previously encountered FbxMeshes.
bool
importFbxMesh(ImportFbxContext& ctx, FbxMesh* fbxMesh, int parent)
{
    auto [nodeIndex, node] = ctx.usd->getParent(parent);
    const auto it = ctx.meshes.find(fbxMesh);
    if (it != ctx.meshes.end()) {
        int meshIndex = it->second;
        if (meshIndex < 0) {
            // Ignore invalid meshes
            return true;
        }
        Mesh& mesh = ctx.usd->meshes[meshIndex];
        // The first time we reuse a mesh we mark it as instanceable
        mesh.instanceable = true;
        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "importFbx: mesh (instanced from %s (%d)) (parent=%d)\n",
                     mesh.name.c_str(),
                     meshIndex,
                     parent);
        const auto it = ctx.meshSkinsMap.find(meshIndex);
        if (it != ctx.meshSkinsMap.end()) {
            int skeletonIndex = it->second;
            ctx.usd->skeletons[skeletonIndex].meshSkinningTargets.push_back(meshIndex);
        } else {
            node.staticMeshes.push_back(meshIndex);
        }
        return true;
    }

    size_t polyCount = fbxMesh->GetPolygonCount();
    size_t polyVertexCount = fbxMesh->GetPolygonVertexCount();
    size_t controlPointsCount = fbxMesh->GetControlPointsCount();
    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "importFbx: mesh %s with %lu faces, %lu vertices, %lu points\n",
                 fbxMesh->GetName(),
                 polyCount,
                 polyVertexCount,
                 controlPointsCount);
    if (polyCount == 0 || polyVertexCount == 0 || controlPointsCount == 0) {
        TF_WARN("Skipping empty mesh %s", fbxMesh->GetName());
        ctx.meshes[fbxMesh] = -1;
        return true;
    }

    auto [meshIndex, mesh] = ctx.usd->addMesh();
    ctx.meshes[fbxMesh] = meshIndex;
    mesh.name = fbxMesh->GetName();

    mesh.faces.resize(polyCount);
    mesh.indices.resize(polyVertexCount);
    mesh.points.resize(controlPointsCount);
    for (size_t i = 0; i < polyCount; i++) {
        mesh.faces[i] = fbxMesh->GetPolygonSize(i);
    }
    for (size_t i = 0; i < polyVertexCount; i++) {
        mesh.indices[i] = fbxMesh->GetPolygonVertices()[i];
    }
    for (size_t i = 0; i < controlPointsCount; i++) {
        mesh.points[i] = GfVec3f{ static_cast<float>(fbxMesh->GetControlPoints()[i][0]),
                                  static_cast<float>(fbxMesh->GetControlPoints()[i][1]),
                                  static_cast<float>(fbxMesh->GetControlPoints()[i][2]) };
    }

    // Normals
    FbxGeometryElementNormal* normalElement = fbxMesh->GetElementNormal();
    if (normalElement != nullptr) {
        mesh.normals.interpolation = fbxGetInterpolation(normalElement->GetMappingMode());
        if (normalElement->GetReferenceMode() == FbxGeometryElement::eDirect) {
            size_t normalCount = normalElement->GetDirectArray().GetCount();
            mesh.normals.values.resize(normalCount);
            for (size_t i = 0; i < normalCount; i++) {
                FbxVector4 normal = normalElement->GetDirectArray().GetAt(i);
                mesh.normals.values[i] = GfVec3f{ static_cast<float>(normal[0]),
                                                  static_cast<float>(normal[1]),
                                                  static_cast<float>(normal[2]) };
            }
            // TODO: pass over the normal indices instead of expanding, usdutils supports that
        } else { // FbxGeometryElement::eIndexToDirect
            size_t normalCount = normalElement->GetIndexArray().GetCount();
            mesh.normals.values.resize(normalCount);
            for (size_t i = 0; i < normalCount; i++) {
                int normalIndex = normalElement->GetIndexArray().GetAt(i);
                FbxVector4 normal = normalElement->GetDirectArray().GetAt(normalIndex);
                mesh.normals.values[i] = GfVec3f{ static_cast<float>(normal[0]),
                                                  static_cast<float>(normal[1]),
                                                  static_cast<float>(normal[2]) };
            }
        }
    }

    // Uvs
    size_t elementUVsCount = fbxMesh->GetElementUVCount();
    size_t numUVsets = 0;
    for (size_t i = 0; i < elementUVsCount; i++) {
        FbxGeometryElementUV* elementUVs = fbxMesh->GetElementUV(i);
        if (elementUVs == nullptr) {
            TF_WARN("Mesh[%s].uvs[%lu] is null. Skipping\n", mesh.name.c_str(), i);
            continue;
        }
        if (numUVsets > 0) {
            mesh.extraUVSets.push_back(Primvar<PXR_NS::GfVec2f>());
        }
        Primvar<PXR_NS::GfVec2f>& uvprimvar =
          (numUVsets == 0) ? mesh.uvs : mesh.extraUVSets[numUVsets - 1];
        numUVsets++;

        uvprimvar.interpolation = fbxGetInterpolation(elementUVs->GetMappingMode());
        FbxLayerElementArrayTemplate<FbxVector2>& uvs = elementUVs->GetDirectArray();
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: uvs size %d\n", uvs.GetCount());
        uvprimvar.values.resize(uvs.GetCount());
        for (int j = 0; j < uvs.GetCount(); j++) {
            uvprimvar.values[j] =
              GfVec2f{ static_cast<float>(uvs[j][0]), static_cast<float>(uvs[j][1]) };
        }
        if (elementUVs->GetReferenceMode() != FbxLayerElement::EReferenceMode::eDirect) {
            FbxLayerElementArrayTemplate<int>& uvIndices = elementUVs->GetIndexArray();
            size_t uvIndicesCount = uvIndices.GetCount();
            uvprimvar.indices.resize(uvIndicesCount);
            for (size_t j = 0; j < uvIndicesCount; j++) {
                uvprimvar.indices[j] = uvIndices[j];
            }
        }
    }

    // Color
    int displayColorCount = fbxMesh->GetElementVertexColorCount();
    bool convertToLinear = (ctx.originalColorSpace == AdobeTokens->sRGB);
    for (int i = 0; i < displayColorCount; i++) {
        FbxGeometryElementVertexColor* colorElement = fbxMesh->GetElementVertexColor(i);
        auto [colorSetIndex, colorSet] = ctx.usd->addColorSet(meshIndex);
        auto [opacitySetIndex, opacitySet] = ctx.usd->addOpacitySet(meshIndex);
        colorSet.interpolation = fbxGetInterpolation(colorElement->GetMappingMode());
        opacitySet.interpolation = colorSet.interpolation;
        FbxLayerElementArrayTemplate<FbxColor>& fbxColors = colorElement->GetDirectArray();
        colorSet.values.resize(fbxColors.GetCount());
        opacitySet.values.resize(fbxColors.GetCount());
        for (int j = 0; j < fbxColors.GetCount(); j++) {
            GfVec3f color{ static_cast<float>(fbxColors[j][0]),
                           static_cast<float>(fbxColors[j][1]),
                           static_cast<float>(fbxColors[j][2]) };

            if (convertToLinear) {
                color[0] = srgbToLinear(color[0]);
                color[1] = srgbToLinear(color[1]);
                color[2] = srgbToLinear(color[2]);
            }
            colorSet.values[j] = color;
            opacitySet.values[j] = static_cast<float>(fbxColors[j][3]);
        }
        if (colorElement->GetReferenceMode() != FbxLayerElement::EReferenceMode::eDirect) {
            FbxLayerElementArrayTemplate<int>& fbxIndices = colorElement->GetIndexArray();
            colorSet.indices.resize(fbxIndices.GetCount());
            opacitySet.indices.resize(fbxIndices.GetCount());
            for (int j = 0; j < fbxIndices.GetCount(); j++) {
                colorSet.indices[j] = fbxIndices[j];
                opacitySet.indices[j] = fbxIndices[j];
            }
        }
    }

    bool isSkinnedMesh = false;
    int skinCount = fbxMesh->GetDeformerCount(FbxDeformer::eSkin);
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbxMesh: skinCount: %d\n", skinCount);
    for (int i = 0; i < skinCount; i++) {
        // shouldn't really expect > 1 deformer! it would overwrite our Mesh
        FbxSkin* skin = FbxCast<FbxSkin>(fbxMesh->GetDeformer(i, FbxDeformer::eSkin));

        int controlPointsCount = fbxMesh->GetControlPointsCount();
        std::vector<std::vector<int>> indexes(controlPointsCount);
        std::vector<std::vector<float>> weights(controlPointsCount);

        // set default link mode
        FbxCluster::ELinkMode linkMode = FbxCluster::ELinkMode::eNormalize;
        int clusterCount = skin->GetClusterCount();
        if (clusterCount > 0) {
            isSkinnedMesh = true;
            FbxCluster* firstCluster = skin->GetCluster(0);
            if (firstCluster == nullptr) {
                TF_WARN("Skin: %d does not have a first cluster.\n", i);
                continue;
            }
            FbxNode* firstlink = firstCluster->GetLink();
            if (firstlink == nullptr) {
                TF_WARN("Skin: %d first cluster does not have a first link.\n", i);
                continue;
            }
            size_t skeletonIndex = ctx.skeletonsMap[firstlink];

            ctx.meshSkinsMap[meshIndex] = skeletonIndex;
            ctx.usd->skeletons[skeletonIndex].meshSkinningTargets.push_back(meshIndex);

            // set the mesh geomBindTransform based on the transform matrix
            // For some reason, FBX put this matrix on the cluster, but we should get the same
            // result no matter which cluster we look at.
            FbxAMatrix geomBindTransform;
            firstCluster->GetTransformMatrix(geomBindTransform);
            mesh.geomBindTransform = GetUSDMatrixFromFBX(geomBindTransform);

            Skeleton& skeleton = ctx.usd->skeletons[skeletonIndex];

            for (int j = 0; j < clusterCount; j++) {
                FbxCluster* cluster = skin->GetCluster(j);
                if (cluster == nullptr) {
                    TF_WARN("No cluster at skin index %d.\n", j);
                    continue;
                }
                FbxNode* link = cluster->GetLink();
                if (link == nullptr) {
                    TF_WARN("No link at skin index %d.\n", j);
                    continue;
                }

                size_t jointIndex = ctx.bonesMap[link];

                // if the linkMode for any cluster is not eNormalize, then we will disable weight
                // normalization
                FbxCluster::ELinkMode clusterLinkMode = cluster->GetLinkMode();
                if (FbxCluster::ELinkMode::eNormalize != clusterLinkMode)
                    linkMode = clusterLinkMode;

                // Set the bindTransform for the joint
                FbxAMatrix linkTransform;
                cluster->GetTransformLinkMatrix(linkTransform);

                // XXX In theory different meshes could have different link transforms in the case
                // where they share the same skeleton/links. If that can happen, we'd end up with a
                // conflict trying to share the skeleton as USD stores the bindTransform on the
                // skeleton, not on the mesh. We haven't seen this yet though, so we don't try to
                // un-share the skeletons in this case, and instead just ovewrite the bindTransforms
                skeleton.bindTransforms[jointIndex] = GetUSDMatrixFromFBX(linkTransform);

                int clusterControlPointIndicesCount = cluster->GetControlPointIndicesCount();
                int* clusterControlPointIndices = cluster->GetControlPointIndices();
                double* pointsWeights = cluster->GetControlPointWeights();
                if (clusterControlPointIndices == nullptr) {
                    TF_WARN("No cluster control point indices for skin cluster: %d.\n", j);
                    continue;
                }
                if (pointsWeights == nullptr) {
                    TF_WARN("No point weights for skin cluster: %d.\n", j);
                    continue;
                }
                for (int k = 0; k < clusterControlPointIndicesCount; k++) {
                    int controlPointIndex = clusterControlPointIndices[k];
                    if (controlPointIndex > indexes.size() || controlPointIndex > weights.size()) {
                        TF_WARN("Control Point Index outside of index or weight bounds. index: %d "
                                " Index Size: %d  Weight Size: %d",
                                controlPointIndex,
                                indexes.size(),
                                weights.size());
                        continue;
                    } else {
                        double influenceWeight = pointsWeights[k];
                        indexes[controlPointIndex].push_back(jointIndex);
                        weights[controlPointIndex].push_back(influenceWeight);
                    }
                }
            }
        }

        int elementSize =
          std::max_element(indexes.begin(),
                           indexes.end(),
                           [](const std::vector<int> val1, const std::vector<int> val2) -> bool {
                               return val1.size() < val2.size();
                           })
            ->size();

        mesh.influenceCount = elementSize;
        mesh.isRigid = skin->GetSkinningType() == FbxSkin::EType::eRigid;
        mesh.joints.resize(controlPointsCount * elementSize);
        mesh.weights.resize(controlPointsCount * elementSize);
        for (int j = 0; j < controlPointsCount; j++) {
            std::vector<int> indexVector = indexes[j];
            std::vector<float> weightsVector = weights[j];
            int count = indexVector.size();

            // Determine the normalization factor for the weights
            double normalizationFactor = 1.0;
            if (FbxCluster::ELinkMode::eNormalize == linkMode) {
                double sum = 0.0;
                for (int k = 0; k < count; k++)
                    sum += weightsVector[k];
                normalizationFactor = (sum == 0.0) ? 0.0 : 1.0 / sum;
            }

            for (int k = 0; k < elementSize; k++) {
                int targetIndex = (j * elementSize) + k;
                if (k < count) {
                    mesh.joints[targetIndex] = indexVector[k];
                    mesh.weights[targetIndex] = weightsVector[k] * normalizationFactor;
                } else {
                    mesh.joints[targetIndex] = 0;
                    mesh.weights[targetIndex] = 0;
                }
            }
        }
    }
    if (!isSkinnedMesh) {
        node.staticMeshes.push_back(meshIndex);
    }
    // TODO: import blend shapes

    // FBX assets exported from Blender have been found with degenerate triangles that have normals
    // of [0,0,0]. Here, we filter those out so they don't cause validation errors if they are
    // exported to glTF
    trimDegenerateNormals(mesh);

    FbxNode* fbxNode = fbxMesh->GetNode();
    if (fbxNode != nullptr) {
        int materialCount = fbxNode->GetMaterialCount();
        int elementMaterialCount = fbxMesh->GetElementMaterialCount();
        for (int i = 0; i < elementMaterialCount; i++) {
            if (i >= 1) {
                TF_WARN("Mesh[%s].material[%d] Multiple material layers not supported\n",
                        mesh.name.c_str(),
                        i);
                break;
            }
            FbxGeometryElementMaterial* material = fbxMesh->GetElementMaterial(i);
            FbxLayerElement::EMappingMode mappingMode = material->GetMappingMode();
            if (mappingMode == FbxLayerElement::EMappingMode::eNone) {
                TF_DEBUG_MSG(FILE_FORMAT_FBX, "None material mapping mode found\n");
            } else if (mappingMode == FbxLayerElement::EMappingMode::eByControlPoint) {
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "byControlPoint material mapping mode not supported\n");
            } else if (mappingMode == FbxLayerElement::EMappingMode::eByPolygonVertex) {
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "byPolygonVertex material mapping mode not supported\n");
            } else if (mappingMode == FbxLayerElement::EMappingMode::eByPolygon) {
                for (int i = 0; i < materialCount; i++) {
                    auto [subsetIndex, subset] = ctx.usd->addSubset(meshIndex);
                    FbxSurfaceMaterial* fbxMaterial = fbxNode->GetMaterial(i);
                    const auto& it = ctx.materials.find(fbxMaterial);
                    if (it != ctx.materials.end()) {
                        subset.material = it->second;
                    }
                    for (int j = 0; j < material->GetIndexArray().GetCount(); j++) {
                        int index = material->GetIndexArray().GetAt(j);
                        if (index == i) {
                            subset.faces.push_back(j);
                        }
                    }
                }
            } else if (mappingMode == FbxLayerElement::EMappingMode::eByEdge) {
                TF_DEBUG_MSG(FILE_FORMAT_FBX, "byEdge material mapping mode not supported\n");
            } else if (mappingMode == FbxLayerElement::EMappingMode::eAllSame) {
                FbxSurfaceMaterial* fbxMaterial = fbxNode->GetMaterial(i);
                const auto& it = ctx.materials.find(fbxMaterial);
                if (it != ctx.materials.end()) {
                    mesh.material = it->second;
                }
            }
        }
        printMesh("importFbx:", mesh, DEBUG_TAG);
        return true;
    } else {
        TF_WARN("fbxMesh has no root node");
        return false;
    }
}

TfToken
fbxWrapModeToToken(FbxTexture::EWrapMode wrap)
{
    return (wrap == FbxTexture::EWrapMode::eRepeat) ? AdobeTokens->repeat : AdobeTokens->clamp;
}

void
importPropFileTexture(ImportFbxContext& ctx,
                      const std::unordered_map<FbxObject*, size_t>& textures,
                      const FbxSurfaceMaterial* material,
                      FbxTexture* texture,
                      Input& input,
                      const std::string& channel)
{
    FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(texture);
    if (fileTexture == nullptr)
        return;
    if (const auto& it = textures.find(fileTexture); it != textures.end()) {
        input.image = it->second;
        input.uvIndex = 0;
        input.channel = TfToken(channel);
        input.wrapS = fbxWrapModeToToken(texture->GetWrapModeU());
        input.wrapT = fbxWrapModeToToken(texture->GetWrapModeV());

        FbxString uvset = texture->UVSet.Get();

        FbxMesh* mesh = ctx.materialToMeshMap[material];
        if (mesh) {
            auto const& uvSets = ctx.meshToUvSetsMap[mesh];
            auto it = std::find(uvSets.begin(), uvSets.end(), uvset);
            if (it != uvSets.end()) {
                input.uvIndex = it - uvSets.begin();
            }
        }

        double su = texture->GetScaleU();
        double sv = texture->GetScaleV();
        if (su != 1 || sv != 1) {
            input.transformScale = GfVec2f(su, sv);
        }
        double rot = texture->GetRotationW();
        if (rot != 0) {
            input.transformRotation = rot;
        }
        double tu = texture->GetTranslationU();
        double tv = texture->GetTranslationV();
        if (tu != 0 || tv != 0) {
            input.transformTranslation = GfVec2f(tu, tv);
        }
    }
}

std::string
printPropValue(FbxPropertyT<FbxDouble> prop)
{
    std::ostringstream oss;
    oss << std::setprecision(3) << prop.Get();
    return oss.str();
};

std::string
printPropValue(FbxPropertyT<FbxDouble3> prop)
{
    std::ostringstream oss;
    oss << std::setprecision(3);
    FbxDouble3 value = prop.Get();
    oss << "<" << value[0] << "," << value[1] << "," << value[2] << ">";
    return oss.str();
};

VtValue
srgbToLinear(const VtValue& value)
{
    if (value.IsHolding<GfVec3f>()) {
        const GfVec3f& vec = value.UncheckedGet<GfVec3f>();
        float r = srgbToLinear(vec[0]);
        float g = srgbToLinear(vec[1]);
        float b = srgbToLinear(vec[2]);
        return VtValue(GfVec3f(r, g, b));
    }

    if (!value.IsEmpty()) {
        TF_WARN("srgbToLinear got non GfVec3f type: %s\n", value.GetTypeName().c_str());
    }

    return value;
}

template<typename T>
void
importPropTexture(ImportFbxContext& ctx,
                  const std::unordered_map<FbxObject*, size_t>& textures,
                  const FbxSurfaceMaterial* material,
                  FbxPropertyT<T>& prop,
                  Input& input,
                  const std::string& channel,
                  const TfToken& colorSpace = AdobeTokens->sRGB)
{
    int propCount = prop.GetSrcObjectCount();
    if (propCount > 1) {
        TF_WARN("More than one source found for property %s only first will be used. \n",
                prop.GetName().Buffer());
    }
    auto srcObj = prop.GetSrcObject();
    std::string textureFilename = "";
    auto texture = FbxCast<FbxTexture>(srcObj);
    if (texture) {
        auto layeredTexture = FbxCast<FbxLayeredTexture>(srcObj);
        if (layeredTexture) {
            if (layeredTexture->GetSrcObjectCount() > 1) {
                TF_WARN(
                  "More than one texture found for layered texture %s, only first will be used.\n",
                  layeredTexture->GetName());
            }
            auto textureObject = layeredTexture->GetSrcObject();
            if (textureObject == nullptr)
                return;
            auto* texture = FbxCast<FbxTexture>(textureObject);
            if (texture == nullptr)
                return;
            importPropFileTexture(ctx, textures, material, texture, input, channel);
        } else {
            importPropFileTexture(ctx, textures, material, texture, input, channel);
        } // else  procedural
    }
    if (!FbxProperty::HasDefaultValue(prop)) {
        input.value = readPropValue(prop);
    }
    if (colorSpace == AdobeTokens->sRGB) {
        input.value = srgbToLinear(input.value);
    }
    // It's handy to also print the value here, besides the texture information
    std::string defaultMessage = FbxProperty::HasDefaultValue(prop) ? "default" : "valid";
    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "    %-18s: image(%d) value(%7s): %-19s %-6s \"%s\"\n",
                 prop.GetName().Buffer(),
                 input.image,
                 defaultMessage.c_str(),
                 printPropValue(prop).c_str(),
                 colorSpace == AdobeTokens->sRGB ? "(sRGB)" : "(raw)",
                 textureFilename.c_str());
    input.colorspace = colorSpace;
}

static const FbxImplementation*
LookForNonSupportedImplementation(FbxSurfaceMaterial* m)
{
    const FbxImplementation* imp = GetImplementation(m, FBXSDK_IMPLEMENTATION_CGFX);
    if (!imp) {
        imp = GetImplementation(m, FBXSDK_IMPLEMENTATION_HLSL);
    }
    if (!imp) {
        imp = GetImplementation(m, FBXSDK_IMPLEMENTATION_SFX);
    }
    if (!imp) {
        imp = GetImplementation(m, FBXSDK_IMPLEMENTATION_OGS);
    }
    if (!imp) {
        imp = GetImplementation(m, FBXSDK_IMPLEMENTATION_SSSL);
    }
    return imp;
}

void
importMeshUVSets(ImportFbxContext& ctx)
{
    size_t meshCount = ctx.scene->GetSrcObjectCount<FbxMesh>();
    for (size_t i = 0; i < meshCount; ++i) {
        FbxMesh* fbxMesh = ctx.scene->GetSrcObject<FbxMesh>(i);
        if (!fbxMesh) {
            TF_WARN("fbxMesh is null: skipping mesh");
            continue;
        }
        FbxNode* fbxNode = fbxMesh->GetNode();
        if (!fbxNode) {
            TF_WARN("fbxNode is null: skipping mesh");
            continue;
        }
        int materialCount = fbxNode->GetMaterialCount();
        if (materialCount) {
            FbxSurfaceMaterial* fbxMaterial = fbxNode->GetMaterial(i);
            if (!ctx.materialToMeshMap[fbxMaterial]) {
                ctx.materialToMeshMap[fbxMaterial] = fbxMesh;
            }
        }

        int elementUVCount = fbxMesh->GetElementUVCount();
        for (int j = 0; j < elementUVCount; j++) {
            FbxGeometryElementUV* elementUV = fbxMesh->GetElementUV(j);
            if (elementUV) {
                const char* name = elementUV->GetName();
                auto& vec = ctx.meshToUvSetsMap[fbxMesh];
                if (name) {
                    vec.push_back(name);
                } else {
                    vec.push_back("default");
                }
            }
        }
    }
}

std::string
_toCamelCase(std::string s) noexcept
{
    bool tail = false;
    std::size_t n = 0;
    for (unsigned char c : s) {
        if (c == '-' || c == '_') {
            tail = false;
        } else if (tail) {
            s[n++] = c;
        } else {
            tail = true;
            if (n != 0) {
                s[n++] = std::toupper(c);
            } else {
                s[n++] = c;
            }
        }
    }
    s.resize(n);
    return s;
}

// This is designed to identify if a material contains a "Autodesk Standard Surface" defintion and
// to process it for mapping to our material model. There isn't a reliable direct way to check this,
// so we need to check the properties of the material to see if they match the standard surface
// material.  The assumption is that if a materal contains all the properties we are expecting to
// see on a standard surface shader, it's safe to assume it is a standard surface shader. There are
// 3 known ways to generate a shader that is a standard surface shader using the Autodesk tools:
//
// 1.) In Maya you define a "Standard Surface" shader which is Renderer agnostic
//
// 2.) In Maya you can define a Arnold variant of the standard surface shader, specially called "Ai
// Standard Surface" shader which is a standard surface shader designed to work with Arnold
//
// 3.) Finally in 3ds max you can define a "Standard Surface" shader, which is an Arnold shader
//
// It's worth nothing that although both Maya and Max can produce an Arnold Standard Surface shader,
// the actual FBX file looks very different between the two and you can't even interop the FBX file
// between Maya and Max.  But regardless we're able to use both of them in this utility by just
// relying on the properties being present and treating them effectively the same here.
//
// Returns true if the material was a standard surface shader and was successfully processed
bool
_mapAutodeskStandardMaterial(const FbxSurfaceMaterial* fbxMaterial,
                             ImportFbxContext& ctx,
                             const std::unordered_map<FbxObject*, size_t>& textures,
                             Material& usdMaterial,
                             InputTranslator& inputTranslator)
{
    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "Checking if %s is an Autodesk Standard Surface Material\n",
                 fbxMaterial->GetName());
    // This will contain the properties that are directly mapped from the standard surface exactly
    // as is.  We need to note if they are One or Three channels and if they are sRGB or raw for
    // later usage.
    std::unordered_map<std::string,
                       std::tuple<Input&, const FbxPropertyNumChannels&, const TfToken&>>
      standardSurfToUsdProperty = {
          { "base_color",
            { usdMaterial.diffuseColor, FbxPropertyNumChannels::Three, AdobeTokens->sRGB } },
          { "specular_color",
            { usdMaterial.specularColor, FbxPropertyNumChannels::Three, AdobeTokens->sRGB } },
          { "metalness", { usdMaterial.metallic, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_roughness",
            { usdMaterial.roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat", { usdMaterial.clearcoat, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_color",
            { usdMaterial.clearcoatColor, FbxPropertyNumChannels::Three, AdobeTokens->sRGB } },
          { "coat_roughness",
            { usdMaterial.clearcoatRoughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_IOR",
            { usdMaterial.clearcoatIor, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "sheen_color",
            { usdMaterial.sheenColor, FbxPropertyNumChannels::Three, AdobeTokens->sRGB } },
          { "sheen_roughness",
            { usdMaterial.sheenRoughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_anisotropy",
            { usdMaterial.anisotropyLevel, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_rotation",
            { usdMaterial.anisotropyAngle, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_IOR", { usdMaterial.ior, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission",
            { usdMaterial.transmission, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission_depth",
            { usdMaterial.absorptionDistance, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission_color",
            { usdMaterial.absorptionColor, FbxPropertyNumChannels::Three, AdobeTokens->sRGB } },
          { "subsurface_color",
            { usdMaterial.scatteringColor, FbxPropertyNumChannels::Three, AdobeTokens->sRGB } },
      };

    // Make a set that has all the properties we want to validate to confirm this is a standard
    // surface.  Will contain the above set with some additional properties we will need special
    // case handling for later on
    const std::string kEmission = "emission";
    const std::string kEmissionColor = "emission_color";
    const std::string kNormalCamera = "normal_camera";
    const std::string kCoatNormal = "coat_normal";
    const std::string kOpacity = "opacity";
    std::set<std::string> validatedStandardSurfProperties;
    for (auto& it : standardSurfToUsdProperty) {
        validatedStandardSurfProperties.insert(it.first);
    }
    validatedStandardSurfProperties.insert(kEmission);
    validatedStandardSurfProperties.insert(kEmissionColor);
    validatedStandardSurfProperties.insert(kNormalCamera);
    validatedStandardSurfProperties.insert(kCoatNormal);
    validatedStandardSurfProperties.insert(kOpacity);

    // Some implementations of the standard surface use camel case for the properties instead of
    // snake case, so we need to check both permutations
    auto getProp = [&fbxMaterial](const std::string& name) -> FbxProperty {
        FbxProperty property = FbxSurfaceMaterialUtils::GetProperty(name.c_str(), fbxMaterial);
        if (!property.IsValid()) {
            std::string camelCaseProp = _toCamelCase(name);
            property = FbxSurfaceMaterialUtils::GetProperty(camelCaseProp.c_str(), fbxMaterial);
        }
        return property;
    };

    // Do a two-pass strategy so we don't map any channels until we've confirmed it is a standard
    // surface shader. Basically it's all or nothing if the standard surface shader is used or not
    for (auto& it : validatedStandardSurfProperties) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "Looking for standard surface property %s\n", it.c_str());
        auto property = getProp(it);
        if (!property.IsValid()) {
            TF_DEBUG_MSG(FILE_FORMAT_FBX,
                         "Standard surface property %s was not found, assuming this is not an "
                         "instance of the autodesk standard surface shader\n",
                         it.c_str());
            return false;
        }
    }

    // If we got here then we assume this is one of the standard shader variants because it had all
    // of the properties we are expecting to see and use to map to USD
    for (auto& it : standardSurfToUsdProperty) {
        auto property = getProp(it.first);
        auto numChannels = std::get<1>(it.second);
        auto colorSpace = std::get<2>(it.second);
        if (numChannels == FbxPropertyNumChannels::One) {
            auto typedProp = static_cast<FbxPropertyT<FbxDouble>>(property);
            Input input;
            importPropTexture(ctx, textures, fbxMaterial, typedProp, input, "r", colorSpace);
            inputTranslator.translateDirect(input, std::get<0>(it.second));
        } else if (numChannels == FbxPropertyNumChannels::Three) {
            auto typedProp = static_cast<FbxPropertyT<FbxDouble3>>(property);
            Input input;
            importPropTexture(ctx, textures, fbxMaterial, typedProp, input, "rgb", colorSpace);
            inputTranslator.translateDirect(input, std::get<0>(it.second));
        }
    }

    // Special case handling for additional properties that aren't directly mapped

    // Only include normal maps if they are defined as non empty file path strings, otherwise the
    // empty type wouldn't be handled by USD properly
    auto normalCameraProperty = getProp(kNormalCamera);
    auto normalCameraTexture = FbxCast<FbxTexture>(normalCameraProperty.GetSrcObject());
    if (normalCameraTexture) {
        auto typedProp = static_cast<FbxPropertyT<FbxDouble3>>(normalCameraProperty);
        Input input;
        importPropTexture(ctx, textures, fbxMaterial, typedProp, input, "rgb", AdobeTokens->raw);
        inputTranslator.translateDirect(input, usdMaterial.normal);
    }

    auto coatNormalProperty = getProp(kCoatNormal);
    auto coatNormalTexture = FbxCast<FbxTexture>(coatNormalProperty.GetSrcObject());
    if (coatNormalTexture) {
        auto typedProp = static_cast<FbxPropertyT<FbxDouble3>>(coatNormalProperty);
        Input input;
        importPropTexture(ctx, textures, fbxMaterial, typedProp, input, "rgb", AdobeTokens->raw);
        inputTranslator.translateDirect(input, usdMaterial.clearcoatNormal);
    }

    auto emissionProperty = static_cast<FbxPropertyT<FbxDouble>>(getProp(kEmission));
    Input emissionInput;
    importPropTexture(
      ctx, textures, fbxMaterial, emissionProperty, emissionInput, "r", AdobeTokens->raw);

    auto emissionColorProperty = static_cast<FbxPropertyT<FbxDouble3>>(getProp(kEmissionColor));
    Input emissionColorInput;
    importPropTexture(ctx,
                      textures,
                      fbxMaterial,
                      emissionColorProperty,
                      emissionColorInput,
                      "rgb",
                      AdobeTokens->sRGB);

    // XXX @dcoffey I believe a more proper way to do this is to keep the emissive intensity as a
    // separate input because in UIs that use a color picker to modify this input you will lose
    // values over one upon modification.  This matches how GLTF handles it though currently, and I
    // think this also is an issue there as well
    inputTranslator.translateFactor(emissionColorInput, emissionInput, usdMaterial.emissiveColor);

    // Opacity in USD must be stored as a single value
    auto opacityProperty = getProp(kOpacity);
    if (opacityProperty.IsValid()) {
        auto opacityTypedProp = static_cast<FbxPropertyT<FbxDouble3>>(opacityProperty);
        FbxDouble3 opacityColor = opacityTypedProp.Get();

        // Convert the opacity color to grayscale and use that as the opacity value
        double grayscaleOpacity = (opacityColor[0] + opacityColor[1] + opacityColor[2]) / 3.0;
        usdMaterial.opacity.value = grayscaleOpacity;
        usdMaterial.opacity.colorspace = AdobeTokens->raw;
    }

    return true;
}

// Returns a normalized path, using '/' as the separator. Note that if a component of the file path
// has a '\' on POSIX systems, this would misinterpret that '\' as a directory separator. But
// given that we don't know what OS the original path came from, this replacement is helpful
// so that filesystem methods can interpret a Windows filepath on MacOS.
static std::filesystem::path
normalizePathFromAnyOS(const std::string& path)
{
    std::string normalized = path;

    // Replace all backslashes with forward slashes before using the lexically_normal() method
    // below. This step is particularly needed on POSIX systems to handle the case when the input
    // path is coming from Windows and uses backslashes as a delimiter.
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    // Remove any . or .. in the path, and fix up cases where we have consecutive separators
    normalized = std::filesystem::u8path(path).lexically_normal().u8string();

    // Replace all backslashes with forward slashes again, as lexically_normal() will convert
    // to backslashes on Windows.
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    return std::filesystem::u8path(normalized);
}

// Return whether the path is an absolute path, even if it refers to a path relevant
// for a different OS than the one we are running on. We cannot use
// std::filesystem::path::is_absolute() or TfIsRelativePath() here, as these functions only work for
// file paths valid on the current OS.
static bool
isAbsolutePathFromAnyOS(const std::filesystem::path& path)
{
    std::string filename = path.u8string();

    // 1. Manually check for POSIX absolute paths (starting with '/')
    if (!filename.empty() && filename[0] == '/') {
        return true;
    }

    // 2. Manually check for Windows absolute paths
    // - Drive letter followed by either '\' or '/' (e.g., "C:\", "C:/")
    if (filename.size() > 2 && std::isalpha(filename[0]) && filename[1] == ':' &&
        (filename[2] == '\\' || filename[2] == '/')) {
        return true;
    }

    // 3. Check for UNC paths (starting with "\\" or "//")
    if (filename.size() > 1 && ((filename[0] == '\\' && filename[1] == '\\') ||
                                (filename[0] == '/' && filename[1] == '/'))) {
        return true;
    }

    return false;
}

void
importFbxMaterials(ImportFbxContext& ctx)
{
    std::unordered_map<FbxObject*, size_t> textures;
    std::vector<ImageAsset> images(ctx.scene->GetTextureCount());
    const std::filesystem::path parentPath =
      std::filesystem::u8path(ctx.fbx->filename).parent_path();
    for (int i = 0; i < ctx.scene->GetTextureCount(); i++) {
        FbxTexture* texture = ctx.scene->GetTexture(i);
        FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(texture);
        if (fileTexture == nullptr)
            continue;

        // FBX seems to store the original absolute file name even though that
        // might be on someone else's machine. We need to use this to match the embedded data
        // key. But otherwise we shouldn't use it as it won't be valid if the FBX file has been
        // shared with another user after it was created.
        std::string origAbsFileName = fileTexture->GetFileName();
        auto embedded = ctx.fbx->embeddedData.find(origAbsFileName);
        bool isEmbedded = embedded != ctx.fbx->embeddedData.end();

        std::string baseName;
        std::string absFileName; // Only used when image is not embedded
        if (isEmbedded) {
            baseName = normalizePathFromAnyOS(origAbsFileName).filename().u8string();
        } else {
            // GetRelativeFileName() will use the original OS path delimiters. We must normalize it
            // before adding it to the metadata.
            // Also- despite the name, GetRelativeFileName() may return an absolute path!
            std::filesystem::path filePathNormalized =
              normalizePathFromAnyOS(fileTexture->GetRelativeFileName());

            // Add the path to the metadata even if the file is not present on disk.
            ctx.usd->importedFileNames.insert(filePathNormalized.u8string());

            std::filesystem::path absFilePath;
            if (isAbsolutePathFromAnyOS(filePathNormalized)) {
                absFilePath = filePathNormalized.make_preferred();

                std::error_code error_code;
                if (!std::filesystem::exists(absFilePath, error_code)) {
                    TF_WARN("FBX image \"%s\" not found", absFilePath.u8string().c_str());
                    continue;
                }
            } else {
                // We then convert the path to use the native OS separator before combining it with
                // the parent path. This will now match the OS currently running this code so we
                // might get a different result here than what was originally returned by
                // GetRelativeFileName()
                std::filesystem::path filePathPreferred = filePathNormalized.make_preferred();
                absFilePath = parentPath / filePathPreferred;

                std::error_code error_code;
                if (!std::filesystem::exists(absFilePath, error_code)) {
                    TF_WARN("FBX image \"%s\" not found relative to source file",
                            filePathPreferred.u8string().c_str());
                    continue;
                }
            }

            absFileName = absFilePath.u8string();
            baseName = absFilePath.filename().u8string();
        }

        textures[texture] = i;

        FbxString uvSet = texture->UVSet.Get();
        ctx.textureToUVSetMap[texture] = uvSet;

        const std::string name = baseName;
        const std::string extension = TfGetExtension(name);
        ImageAsset& image = images[i];
        image.name = name;
        image.uri = name;
        image.format = getFormat(extension);
        if (ctx.options->importImages) {
            if (isEmbedded) {
                const std::vector<char>& data = embedded->second;
                image.image.resize(data.size());
                memcpy(image.image.data(), data.data(), data.size());
            } else {
                std::ifstream file(absFileName, std::ios::binary);
                if (!file.is_open()) {
                    TF_WARN("Failed to open file \"%s\"", absFileName.c_str());
                    continue;
                }
                file.seekg(0, file.end);
                int length = file.tellg();
                file.seekg(0, file.beg);
                image.image.resize(length);
                file.read(reinterpret_cast<char*>(image.image.data()), length);
                file.close();
            }
        }
    }

    InputTranslator inputTranslator(ctx.options->importImages, images, DEBUG_TAG);
    size_t materialsCount = ctx.scene->GetSrcObjectCount<FbxSurfaceMaterial>();
    ctx.usd->materials.resize(materialsCount);
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "\tMaterials count: %lu \n", materialsCount);
    for (size_t i = 0; i < materialsCount; i++) {
        Material& um = ctx.usd->materials[i];
        FbxSurfaceMaterial* material = ctx.scene->GetSrcObject<FbxSurfaceMaterial>(i);
        ctx.materials[material] = i; // Should use GetUniqueID() instead of FbxObject* as key?
        um.name = material->GetName();
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: material[%lu] { %s }\n", i, um.name.c_str());

        FbxProperty lP =
          FbxSurfaceMaterialUtils::GetProperty(FbxSurfaceMaterial::sShadingModel, material);
        auto shaderModel = lP.Get<FbxString>().Buffer();
        TF_DEBUG_MSG(FILE_FORMAT_FBX, " Shader model: %s\n", shaderModel);

        // Check for and process the autodesk standard surface representation first before we do
        // anything else as this is handled as a special case
        if (_mapAutodeskStandardMaterial(material, ctx, textures, um, inputTranslator)) {
            // Everything was done in the above util, so we can just continue
            continue;
        }

        const FbxImplementation* imp = LookForNonSupportedImplementation(material);
        if (imp) { // This is a hardware shader
            TF_WARN("Hardware shader not supported\n");
            TF_DEBUG_MSG(FILE_FORMAT_FBX, " Language: %s\n", imp->Language.Get().Buffer());
            TF_DEBUG_MSG(
              FILE_FORMAT_FBX, " LanguageVersion: %s\n", imp->LanguageVersion.Get().Buffer());
            TF_DEBUG_MSG(FILE_FORMAT_FBX, " RenderName: %s\n", imp->RenderName.Buffer());
            TF_DEBUG_MSG(FILE_FORMAT_FBX, " RenderAPI: %s\n", imp->RenderAPI.Get().Buffer());
            TF_DEBUG_MSG(
              FILE_FORMAT_FBX, " RenderAPIVersion: %s\n", imp->RenderAPIVersion.Get().Buffer());
            const FbxBindingTable* lRootTable = imp->GetRootTable();
            FbxString filename = lRootTable->DescAbsoluteURL.Get();
            FbxString techniqueName = lRootTable->DescTAG.Get();
            continue;
        }

        Input ambientFactor;
        Input diffuse;
        Input diffuseFactor;
        Input emissive;
        Input emissiveFactor;
        Input normal;
        Input bump;
        Input transparentColor;
        Input transparencyFactor;
        Input shininess;
        Input specular;
        Input specularFactor;
        Input reflectionFactor;

        FbxSurfaceLambert* lambert = FbxCast<FbxSurfaceLambert>(material);
        FbxSurfacePhong* phong = FbxCast<FbxSurfacePhong>(material);
        if (lambert) {
            importPropTexture(ctx,
                              textures,
                              material,
                              lambert->AmbientFactor,
                              ambientFactor,
                              "r",
                              AdobeTokens->raw);
            importPropTexture(ctx, textures, material, lambert->Diffuse, diffuse, "rgb");
            importPropTexture(ctx,
                              textures,
                              material,
                              lambert->DiffuseFactor,
                              diffuseFactor,
                              "r",
                              AdobeTokens->raw);
            importPropTexture(ctx, textures, material, lambert->Emissive, emissive, "rgb");
            importPropTexture(ctx,
                              textures,
                              material,
                              lambert->EmissiveFactor,
                              emissiveFactor,
                              "r",
                              AdobeTokens->raw);
            importPropTexture(
              ctx, textures, material, lambert->NormalMap, normal, "rgb", AdobeTokens->raw);
            importPropTexture(ctx, textures, material, lambert->Bump, bump, "r", AdobeTokens->raw);

            // For transparent textures, we only capture the R channel of the texture as we will
            // map this directly to opacity if the texture exists. We do this because the USD
            // Preview Surface only has a single-valued opacity property.
            // HOWEVER, using the 'r' channel of the TransparentColor texture as an opacity value
            // worked for some fbx scenes with separate opacity textures but some fbx scenes
            // (possibly incorrectly) used the DiffuseColor texture as the TransparentColor texture.
            // This lead to strange results. As a consequence, we are currently ignoring the
            // TransparentColor property and will only use the TransparencyFactor and Opacity fbx
            // properties on the material to map to the USD opacity property. importPropTexture(ctx,
            // textures, material, lambert->TransparentColor, transparentColor, "r",
            // AdobeTokens->raw);

            importPropTexture(ctx,
                              textures,
                              material,
                              lambert->TransparencyFactor,
                              transparencyFactor,
                              "r",
                              AdobeTokens->raw);
        }
        if (phong) {
            importPropTexture(ctx, textures, material, phong->Specular, specular, "rgb");
            importPropTexture(
              ctx, textures, material, phong->Shininess, shininess, "rgb", AdobeTokens->raw);
            importPropTexture(ctx,
                              textures,
                              material,
                              phong->SpecularFactor,
                              specularFactor,
                              "r",
                              AdobeTokens->raw);
            importPropTexture(ctx,
                              textures,
                              material,
                              phong->ReflectionFactor,
                              reflectionFactor,
                              "r",
                              AdobeTokens->raw);
        }

        if (ctx.options->importPhong) {
            inputTranslator.translatePhong2PBR(
              diffuse, specular, shininess, um.diffuseColor, um.metallic, um.roughness);
        } else {
            inputTranslator.translateDirect(diffuse, um.diffuseColor);
            // Note, using reflectionFactor for metallic, and specularFactor for roughness, are very
            // crude approximations for a Phong to PBR conversion.
            inputTranslator.translateDirect(reflectionFactor, um.metallic);
            inputTranslator.translateDirect(specularFactor, um.roughness);
        }

        inputTranslator.translateFactor(emissive, emissiveFactor, um.emissiveColor);

        // ignore specular color if there is a specular factor texture but no specular color
        if ((specular.image >= 0) || (specularFactor.image < 0)) {
            inputTranslator.translateFactor(specular, specularFactor, um.specularColor);
        }

        // NOTE: as commented above, we are ignoring TransparentColor values so the
        // condition in the 'if' statement below should always be false, in which case
        // the 'else' block will be executed.

        // If there is a TransparentColor texture, we use it directly as the opacity channel
        if (transparentColor.image >= 0) {
            inputTranslator.translateDirect(transparentColor, um.opacity);
        } else {
            // There are FBX files where both the Opacity and TransparencyFactor properties are
            // present (even though the Opacity property has been phased out and is not defined as a
            // property of FbxSurfaceLambert). In some cases, both properties are present in the
            // material definition and so it's unclear which should be used. We use the
            // "TransparencyFactor" (ie 1.0) as is when both values are present and both equal 1.0.
            // Otherwise, we convert TransparencyFactor to an opacity value by computing 1.0 -
            // TransparencyFactor
            FbxProperty opacityProp = material->FindProperty("Opacity", FbxDoubleDT, true);
            FbxProperty transparencyFactorProp =
              material->FindProperty("TransparencyFactor", FbxDoubleDT, true);
            if (opacityProp.IsValid() && transparencyFactorProp.IsValid() &&
                1.0 == opacityProp.Get<double>() && 1.0 == transparencyFactorProp.Get<double>()) {
                // Use the transparencyFactor as is and treat it like an opacity value
                inputTranslator.translateDirect(transparencyFactor, um.opacity);
            } else {
                // invert transparencyFactor and assign to usd opacity
                inputTranslator.translateTransparency2Opacity(transparencyFactor, um.opacity);
            }
        }

        inputTranslator.translateNormals(bump, normal, um.normal);
    }
    ctx.usd->images = std::move(inputTranslator.getImages());
}

bool
importFbxMarker(ImportFbxContext& ctx, FbxNodeAttribute* attribute, int parent)
{
    return true;
}
bool
importFbxNurbs(ImportFbxContext& ctx, FbxNodeAttribute* attribute, int parent)
{
    return true;
}
bool
importFbxPatch(ImportFbxContext& ctx, FbxNodeAttribute* attribute, int parent)
{
    return true;
}
bool
importFbxLight(ImportFbxContext& ctx, FbxNodeAttribute* attribute, int parent)
{
    FbxLight* fbxLight = FbxCast<FbxLight>(attribute);
    if (!fbxLight) {
        // FbxCast is safe, so if it fails, it returns a null pointer. If that happens, that means
        // the attribute is not a light, even though this function should only be called on
        // attributes with GetAttributeType() == FbxNodeAttribute::eLight
        TF_WARN("importFbxLight: Non-light FBX node attribute cannot be processed as a light.");
        return false;
    }

    std::string type;
    LightType usdType;
    float coneAngle = 0;
    float coneFalloff = 0;
    float radius = 0.5;
    switch (fbxLight->LightType.Get()) {
        case FbxLight::ePoint:
            type = "sphere (from FBX point light)";
            usdType = LightType::Sphere;

            radius = DEFAULT_POINT_LIGHT_RADIUS;

            break;
        case FbxLight::eDirectional:
            type = "sun (from FBX directional light)";
            usdType = LightType::Sun;

            break;
        case FbxLight::eSpot:
            type = "disk (from FBX spot light)";
            usdType = LightType::Disk;

            radius = DEFAULT_SPOT_LIGHT_RADIUS;

            // FBX inner cone angle is from the center to where falloff begins, and outer cone
            // angle is from the center to where falloff ends. Meanwhile, in USD, angle is from
            // the center to the edge of the cone, and softness is a number from 0 to 1 indicating
            // how close to the center the falloff begins.

            // USD's cone angle is the entire shape of the spot light, corresponding to FBX's
            // outer angle
            coneAngle = fbxLight->OuterAngle.Get();

            // Get the fraction of the cone containing the falloff
            if (fbxLight->OuterAngle.Get()) {
                coneFalloff = 1 - (fbxLight->InnerAngle.Get() / fbxLight->OuterAngle.Get());
            }

            break;
        case FbxLight::eArea:
            TF_WARN("importFbxLight: ignoring unsupported light of type \"area\"\n");

            return false;
        case FbxLight::eVolume:
            TF_WARN("importFbxLight: ignoring unsupported light of type \"volume\"\n");

            return false;
        default:
            TF_WARN("importFbxLight: ignoring light of unknown type\n");

            return false;
    }

    auto [lightIndex, light] = ctx.usd->addLight();
    auto [nodeIndex, node] = ctx.usd->getParent(parent);
    node.light = lightIndex;

    light.type = usdType;
    light.coneAngle = coneAngle;
    light.coneFalloff = coneFalloff;

    light.name = fbxLight->GetName();
    light.color = toVec3f(fbxLight->Color.Get());
    light.intensity = fbxLight->Intensity.Get() * FBX_TO_USD_INTENSITY_SCALE_FACTOR;

    // Account for FBX's different coordinate system, and take the inverse on import. See comment
    // at definition of LIGHT_ROTATION_OFFSET_EXPORT for more information
    GfRotation rotationOffset = GfRotation(toQuatf(LIGHT_ROTATION_OFFSET_EXPORT).GetInverse());

    auto reorientLight = [rotationOffset](const GfQuatf rotation) {
        return GfQuatf((rotationOffset * GfRotation(rotation)).GetQuat());
    };

    // Reorient the light's rotation. Usually, light animations are done by animating the
    // parent of the light, but in case the light itself is animated, update those rotations
    // as well
    node.rotation = reorientLight(node.rotation);
    for (NodeAnimation& nodeAnimation : node.animations) {
        for (size_t rotationIdx = 0; rotationIdx < nodeAnimation.rotations.values.size();
             ++rotationIdx) {
            nodeAnimation.rotations.values[rotationIdx] =
              reorientLight(nodeAnimation.rotations.values[rotationIdx]);
        }
    }

    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "importFbx: light[%d]{ %s } of type %s\n",
                 lightIndex,
                 light.name.c_str(),
                 type.c_str());

    // TODO: Extract FBX light radius and replace this temporary dummy value with it. When this is
    // updated, please update corresponding unit tests as well
    light.radius = radius;
    TF_WARN("importFbxLight: ignoring FBX light radius for light of type %s, setting radius=%f\n",
            type.c_str(),
            radius);

    return true;
}
bool
importFbxLOD(ImportFbxContext& ctx, FbxNodeAttribute* attribute, int parent)
{
    return true;
}

static float inch2mm = 25.4f;
bool
importFbxCamera(ImportFbxContext& ctx, FbxNodeAttribute* attribute, int parent)
{
    std::string name = "";
    FbxCamera* fbxCamera = FbxCast<FbxCamera>(attribute);
    if (!fbxCamera) {
        // FbxCast is safe, so if it fails, it returns a null pointer. If that happens, that means
        // the attribute is not a camera, even though this function should only be called on
        // attributes with GetAttributeType() == FbxNodeAttribute::eCamera
        TF_WARN("importFbxCamera: Non-camera FBX node attribute cannot be processed as a camera.");
        return false;
    }
    auto [cameraIndex, camera] = ctx.usd->addCamera();
    auto [nodeIndex, node] = ctx.usd->getParent(parent);
    camera.name = fbxCamera->GetName();
    node.camera = cameraIndex;

    // If the camera doesn't have a specific look-at target, we need to compensate
    // for the default orientation of the fbx camera looking down the X axis.
    FbxNode* cameraNode = fbxCamera->GetNode();
    if (cameraNode && !cameraNode->GetTarget()) {
        // Account for FBX's different coordinate system, and take the inverse on import. See
        // comment at definition of CAMERA_ROTATION_OFFSET_EXPORT for more information
        // Note that the rotation order matters
        auto reorientCamera = [](const GfQuatf rotation) {
            GfRotation rotationOffset = GfRotation(toQuatf(CAMERA_ROTATION_OFFSET_EXPORT));
            return GfQuatf((rotationOffset.GetInverse() * GfRotation(rotation)).GetQuat());
        };

        // Reorient the camera's rotation. Usually, camera animations are done by animating the
        // parent of the camera, but in case the camera itself is animated, update those rotations
        // as well
        node.rotation = reorientCamera(node.rotation);
        for (NodeAnimation& nodeAnimation : node.animations) {
            for (size_t rotationIdx = 0; rotationIdx < nodeAnimation.rotations.values.size();
                 ++rotationIdx) {
                nodeAnimation.rotations.values[rotationIdx] =
                  reorientCamera(nodeAnimation.rotations.values[rotationIdx]);
            }
        }
    }

    camera.nearZ = fbxCamera->GetNearPlane();
    camera.farZ = fbxCamera->GetFarPlane();

    camera.camera.SetClippingRange(GfRange1f(camera.nearZ, camera.farZ));
    if (fbxCamera->ProjectionType.Get() == FbxCamera::EProjectionType::ePerspective) {
        // Could this fbx stuff be used for something?
        // float ar;
        // float aw = fbxCamera->AspectWidth.Get();
        // float ah = fbxCamera->AspectHeight.Get();
        // FbxCamera::EFormat cameraFormat = fbxCamera->GetFormat();
        // switch(cameraFormat) {
        // case FbxCamera::eNTSC:          ar =  640.0f /  480.0f; break;
        // case FbxCamera::eD1NTSC:        ar =  720.0f /  486.0f; break;
        // case FbxCamera::ePAL:           ar =  570.0f /  486.0f; break;
        // case FbxCamera::eD1PAL:         ar =  720.0f /  576.0f; break;
        // case FbxCamera::eHD:            ar = 1980.0f / 1080.0f; break;
        // case FbxCamera::e640x480:       ar =  640.0f /  480.0f; break;
        // case FbxCamera::e320x200:       ar =  320.0f /  200.0f; break;
        // case FbxCamera::e320x240:       ar =  320.0f /  240.0f; break;
        // case FbxCamera::e128x128:       ar =  128.0f /  128.0f; break;
        // case FbxCamera::eFullscreen:    ar = 1280.0f / 1024.0f; break;
        // case FbxCamera::eCustomFormat:  ar =      aw /      ah; break;
        // default:
        // break;
        // }
        // FbxCamera::EAspectRatioMode aspectRatioMode = fbxCamera->GetAspectRatioMode();
        // switch(aspectRatioMode) {
        // case FbxCamera::EAspectRatioMode::eFixedHeight:     ar = aw;      break;
        // case FbxCamera::EAspectRatioMode::eFixedWidth:      ar = 1 / ah;  break;
        // case FbxCamera::EAspectRatioMode::eWindowSize:      ar = 1;       break;
        // case FbxCamera::EAspectRatioMode::eFixedRatio:      ar = aw;      break;
        // case FbxCamera::EAspectRatioMode::eFixedResolution: ar = aw / ah; break;
        // }
        float apW = fbxCamera->GetApertureWidth();
        float apH = fbxCamera->GetApertureHeight();
        float f = fbxCamera->FocalLength.Get();
        float fovX = fbxCamera->FieldOfViewX.Get();
        // float fovY = fbxCamera->FieldOfViewY.Get();
        float fov = fbxCamera->FieldOfView.Get();
        FbxCamera::EApertureFormat apertureFormat = fbxCamera->GetApertureFormat();

        switch (apertureFormat) {
            case FbxCamera::eCustomAperture:
                break;
            case FbxCamera::e16mmTheatrical:
                apW = 0.4040;
                apH = 0.2950;
                break;
            case FbxCamera::eSuper16mm:
                apW = 0.4930;
                apH = 0.2920;
                break;
            case FbxCamera::e35mmAcademy:
                apW = 0.8640;
                apH = 0.6300;
                break;
            case FbxCamera::e35mmTVProjection:
                apW = 0.8160;
                apH = 0.6120;
                break;
            case FbxCamera::e35mmFullAperture:
                apW = 0.9800;
                apH = 0.7350;
                break;
            case FbxCamera::e35mm185Projection:
                apW = 0.8250;
                apH = 0.4460;
                break;
            case FbxCamera::e35mmAnamorphic:
                apW = 0.8640;
                apH = 0.7320;
                break;
            case FbxCamera::e70mmProjection:
                apW = 2.0660;
                apH = 0.9060;
                break;
            case FbxCamera::eVistaVision:
                apW = 1.4850;
                apH = 0.9910;
                break;
            case FbxCamera::eDynaVision:
                apW = 2.0800;
                apH = 1.4800;
                break;
            case FbxCamera::eIMAX:
                apW = 2.7720;
                apH = 2.0720;
                break;
            default:
                apW = 1;
                apH = 1;
                break;
        }
        // Fbx oddities wrt which field if actually true. Taken from fbx camera sample
        FbxCamera::EApertureMode apertureMode = fbxCamera->GetApertureMode();
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbxCamera: AperatureMode: %d\n", apertureMode);
        switch (apertureMode) {
            case FbxCamera::eHorizAndVert:
                f = fbxCamera->ComputeFocalLength(fovX);
                break;
            case FbxCamera::eHorizontal:
                f = fbxCamera->ComputeFocalLength(fov);
                break;
            case FbxCamera::eVertical:
                f = fbxCamera->ComputeFocalLength(fov);
                break;
            case FbxCamera::eFocalLength:
                fov = fbxCamera->ComputeFieldOfView(f);
                break;
            default:
                break;
        }
        apW *= inch2mm;
        apH *= inch2mm;
        camera.projection = GfCamera::Projection::Perspective;
        camera.camera.SetProjection(GfCamera::Projection::Perspective);
        camera.fov = fov;
        camera.f = f;                    // focal length in mm
        camera.horizontalAperture = apW; // aperture in mm
        camera.verticalAperture = apH;   // aperture in mm
    } else {
        float f = fbxCamera->FocalLength.Get();
        float fov = fbxCamera->FieldOfView.Get();
        float orthoZoom = fbxCamera->OrthoZoom.Get();

        // For fbx, we need to scale the orthoZoom value by 30.0 to get a proper orthoscale value
        // See here:
        // https://forums.autodesk.com/t5/fbx-forum/how-do-i-get-the-quot-orthographic-width-quot-for-a-camera/td-p/4227903
        // for some relevent background.
        orthoZoom *= 30.0f;

        float aspectRatio = 1.0f;
        // Note that SetOrthographicFromAspectRatioAndSize will divide orthoZoom by the
        // GfCamera::APERTURE_UNIT to get the vertical aperture (ie, converting from cm to mm)
        // so we'll need to apply the inverse of that later when exporting.
        camera.camera.SetOrthographicFromAspectRatioAndSize(
          aspectRatio, orthoZoom, GfCamera::FOVHorizontal);
        camera.camera.SetFocusDistance(orthoZoom);

        camera.projection = GfCamera::Projection::Orthographic;
        camera.fov = fov;
        camera.aspectRatio = aspectRatio;
        camera.f = f;
        camera.horizontalAperture = camera.camera.GetHorizontalAperture();
        camera.verticalAperture = camera.camera.GetVerticalAperture();
    }
    return true;
}

bool
importFbxUnknown(ImportFbxContext& ctx, FbxNodeAttribute* attribute, int parent)
{
    return true;
}

// This must happen before importing skeletons, since skeletons will need to read animated data.
bool
loadAnimLayers(ImportFbxContext& ctx)
{
    int animStackCount = ctx.scene->GetSrcObjectCount<FbxAnimStack>();
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: Animation stack count: %d \n", animStackCount);
    if (animStackCount == 0) {
        return true;
    }

    for (int i = 0; i < animStackCount; i++) {
        FbxAnimStack* stack = ctx.scene->GetSrcObject<FbxAnimStack>(i);
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "loadAnimLayers: Animation stack: %s\n", stack->GetName());
        FbxTime localStart = stack->LocalStart.Get();
        FbxTime localStop = stack->LocalStop.Get();
        double localStartSeconds = localStart.GetSecondDouble();
        double localStopSeconds = localStop.GetSecondDouble();

        AnimationTrack track;
        track.name = stack->GetName();
        track.minTime = localStartSeconds;
        track.maxTime = localStopSeconds;

        // FBX time unit is seconds so we set the USD timeCodesPerSecond to 1.0.
        ctx.usd->timeCodesPerSecond = 1;
        ctx.usd->animationTracks.push_back(track);

        size_t animLayersCount = stack->GetMemberCount<FbxAnimLayer>();
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: Animation stack: %s \n", stack->GetName());
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: \tLocalStart: %f s \n", localStartSeconds);
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: \tLocalStop: %f s \n", localStopSeconds);
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: \tanimLayersCount: %zu\n", animLayersCount);

        ctx.animationStacks.push_back(ImportedFbxStack());
        ImportedFbxStack& fbxStack = ctx.animationStacks.back();
        fbxStack.stack = stack;
        fbxStack.name = stack->GetName();

        for (size_t animLayerIndex = 0; animLayerIndex < animLayersCount; animLayerIndex++) {
            FbxAnimLayer* layer = stack->GetMember<FbxAnimLayer>(animLayerIndex);
            fbxStack.animLayers.push_back(layer);
            TF_DEBUG_MSG(
              FILE_FORMAT_FBX, "importFbx: found animation layer: %s \n", layer->GetName());
        }
    }
    return true;
}

static void
addAnimCurveFrameTimes(const FbxAnimCurve* curve, std::set<FbxTime>& frames)
{
    if (curve != nullptr) {
        int keyCount = curve->KeyGetCount();
        for (int i = 0; i < keyCount; i++) {
            FbxAnimCurveKey animKey = curve->KeyGet(i);
            FbxTime time = animKey.GetTime();
            frames.insert(time);
        }
    }
}

bool
isFbxSkeletonNode(FbxNode* node)
{
    for (int i = 0; i < node->GetNodeAttributeCount(); i++) {
        FbxNodeAttribute* attribute = node->GetNodeAttributeByIndex(i);
        if (attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton) {
            return true;
        }
    }
    return false;
}

bool
importFbxSkeleton(ImportFbxContext& ctx, const ImportedFbxSkeleton& importedSkeleton)
{
    auto [skeletonIndex, skeleton] = ctx.usd->addSkeleton();

    std::vector<std::set<FbxTime>> framesInEachStack;
    framesInEachStack.resize(ctx.animationStacks.size());

    std::vector<std::pair<FbxNode*, TfToken>> animatedNodes;

    size_t jointCount = 0;

    std::function<void(
      size_t skeletonIndex, Skeleton & skeleton, FbxNode * fbxNode, const SdfPath& parentPath)>
      importFbxBone;
    importFbxBone = [&](size_t skeletonIndex,
                        Skeleton& skeleton,
                        FbxNode* fbxNode,
                        const SdfPath& parentPath) {
        // Make sure it has a skeleton attribute
        if (!isFbxSkeletonNode(fbxNode))
            return;

        size_t jointIndex = jointCount++;

        ctx.skeletonsMap[fbxNode] = skeletonIndex;
        ctx.bonesMap[fbxNode] = jointIndex;

        TfToken stem("n" + std::to_string(jointIndex));
        SdfPath jointPath = parentPath.IsEmpty() ? SdfPath(stem) : parentPath.AppendChild(stem);
        TfToken jointPathToken = jointPath.GetAsToken();

        FbxAMatrix localTransform;
        FbxAMatrix globalTransform;
        {
            ScopedAnimStackDisabler animStackDisabler(ctx);
            localTransform = fbxNode->EvaluateLocalTransform();
            globalTransform = fbxNode->EvaluateGlobalTransform();
        }
        skeleton.joints.push_back(jointPathToken);
        skeleton.jointNames.push_back(stem);
        skeleton.restTransforms.push_back(GetUSDMatrixFromFBX(localTransform));

        // The bindTransforms will be updated later when the skelelon clusters
        // are processed but we still set them using the default global joint transform.
        skeleton.bindTransforms.push_back(GetUSDMatrixFromFBX(globalTransform));

        // Here also register which nodes are animated,
        // and accumulate in a map the animation keys' times.
        if (fbxNode->LclRotation.IsAnimated() || fbxNode->LclTranslation.IsAnimated() ||
            fbxNode->LclScaling.IsAnimated()) {

            for (int animationStackIndex = 0; animationStackIndex < ctx.animationStacks.size();
                 animationStackIndex++) {
                const ImportedFbxStack& fbxStack = ctx.animationStacks[animationStackIndex];
                std::set<FbxTime>& frames = framesInEachStack[animationStackIndex];

                for (FbxAnimLayer* animLayer : fbxStack.animLayers) {
                    const FbxAnimCurve* translationCurve =
                      fbxNode->LclTranslation.GetCurve(animLayer);
                    const FbxAnimCurve* rotationCurve = fbxNode->LclRotation.GetCurve(animLayer);
                    const FbxAnimCurve* scalingCurve = fbxNode->LclScaling.GetCurve(animLayer);
                    addAnimCurveFrameTimes(translationCurve, frames);
                    addAnimCurveFrameTimes(rotationCurve, frames);
                    addAnimCurveFrameTimes(scalingCurve, frames);

                    if (translationCurve != nullptr || rotationCurve != nullptr ||
                        scalingCurve != nullptr) {
                        animatedNodes.emplace_back(fbxNode, jointPathToken);
                    }
                }
            }
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "Importing animation for bone %s \n", fbxNode->GetName());
        }

        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "import joint %s:\t %s\n",
                     fbxNode->GetName(),
                     skeleton.joints[jointIndex].GetText());
        for (int i = 0; i < fbxNode->GetChildCount(); i++) {
            FbxNode* childNode = fbxNode->GetChild(i);
            if (childNode == nullptr) {
                TF_WARN(
                  "Child node at index %d is null for node '%s'. Skipping.", i, fbxNode->GetName());
                continue;
            }
            importFbxBone(skeletonIndex, skeleton, childNode, jointPath);
        }
    };

    // There may be multiple root joints so add each root to skeleton
    for (auto skel : importedSkeleton.fbxSkeletons) {
        importFbxBone(skeletonIndex, skeleton, skel->GetNode(), SdfPath());
    }

    for (const auto& i : animatedNodes) {
        skeleton.animatedJoints.push_back(i.second);
    }

    for (int animationStackIndex = 0; animationStackIndex < framesInEachStack.size();
         animationStackIndex++) {
        AnimationTrack& track = ctx.usd->animationTracks[animationStackIndex];

        // Set the current animation stack so that EvaluateLocalTransform will return the correct
        // value
        ctx.scene->SetCurrentAnimationStack(ctx.animationStacks[animationStackIndex].stack);

        std::set<FbxTime>& frames = framesInEachStack[animationStackIndex];
        if (!frames.empty()) {
            track.hasTimepoints = true;
            ctx.usd->hasAnimations = true;

            TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: assembling animation data\n");
            skeleton.skeletonAnimations.resize(framesInEachStack.size());
            SkeletonAnimation& skeletonAnimation = skeleton.skeletonAnimations[animationStackIndex];
            skeletonAnimation.times.resize(frames.size());
            skeletonAnimation.translations.resize(frames.size(),
                                                  VtArray<GfVec3f>(animatedNodes.size()));
            skeletonAnimation.rotations.resize(frames.size(),
                                               VtArray<GfQuatf>(animatedNodes.size()));
            skeletonAnimation.scales.resize(frames.size(), VtArray<GfVec3h>(animatedNodes.size()));
            size_t i = 0;
            for (const auto& nodePair : animatedNodes) {
                FbxNode* fbxNode = nodePair.first;
                size_t j = 0;
                for (const FbxTime& frameTime : frames) {
                    FbxAMatrix localTransform = fbxNode->EvaluateLocalTransform(frameTime);
                    GfMatrix4d usdLocalTransform =
                      ConvertMatrix4<FbxAMatrix, GfMatrix4d>(localTransform);
                    GfVec3f translation;
                    GfQuatf rotation;
                    GfVec3h scale;
                    UsdSkelDecomposeTransform(usdLocalTransform, &translation, &rotation, &scale);
                    skeletonAnimation.times[j] = frameTime.GetSecondDouble();
                    skeletonAnimation.translations[j][i] = translation;
                    skeletonAnimation.rotations[j][i] = rotation;
                    skeletonAnimation.scales[j][i] = scale;
                    j++;
                }
                i++;
            }
        }
    }

    return true;
}

// Import skeletons from fbx.
// The only way of recognizing a skeleton is to check whether an fbx node has a skeleton attribute.
// So traverse all nodes here, but only look at the skeleton roots for further processing.
bool
importFBXSkeletons(ImportFbxContext& ctx)
{
    int skeletonCount = ctx.scene->GetSrcObjectCount<FbxSkeleton>();

    // Build a mapping of skeleton root parent nodes to their children.
    // FBX supports multiple root nodes in a skeleton so we need to
    // aggregate the roots with common parents and process them as a single
    // skeleton.
    for (int i = 0; i < skeletonCount; i++) {
        FbxSkeleton* fbxSkeleton = ctx.scene->GetSrcObject<FbxSkeleton>(i);
        if (fbxSkeleton->IsSkeletonRoot()) {
            FbxNode* node = fbxSkeleton->GetNode();
            if (node != nullptr) {
                FbxNode* parent = node->GetParent();
                auto it = std::find_if(ctx.skeletons.begin(),
                                       ctx.skeletons.end(),
                                       [parent](const ImportedFbxSkeleton& skeleton) {
                                           return skeleton.fbxParent == parent;
                                       });

                if (it == ctx.skeletons.end()) {
                    ImportedFbxSkeleton skeleton;
                    skeleton.fbxParent = parent;
                    skeleton.fbxSkeletons.push_back(fbxSkeleton);
                    ctx.skeletons.emplace_back(std::move(skeleton));
                } else {
                    it->fbxSkeletons.push_back(fbxSkeleton);
                }
            } else {
                TF_WARN("importFBXSkeletons: Skeleton root node is null");
            }
        }
    }

    for (const ImportedFbxSkeleton& skeleton : ctx.skeletons) {
        importFbxSkeleton(ctx, skeleton);
    }

    return true;
}

void
setSkeletonParents(ImportFbxContext& ctx)
{
    int skeletonIndex = 0;
    for (const ImportedFbxSkeleton& skeleton : ctx.skeletons) {
        int parentIndex = ctx.nodeMap[skeleton.fbxParent];
        ctx.usd->skeletons[skeletonIndex].parent = parentIndex;
        skeletonIndex++;
    }
}

bool
isSkinnedMesh(const FbxMesh* fbxMesh)
{
    int skinCount = fbxMesh->GetDeformerCount(FbxDeformer::eSkin);
    if (skinCount > 0) {
        FbxSkin* skin = FbxCast<FbxSkin>(fbxMesh->GetDeformer(0, FbxDeformer::eSkin));
        if (skin->GetClusterCount() > 0)
            return true;
    }
    return false;
}

void
importFbxNodes(ImportFbxContext& ctx, FbxNode* fbxNode, int parent)
{
    auto [nodeIndex, node] = ctx.usd->addNode(parent);
    node.name = fbxNode->GetName();

    if (!fbxNode->GetVisibility()) {
        node.markedInvisible = true;
    }
    if (!fbxNode->VisibilityInheritance.Get()) { // True by default
        TF_WARN("importFbxNodes: Node %s does not inherit visibility (VisibilityInheritance = "
                "false). This is currently unsupported. The node is set as %s",
                fbxNode->GetName(),
                node.markedInvisible ? "invisible" : "visible");
    }

    ctx.nodeMap[fbxNode] = nodeIndex;

    TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: node %s\n", node.name.c_str());
    GfVec3d t(0);
    GfQuatf r(0);
    GfVec3f s(1);

    // It is rare for the root node to contain a transform, but in case it does, we use the global
    // transform to import the transform of each child of the root, given that we skipped importing
    // the root node itself.
    const bool useGlobalTransform = (parent == -1);
    importFbxTransform(ctx, fbxNode, node, t, r, s, useGlobalTransform);

    // Fbx nodes have additional 'Geometric TRS' data, which are applied to its node attributes
    // but not to its children nodes. So if these are found, we insert a subnode here.
    // The subnode will parent any node attributes, but not any children of the original node.
    int subNodeIndex = -1;
    if (t != GfVec3d(0) || r != GfQuatf(0) || s != GfVec3f(1)) {
        auto [subIndex, subNode] = ctx.usd->addNode(nodeIndex);
        subNode.name = std::string("sub") + fbxNode->GetName();
        if (t != GfVec3d(0)) {
            subNode.translation = t;
        }
        if (r != GfQuatf(0)) {
            subNode.rotation = r;
        }
        if (s != GfVec3f(1)) {
            subNode.scale = s;
        }
        subNodeIndex = subIndex;
    }
    int parentIndex = subNodeIndex == -1 ? nodeIndex : subNodeIndex;

    // Import the node attributes
    for (int i = 0; i < fbxNode->GetNodeAttributeCount(); i++) {
        FbxNodeAttribute* attribute = fbxNode->GetNodeAttributeByIndex(i);
        if (attribute == nullptr) {
            TF_WARN(
              "Attribute at index %d is null for node '%s'. Skipping.", i, fbxNode->GetName());
            continue;
        }
        auto attrType = attribute->GetAttributeType();
        switch (attrType) {
            case FbxNodeAttribute::eMesh: {
                FbxMesh* fbxMesh = FbxCast<FbxMesh>(attribute);
                if (fbxMesh != nullptr) {
                    // If the mesh is skinned, we clear the transform as it will be placed
                    // at the root of the scene.

                    // XXX There are still issues with importing FBX skinned meshes that do not live
                    // at the root level which needs to be addressed. USD wants the skinned mesh to
                    // be placed next to the skeleton and GLTF wants skinned meshes to be at the
                    // root level. FBX maps the world space skeletal transformations to the local
                    // local space of the mesh by applying the inv(localToWorld) of the mesh to the
                    // skeleton's parentToWorld matrix. It is not yet understood how to handle this
                    // with the FBX->USD conversion. This results in the mesh missing the
                    // transformation from skeletal space to world space.
                    if (isSkinnedMesh(fbxMesh)) {
                        node.transform = GfMatrix4d(1);
                        node.hasTransform = false;
                    }
                    importFbxMesh(ctx, fbxMesh, parentIndex);
                } else {
                    TF_WARN("importFbx: fbxmesh was NULL");
                }
            } break;
            case FbxNodeAttribute::eMarker:
                importFbxMarker(ctx, attribute, parentIndex);
                break;
            case FbxNodeAttribute::eNurbs:
                importFbxNurbs(ctx, attribute, parentIndex);
                break;
            case FbxNodeAttribute::ePatch:
                importFbxPatch(ctx, attribute, parentIndex);
                break;
            case FbxNodeAttribute::eCamera:
                importFbxCamera(ctx, attribute, parentIndex);
                break;
            case FbxNodeAttribute::eLight:
                importFbxLight(ctx, attribute, parentIndex);
                break;
            case FbxNodeAttribute::eLODGroup:
                importFbxLOD(ctx, attribute, parentIndex);
                break;
            default:
                importFbxUnknown(ctx, attribute, parentIndex);
                break;
        }
    }

    for (int i = 0; i < fbxNode->GetChildCount(); i++) {
        FbxNode* childNode = fbxNode->GetChild(i);
        if (childNode == nullptr) {
            TF_WARN(
              "Child node at index %d is null for node '%s'. Skipping.", i, fbxNode->GetName());
            continue;
        }
        importFbxNodes(ctx, childNode, nodeIndex);
    }
}

void
importFbxNodeHierarchy(ImportFbxContext& ctx)
{
    FbxNode* fbxNode = ctx.scene->GetRootNode();

    // Call importFbxNodes once for each child of the root node. We do not import the root node
    // itself, as this node is created by the FBX to act as a container for the other nodes.
    // Skipping it will ensure each child ends up as a one of the rootNodes in the UsdData. The root
    // node itself would not be expected to have any attributes, so it should be ok to skip the
    // attribute import process on it. It is also rare for the root node to contain a transform, but
    // in case it does, we use the global transform to import the transform of each child.
    for (int i = 0; i < fbxNode->GetChildCount(); i++) {
        FbxNode* childNode = fbxNode->GetChild(i);
        if (childNode == nullptr) {
            TF_WARN(
              "Child node at index %d is null for node '%s'. Skipping.", i, fbxNode->GetName());
            continue;
        }
        importFbxNodes(ctx, childNode, -1);
    }
}

// Before converting meshes from Fbx to USD, we first triangulate
// any meshes that have edge information which defines a specific
// triangulation (ie. the splitting of quads). We don't pre-triangulate
// meshes that don't have edge information.
void
triangulateMeshes(ImportFbxContext& ctx)
{
    size_t meshCount = ctx.scene->GetSrcObjectCount<FbxMesh>();
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: Total meshes:%zu\n", meshCount);
    if (meshCount == 0)
        return;

    std::vector<FbxMesh*> meshes;
    meshes.reserve(meshCount);

    // Collect meshes with non-zero edge counts. We will triangle only those
    // as the edge information is relevent to the triangulation.
    // We can't triangulate in this loop because triangulation affects the
    // ordering of meshes.
    for (size_t i = 0; i < meshCount; ++i) {
        FbxMesh* mesh = ctx.scene->GetSrcObject<FbxMesh>(i);
        size_t polyCount = mesh->GetPolygonCount();
        size_t edgeCount = mesh->GetMeshEdgeCount();
        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "importFbx: mesh[%lu]=%s polycount=%lu edgecount=%lu\n",
                     i,
                     mesh->GetName(),
                     polyCount,
                     edgeCount);
        if (edgeCount > 0) {
            meshes.push_back(mesh);
        }
    }

    if (meshes.size() > 0) {
        FbxGeometryConverter conv(ctx.fbx->manager);

        // triangulate each mesh
        for (auto mesh : meshes) {
            // We use the legacy triangulation algorithm because crashes have been occuring
            // when using the newer algorithm.
            conv.Triangulate(mesh, /* pReplace = */ true, /* pLegacy = */ true);
        }
    }
}

bool
importFbx(const ImportFbxOptions& options, Fbx& fbx, UsdData& usd)
{
    ImportFbxContext ctx;
    ctx.options = &options;
    ctx.usd = &usd;
    ctx.fbx = &fbx;
    ctx.scene = fbx.scene;
    ctx.originalColorSpace = options.originalColorSpace;

    // Include the FBX file name itself in the filenames we add to the metadata
    {
        std::string baseName = TfGetBaseName(fbx.filename);
        usd.importedFileNames.emplace(std::move(baseName));
    }

    importMetadata(ctx);
    importFbxSettings(ctx);

    importMeshUVSets(ctx);

    if (options.importMaterials) {
        importFbxMaterials(ctx);
    }
    if (options.importGeometry) {
        triangulateMeshes(ctx);
        loadAnimLayers(ctx);
        importFBXSkeletons(ctx);
        importFbxNodeHierarchy(ctx);
        setSkeletonParents(ctx);
    }

    return true;
}
}
