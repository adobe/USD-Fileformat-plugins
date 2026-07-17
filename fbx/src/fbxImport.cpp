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
#include <algorithm>
#include <cctype>
#include <fileformatutils/common.h>
#include <fileformatutils/featureFlags.h>
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
    FbxAMatrix parentGlobalInverse;
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
    // Tracks which skeleton nodes have animations and will be handled by skeletal animation system
    std::unordered_set<FbxNode*> animatedSkeletonNodes;

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
    std::string generator = "Adobe usdFbx 1.0";

    FbxDocumentInfo* docInfo = ctx.fbx->scene->GetDocumentInfo();
    if (docInfo) {
        FbxString origAppName = docInfo->Original_ApplicationName.Get();

        // If the FBX specified a generator, add it to the USD generator string
        if (!origAppName.IsEmpty()) {
            generator += "; FBX generator: ";
            generator += origAppName.Buffer();
        }
    }

    ctx.usd->metadata.SetValueAtPath("generator", PXR_NS::VtValue(generator));
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
    // Returns true when any scale component is near-zero.  We check both the
    // FBX property directly AND the decomposed scale because:
    //  - UsdSkelDecomposeTransform may return non-zero scale for degenerate
    //    matrices on some USD versions (public OpenUSD)
    //  - Real FBX files may have zero effective scale from animation keys or
    //    parent inheritance rather than the static LclScaling property
    auto hasNearZeroFbxScale = [](const FbxDouble3& s) {
        constexpr double kMinScaleSq = 1e-12;
        return s[0] * s[0] < kMinScaleSq || s[1] * s[1] < kMinScaleSq || s[2] * s[2] < kMinScaleSq;
    };
    auto hasNearZeroDecomposedScale = [](const GfVec3f& s) {
        constexpr double kMinScaleSq = 1e-12;
        return s[0] * s[0] < kMinScaleSq || s[1] * s[1] < kMinScaleSq || s[2] * s[2] < kMinScaleSq;
    };

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

    // Only import regular node animations if this is not an animated skeleton node
    // Animated skeleton nodes get their animations from the skeletal animation system
    bool isAnimatedSkeletonNode =
      (ctx.animatedSkeletonNodes.find(fbxNode) != ctx.animatedSkeletonNodes.end());
    if (!isAnimatedSkeletonNode) {
        for (int animationStackIndex = 0;
             animationStackIndex < static_cast<int>(ctx.animationStacks.size());
             animationStackIndex++) {
            // Set the current animation stack so that EvaluateLocalTransform will return the
            // correct value
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

                    // Usually the curve has animation data, but sometimes it is null but at least
                    // one of the curveNode's channels do. For this reason, we check them all
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

                // Near-zero scale makes the composed matrix singular, so the
                // decomposed rotation is unreliable.  Fall back to FBX's
                // separate rotation channel.
                if (hasNearZeroFbxScale(fbxNode->LclScaling.EvaluateValue(keyFrameTime)) ||
                    hasNearZeroDecomposedScale(scale)) {
                    rotation = toQuatf(fbxNode->LclRotation.EvaluateValue(keyFrameTime));
                    rotation.Normalize();
                }

                nodeAnimation.translations.times.push_back(time);
                nodeAnimation.translations.values.push_back(translation);

                nodeAnimation.rotations.times.push_back(time);
                nodeAnimation.rotations.values.push_back(rotation);

                nodeAnimation.scales.times.push_back(time);
                nodeAnimation.scales.values.push_back(scale);
            }
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

        // When any scale component is near-zero the composed matrix is singular and
        // UsdSkelDecomposeTransform produces an arbitrary rotation.  Fall back to
        // FBX's LclRotation channel which gives the authored local rotation
        // regardless of whether this is a root child or nested node.
        if (hasNearZeroFbxScale(fbxNode->LclScaling.Get()) || hasNearZeroDecomposedScale(scale)) {
            rotation = toQuatf(fbxNode->LclRotation.Get());
            rotation.Normalize();
        }
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

    // Tangents
    FbxGeometryElementTangent* tangentElement = fbxMesh->GetElementTangent();
    if (tangentElement != nullptr) {
        mesh.tangents.interpolation = fbxGetInterpolation(tangentElement->GetMappingMode());
        if (tangentElement->GetReferenceMode() == FbxGeometryElement::eDirect) {
            size_t tangentCount = tangentElement->GetDirectArray().GetCount();
            mesh.tangents.values.resize(tangentCount);
            for (size_t i = 0; i < tangentCount; i++) {
                FbxVector4 tangent = tangentElement->GetDirectArray().GetAt(i);
                mesh.tangents.values[i] = GfVec4f{ static_cast<float>(tangent[0]),
                                                   static_cast<float>(tangent[1]),
                                                   static_cast<float>(tangent[2]),
                                                   static_cast<float>(tangent[3]) };
            }
        } else { // FbxGeometryElement::eIndexToDirect
            size_t tangentCount = tangentElement->GetIndexArray().GetCount();
            mesh.tangents.values.resize(tangentCount);
            for (size_t i = 0; i < tangentCount; i++) {
                int tangentIndex = tangentElement->GetIndexArray().GetAt(i);
                FbxVector4 tangent = tangentElement->GetDirectArray().GetAt(tangentIndex);
                mesh.tangents.values[i] = GfVec4f{ static_cast<float>(tangent[0]),
                                                   static_cast<float>(tangent[1]),
                                                   static_cast<float>(tangent[2]),
                                                   static_cast<float>(tangent[3]) };
            }
        }
    }

    // Bitangents (read from FBX binormals)
    FbxGeometryElementBinormal* binormalElement = fbxMesh->GetElementBinormal();
    if (binormalElement != nullptr) {
        mesh.bitangents.interpolation = fbxGetInterpolation(binormalElement->GetMappingMode());
        if (binormalElement->GetReferenceMode() == FbxGeometryElement::eDirect) {
            size_t binormalCount = binormalElement->GetDirectArray().GetCount();
            mesh.bitangents.values.resize(binormalCount);
            for (size_t i = 0; i < binormalCount; i++) {
                FbxVector4 binormal = binormalElement->GetDirectArray().GetAt(i);
                mesh.bitangents.values[i] = GfVec3f{ static_cast<float>(binormal[0]),
                                                     static_cast<float>(binormal[1]),
                                                     static_cast<float>(binormal[2]) };
            }
        } else { // FbxGeometryElement::eIndexToDirect
            size_t binormalCount = binormalElement->GetIndexArray().GetCount();
            mesh.bitangents.values.resize(binormalCount);
            for (size_t i = 0; i < binormalCount; i++) {
                int binormalIndex = binormalElement->GetIndexArray().GetAt(i);
                FbxVector4 binormal = binormalElement->GetDirectArray().GetAt(binormalIndex);
                mesh.bitangents.values[i] = GfVec3f{ static_cast<float>(binormal[0]),
                                                     static_cast<float>(binormal[1]),
                                                     static_cast<float>(binormal[2]) };
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

            // Use find() instead of operator[] to avoid creating default entries
            // for nodes not in any skeleton, and validate skeleton index bounds
            auto skelIt = ctx.skeletonsMap.find(firstlink);
            if (skelIt == ctx.skeletonsMap.end()) {
                TF_WARN("Skin %d first link node '%s' not found in skeletons map\n",
                        i,
                        firstlink->GetName());
                continue;
            }
            size_t skeletonIndex = skelIt->second;

            // Validate skeleton index before accessing
            if (skeletonIndex >= ctx.usd->skeletons.size()) {
                TF_WARN("Skeleton index %zu out of bounds (size %zu) for skin %d\n",
                        skeletonIndex,
                        ctx.usd->skeletons.size(),
                        i);
                continue;
            }

            ctx.meshSkinsMap[meshIndex] = skeletonIndex;
            ctx.usd->skeletons[skeletonIndex].meshSkinningTargets.push_back(meshIndex);

            // set the mesh geomBindTransform based on the transform matrix
            // For some reason, FBX put this matrix on the cluster, but we should get the same
            // result no matter which cluster we look at.
            // Factor out the skeleton parent's global transform to make the bind transform
            // relative to the parent (armature) node.
            FbxAMatrix geomBindTransform;
            firstCluster->GetTransformMatrix(geomBindTransform);
            const FbxAMatrix& parentInv = ctx.skeletons[skeletonIndex].parentGlobalInverse;
            geomBindTransform = parentInv * geomBindTransform;
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

                // Use find() instead of operator[] to avoid creating default entries
                // for nodes not in the skeleton, and validate bounds before accessing
                // bindTransforms
                auto boneIt = ctx.bonesMap.find(link);
                if (boneIt == ctx.bonesMap.end()) {
                    TF_WARN("Cluster link node '%s' not found in skeleton bones map for skin %d "
                            "cluster %d\n",
                            link->GetName(),
                            i,
                            j);
                    continue;
                }
                size_t jointIndex = boneIt->second;

                // if the linkMode for any cluster is not eNormalize, then we will disable weight
                // normalization
                FbxCluster::ELinkMode clusterLinkMode = cluster->GetLinkMode();
                if (FbxCluster::ELinkMode::eNormalize != clusterLinkMode)
                    linkMode = clusterLinkMode;

                // Set the bindTransform for the joint, factoring out the skeleton parent's
                // global transform to avoid double-application through the USD hierarchy.
                FbxAMatrix linkTransform;
                cluster->GetTransformLinkMatrix(linkTransform);
                linkTransform = parentInv * linkTransform;

                // XXX In theory different meshes could have different link transforms in the case
                // where they share the same skeleton/links. If that can happen, we'd end up with a
                // conflict trying to share the skeleton as USD stores the bindTransform on the
                // skeleton, not on the mesh. We haven't seen this yet though, so we don't try to
                // un-share the skeletons in this case, and instead just ovewrite the bindTransforms

                // Validate jointIndex is within bounds before writing
                if (jointIndex >= skeleton.bindTransforms.size()) {
                    TF_WARN(
                      "Joint index %zu exceeds bind transforms size %zu for skin %d cluster %d\n",
                      jointIndex,
                      skeleton.bindTransforms.size(),
                      i,
                      j);
                    continue;
                }
                skeleton.bindTransforms[jointIndex] = GetUSDMatrixFromFBX(linkTransform);

                int clusterControlPointIndicesCount = cluster->GetControlPointIndicesCount();
                int* clusterControlPointIndices = cluster->GetControlPointIndices();
                double* pointsWeights = cluster->GetControlPointWeights();
                if (clusterControlPointIndices == nullptr) {
                    // This is normal for some meshes, so don't warn about it as it can spam the
                    // console
                    // TF_WARN("No cluster control point indices for skin cluster: %d.\n", j);
                    continue;
                }
                if (pointsWeights == nullptr) {
                    TF_WARN("No point weights for skin cluster: %d.\n", j);
                    continue;
                }
                for (int k = 0; k < clusterControlPointIndicesCount; k++) {
                    int controlPointIndex = clusterControlPointIndices[k];
                    // Use >= instead of > to prevent off-by-one out-of-bounds access
                    if (controlPointIndex < 0 ||
                        static_cast<size_t>(controlPointIndex) >= indexes.size() ||
                        static_cast<size_t>(controlPointIndex) >= weights.size()) {
                        TF_WARN("Control Point Index outside of index or weight bounds. index: %d "
                                " Index Size: %zu  Weight Size: %zu",
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
    // When a mesh is shared across multiple FBX nodes (e.g. C4D instances), GetNode() returns
    // the first connected node, which may be an instance without a material connection. Fall
    // back to the first parent node that actually has materials assigned.
    if (fbxNode != nullptr && fbxNode->GetMaterialCount() == 0) {
        int nodeCount = fbxMesh->GetNodeCount();
        for (int ni = 0; ni < nodeCount; ni++) {
            FbxNode* candidate = fbxMesh->GetNode(ni);
            if (candidate != nullptr && candidate->GetMaterialCount() > 0) {
                fbxNode = candidate;
                break;
            }
        }
    }
    if (fbxNode != nullptr) {
        int materialCount = fbxNode->GetMaterialCount();
        int elementMaterialCount = fbxMesh->GetElementMaterialCount();
        if (elementMaterialCount == 0 && materialCount > 0) {
            // If there are no element materials, FBX defaults to using the first material for the
            // whole mesh
            TF_DEBUG_MSG(FILE_FORMAT_FBX,
                         "Mesh[%s] has no material elements. Defaulting to use first material\n",
                         mesh.name.c_str());

            FbxSurfaceMaterial* fbxMaterial = fbxNode->GetMaterial(0);
            const auto& it = ctx.materials.find(fbxMaterial);
            if (it != ctx.materials.end()) {
                mesh.material = it->second;
            } else {
                TF_WARN("Mesh[%s] has no material element, and the (default) first material "
                        "could not be found. No materials will be linked\n",
                        mesh.name.c_str());
            }
        } else {
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
        if (su != 1.0 || sv != 1.0) {
            input.uvScale = GfVec2f(su, sv);
        }
        double rot = texture->GetRotationW();
        if (rot != 0.0) {
            input.uvRotation = rot;
        }
        double tu = texture->GetTranslationU();
        double tv = texture->GetTranslationV();
        if (tu != 0.0 || tv != 0.0) {
            input.uvTranslation = GfVec2f(tu, tv);
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
    bool convertToLinear = (ctx.originalColorSpace == AdobeTokens->sRGB);
    if (convertToLinear && colorSpace == AdobeTokens->sRGB) {
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
    // Set the colorspace annotation:
    // - If conversion happened, data is now linear (raw)
    // - If no conversion and originalColorSpace not set, use raw (unknown colorspace)
    // - Otherwise, keep the semantic colorspace annotation
    input.colorspace = convertToLinear ? AdobeTokens->raw : colorSpace;
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

// Inverse of _toCamelCase: convert a camelCase identifier to snake_case. Existing underscores are
// preserved, so a name that is already snake_case (e.g. 3ds Max's "base_color") passes through
// unchanged. An underscore is inserted before an uppercase letter that starts a new word, which
// also splits trailing acronyms (e.g. "specularIOR" -> "specular_ior").
std::string
_toSnakeCase(const std::string& s) noexcept
{
    std::string out;
    out.reserve(s.size() + 8);
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];
        if (std::isupper(c)) {
            const bool prevIsWord = i > 0 && (std::islower(static_cast<unsigned char>(s[i - 1])) ||
                                              std::isdigit(static_cast<unsigned char>(s[i - 1])));
            const bool nextIsLower =
              i + 1 < s.size() && std::islower(static_cast<unsigned char>(s[i + 1]));
            if (i > 0 && (prevIsWord || nextIsLower) && !out.empty() && out.back() != '_') {
                out.push_back('_');
            }
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
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
_mapAutodeskStandardMaterialOpenPbr(const FbxSurfaceMaterial* fbxMaterial,
                                    ImportFbxContext& ctx,
                                    const std::unordered_map<FbxObject*, size_t>& textures,
                                    OpenPbrMaterial& usdMaterial,
                                    InputTranslator& inputTranslator)
{
    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "Checking if %s is an Autodesk Standard Surface Material\n",
                 fbxMaterial->GetName());
    // Determine the effective colorspace for color properties based on the originalColorSpace
    // option. If originalColorSpace is set to sRGB, color data will be converted to linear and
    // stored as raw. If originalColorSpace is not set, no conversion happens and data is passed
    // through as raw (unknown colorspace - let the client application handle color management).
    const TfToken& colorPropertySpace =
      (ctx.originalColorSpace == AdobeTokens->sRGB) ? AdobeTokens->sRGB : AdobeTokens->raw;

    // This will contain the properties that are directly mapped from the standard surface exactly
    // as is.  We need to note if they are One or Three channels and the colorspace for later usage.
    std::unordered_map<std::string, std::tuple<Input&, FbxPropertyNumChannels, const TfToken&>>
      standardSurfToUsdProperty = {
          { "base_color",
            { usdMaterial.base_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "specular_color",
            { usdMaterial.specular_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "metalness",
            { usdMaterial.base_metalness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_roughness",
            { usdMaterial.specular_roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat", { usdMaterial.coat_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_color",
            { usdMaterial.coat_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "coat_roughness",
            { usdMaterial.coat_roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_IOR", { usdMaterial.coat_ior, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "sheen_color",
            { usdMaterial.fuzz_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "sheen_roughness",
            { usdMaterial.fuzz_roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_anisotropy",
            { usdMaterial.specular_roughness_anisotropy,
              FbxPropertyNumChannels::One,
              AdobeTokens->raw } },
          { "specular_rotation",
            { usdMaterial.anisotropyAngle, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_IOR",
            { usdMaterial.specular_ior, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission",
            { usdMaterial.transmission_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission_depth",
            { usdMaterial.transmission_depth, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission_color",
            { usdMaterial.transmission_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "subsurface_color",
            { usdMaterial.subsurface_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
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
        FbxProperty property = getProp(it.first);
        Input& input = std::get<0>(it.second);
        FbxPropertyNumChannels numChannels = std::get<1>(it.second);
        const TfToken& colorSpace = std::get<2>(it.second);
        if (numChannels == FbxPropertyNumChannels::One) {
            auto typedProp = static_cast<FbxPropertyT<FbxDouble>>(property);
            Input tempInput;
            importPropTexture(ctx, textures, fbxMaterial, typedProp, tempInput, "r", colorSpace);
            inputTranslator.translateDirect(tempInput, input);
        } else if (numChannels == FbxPropertyNumChannels::Three) {
            auto typedProp = static_cast<FbxPropertyT<FbxDouble3>>(property);
            Input tempInput;
            importPropTexture(ctx, textures, fbxMaterial, typedProp, tempInput, "rgb", colorSpace);
            inputTranslator.translateDirect(tempInput, input);
        } else {
            TF_CODING_ERROR("Unknown number of channels");
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
        inputTranslator.translateDirect(input, usdMaterial.geometry_normal);
    }

    auto coatNormalProperty = getProp(kCoatNormal);
    auto coatNormalTexture = FbxCast<FbxTexture>(coatNormalProperty.GetSrcObject());
    if (coatNormalTexture) {
        auto typedProp = static_cast<FbxPropertyT<FbxDouble3>>(coatNormalProperty);
        Input input;
        importPropTexture(ctx, textures, fbxMaterial, typedProp, input, "rgb", AdobeTokens->raw);
        inputTranslator.translateDirect(input, usdMaterial.geometry_coat_normal);
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
                      colorPropertySpace);

    // XXX I believe a more proper way to do this is to keep the emissive intensity as a
    // separate input because in UIs that use a color picker to modify this input you will lose
    // values over one upon modification.  This matches how GLTF handles it though currently, and I
    // think this also is an issue there as well
    inputTranslator.translateFactor(emissionColorInput, emissionInput, usdMaterial.emission_color);

    // Opacity in USD must be stored as a single value
    auto opacityProperty = getProp(kOpacity);
    if (opacityProperty.IsValid()) {
        auto opacityTypedProp = static_cast<FbxPropertyT<FbxDouble3>>(opacityProperty);
        GfVec3f opacityColor = readPropValue(opacityTypedProp);

        // Convert the opacity color to grayscale and use that as the opacity value
        float grayscaleOpacity = (opacityColor[0] + opacityColor[1] + opacityColor[2]) / 3.0f;
        usdMaterial.geometry_opacity.value = grayscaleOpacity;
        usdMaterial.geometry_opacity.colorspace = AdobeTokens->raw;
    }

    return true;
}

// Maya 2026 and 3ds Max 2026 both export native OpenPBR materials into FBX, and both emit the full
// canonical OpenPBR parameter set prefixed with a vendor token. They differ in the details:
//
//   - Maya ("openPBRSurface"): ShadingModel "openpbrsurface", properties under the "Maya|" compound
//     with camelCase leaves ("baseColor"), colors as Vector3D (FbxDouble3), and an
//     FbxImplementation whose binding table authoritatively maps each "Maya|<leaf>" to its
//     canonical OpenPBR name.
//   - 3ds Max: ShadingModel "unknown", properties under "3dsMax|Parameters|" with snake_case leaves
//     already matching the OpenPBR spec ("base_color"), colors as ColorAndAlpha (FbxDouble4), and
//     no binding table. Textures live on separate "<param>_map" + "<param>_map_on" slots.
//
// Rather than guess by property index or maintain per-channel alias lists, this resolves every
// value to a canonical OpenPBR name (binding table when present, otherwise prefix stripping via the
// recursive descendant walk plus camelCase->snake_case normalization) and looks it up in a single
// target table. Returns true if the material was recognized as OpenPBR-authored and processed.
bool
_mapDccOpenPbrMaterialOpenPbr(const FbxSurfaceMaterial* fbxMaterial,
                              ImportFbxContext& ctx,
                              const std::unordered_map<FbxObject*, size_t>& textures,
                              OpenPbrMaterial& usdMaterial,
                              InputTranslator& inputTranslator)
{
    const TfToken& colorPropertySpace =
      (ctx.originalColorSpace == AdobeTokens->sRGB) ? AdobeTokens->sRGB : AdobeTokens->raw;

    // Canonical OpenPBR (snake_case) name -> destination field, channel count, colorspace.
    std::unordered_map<std::string, std::tuple<Input&, FbxPropertyNumChannels, const TfToken&>>
      targetTable = {
          { "base_weight",
            { usdMaterial.base_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "base_color",
            { usdMaterial.base_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "base_diffuse_roughness",
            { usdMaterial.base_diffuse_roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "base_metalness",
            { usdMaterial.base_metalness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_weight",
            { usdMaterial.specular_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_color",
            { usdMaterial.specular_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "specular_roughness",
            { usdMaterial.specular_roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_ior",
            { usdMaterial.specular_ior, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_roughness_anisotropy",
            { usdMaterial.specular_roughness_anisotropy,
              FbxPropertyNumChannels::One,
              AdobeTokens->raw } },
          { "transmission_weight",
            { usdMaterial.transmission_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission_color",
            { usdMaterial.transmission_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "transmission_depth",
            { usdMaterial.transmission_depth, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "transmission_scatter",
            { usdMaterial.transmission_scatter,
              FbxPropertyNumChannels::Three,
              colorPropertySpace } },
          { "transmission_scatter_anisotropy",
            { usdMaterial.transmission_scatter_anisotropy,
              FbxPropertyNumChannels::One,
              AdobeTokens->raw } },
          { "transmission_dispersion_scale",
            { usdMaterial.transmission_dispersion_scale,
              FbxPropertyNumChannels::One,
              AdobeTokens->raw } },
          { "transmission_dispersion_abbe_number",
            { usdMaterial.transmission_dispersion_abbe_number,
              FbxPropertyNumChannels::One,
              AdobeTokens->raw } },
          { "subsurface_weight",
            { usdMaterial.subsurface_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "subsurface_color",
            { usdMaterial.subsurface_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "subsurface_radius",
            { usdMaterial.subsurface_radius, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "subsurface_radius_scale",
            { usdMaterial.subsurface_radius_scale,
              FbxPropertyNumChannels::Three,
              AdobeTokens->raw } },
          { "subsurface_scatter_anisotropy",
            { usdMaterial.subsurface_scatter_anisotropy,
              FbxPropertyNumChannels::One,
              AdobeTokens->raw } },
          { "fuzz_weight",
            { usdMaterial.fuzz_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "fuzz_color",
            { usdMaterial.fuzz_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "fuzz_roughness",
            { usdMaterial.fuzz_roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_weight",
            { usdMaterial.coat_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_color",
            { usdMaterial.coat_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "coat_roughness",
            { usdMaterial.coat_roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_roughness_anisotropy",
            { usdMaterial.coat_roughness_anisotropy,
              FbxPropertyNumChannels::One,
              AdobeTokens->raw } },
          { "coat_ior", { usdMaterial.coat_ior, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_darkening",
            { usdMaterial.coat_darkening, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "thin_film_weight",
            { usdMaterial.thin_film_weight, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "thin_film_thickness",
            { usdMaterial.thin_film_thickness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "thin_film_ior",
            { usdMaterial.thin_film_ior, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "emission_luminance",
            { usdMaterial.emission_luminance, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "emission_color",
            { usdMaterial.emission_color, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "geometry_opacity",
            { usdMaterial.geometry_opacity, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "geometry_normal",
            { usdMaterial.geometry_normal, FbxPropertyNumChannels::Three, AdobeTokens->raw } },
          { "geometry_coat_normal",
            { usdMaterial.geometry_coat_normal, FbxPropertyNumChannels::Three, AdobeTokens->raw } },
      };

    // Build the authoritative binding-table map (Maya). Source is the full prefixed property name
    // ("Maya|baseColor"), destination the canonical OpenPBR name ("base_color"). Also note whether
    // any implementation declares the OpenPbrSL shading language, which is a strong detection
    // signal. Implementations are connected as destination objects of the material.
    std::unordered_map<std::string, std::string> bindingMap;
    bool hasOpenPbrImplementation = false;
    const int implCount = fbxMaterial->GetDstObjectCount<FbxImplementation>();
    for (int j = 0; j < implCount; ++j) {
        const FbxImplementation* impl = fbxMaterial->GetDstObject<FbxImplementation>(j);
        if (!impl) {
            continue;
        }
        // A material may carry several implementations (e.g. 3ds Max emits a MentalRay one
        // alongside OpenPBR). Only trust the OpenPbrSL binding table so unrelated mappings don't
        // leak in.
        if (std::string(impl->Language.Get().Buffer()) != "OpenPbrSL") {
            continue;
        }
        hasOpenPbrImplementation = true;
        const FbxBindingTable* table = impl->GetRootTable();
        if (!table) {
            continue;
        }
        for (size_t k = 0; k < table->GetEntryCount(); ++k) {
            const FbxBindingTableEntry& entry = table->GetEntry(k);
            const char* source = entry.GetSource();
            const char* destination = entry.GetDestination();
            if (source && destination) {
                bindingMap[source] = destination;
            }
        }
    }

    // Resolve a property to its canonical OpenPBR name: binding table first, otherwise normalize
    // the leaf name to snake_case (a no-op for 3ds Max's already-snake_case leaves).
    auto canonicalName = [&](const FbxProperty& prop) -> std::string {
        auto it = bindingMap.find(prop.GetHierarchicalName().Buffer());
        if (it != bindingMap.end()) {
            return it->second;
        }
        return _toSnakeCase(prop.GetName().Buffer());
    };

    // Detection: only claim this material when there is an unambiguous OpenPBR signal, so we don't
    // intercept other shaders (e.g. Arnold "Standard Surface") that happen to share property names
    // like base_color and specular_roughness. The two reliable signals from real Maya 2026 / 3ds
    // Max 2026 exports are the Maya "openpbrsurface" shading model and an OpenPbrSL
    // FbxImplementation (which both DCCs emit, carrying the binding table). Materials without
    // either fall through to the existing standard-surface and heuristic handlers unchanged.
    FbxProperty shadingModelProp =
      FbxSurfaceMaterialUtils::GetProperty(FbxSurfaceMaterial::sShadingModel, fbxMaterial);
    std::string shadingModel =
      shadingModelProp.IsValid() ? shadingModelProp.Get<FbxString>().Buffer() : "";
    std::transform(shadingModel.begin(),
                   shadingModel.end(),
                   shadingModel.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (shadingModel != "openpbrsurface" && !hasOpenPbrImplementation) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "Material '%s' has no OpenPBR signal, not handling as DCC OpenPBR\n",
                     fbxMaterial->GetName());
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "Processing '%s' as DCC OpenPBR material (binding entries=%zu)\n",
                 fbxMaterial->GetName(),
                 bindingMap.size());

    const bool convertToLinear = (ctx.originalColorSpace == AdobeTokens->sRGB);

    // 3ds Max stores textures on separate "<param>_map" slots gated by a "<param>_map_on" bool.
    std::unordered_map<std::string, FbxProperty> mapTextureProps;
    std::unordered_map<std::string, bool> mapEnabled;
    auto endsWith = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    // Maya wires the normal through the legacy normalCamera attribute (bound as "normalCamera",
    // not "geometry_normal"), with a sibling normalCameraUsedAs that says whether it's a bump (0)
    // or a tangent-space normal map (1). Capture these and resolve after the loop.
    FbxProperty normalCameraProp;
    FbxProperty coatNormalProp;
    int normalCameraUsedAs = 0;

    bool foundAnyProperties = false;
    for (FbxProperty prop = fbxMaterial->GetFirstProperty(); prop.IsValid();
         prop = fbxMaterial->GetNextProperty(prop)) {
        const std::string leaf = prop.GetName().Buffer();

        // Collect 3ds Max texture-map slots for a second pass.
        if (endsWith(leaf, "_map_on")) {
            mapEnabled[_toSnakeCase(leaf.substr(0, leaf.size() - 7))] = prop.Get<FbxBool>();
            continue;
        }
        if (endsWith(leaf, "_map")) {
            if (FbxCast<FbxTexture>(prop.GetSrcObject())) {
                mapTextureProps[_toSnakeCase(leaf.substr(0, leaf.size() - 4))] = prop;
            }
            continue;
        }

        // Maya normal inputs: capture the texture-bearing normalCamera / geometryCoatNormal and the
        // bump-vs-normal flag; routed to geometry_normal / geometry_coat_normal after the loop.
        if (leaf == "normalCameraUsedAs") {
            normalCameraUsedAs = static_cast<int>(static_cast<FbxPropertyT<FbxInt>>(prop).Get());
            continue;
        }
        if (leaf == "normalCamera") {
            if (FbxCast<FbxTexture>(prop.GetSrcObject())) {
                normalCameraProp = prop;
            }
            continue;
        }
        if (leaf == "geometryCoatNormal") {
            if (FbxCast<FbxTexture>(prop.GetSrcObject())) {
                coatNormalProp = prop;
            }
            continue;
        }

        const std::string canonical = canonicalName(prop);
        const EFbxType dataType = prop.GetPropertyDataType().GetType();

        if (canonical == "geometry_thin_walled" && dataType == eFbxBool) {
            auto typed = static_cast<FbxPropertyT<FbxBool>>(prop);
            usdMaterial.geometry_thin_walled.value = static_cast<bool>(typed.Get());
            usdMaterial.geometry_thin_walled.colorspace = AdobeTokens->raw;
            foundAnyProperties = true;
            continue;
        }

        auto target = targetTable.find(canonical);
        if (target == targetTable.end()) {
            continue;
        }
        Input& input = std::get<0>(target->second);
        const FbxPropertyNumChannels numChannels = std::get<1>(target->second);
        const TfToken& colorSpace = std::get<2>(target->second);

        Input tempInput;
        if (numChannels == FbxPropertyNumChannels::Three) {
            if (dataType == eFbxDouble3) {
                auto typed = static_cast<FbxPropertyT<FbxDouble3>>(prop);
                importPropTexture(ctx, textures, fbxMaterial, typed, tempInput, "rgb", colorSpace);
            } else if (dataType == eFbxDouble4) {
                // ColorAndAlpha: take RGB, drop alpha, and mirror importPropTexture's colorspace
                // handling so 3ds Max colors land in the same space as Maya's.
                auto typed = static_cast<FbxPropertyT<FbxDouble4>>(prop);
                FbxDouble4 rgba = typed.Get();
                tempInput.value = GfVec3f(rgba[0], rgba[1], rgba[2]);
                if (convertToLinear && colorSpace == AdobeTokens->sRGB) {
                    tempInput.value = srgbToLinear(tempInput.value);
                }
                tempInput.colorspace = convertToLinear ? AdobeTokens->raw : colorSpace;
            } else {
                continue;
            }
        } else if (numChannels == FbxPropertyNumChannels::One) {
            if (dataType != eFbxDouble && dataType != eFbxFloat) {
                continue;
            }
            // Read both Double and Float scalars through an FbxDouble view (the SDK coerces float)
            // so importPropTexture also picks up a texture wired onto the scalar property itself,
            // which is how Maya attaches roughness/metalness/coat maps.
            auto typed = static_cast<FbxPropertyT<FbxDouble>>(prop);
            importPropTexture(ctx, textures, fbxMaterial, typed, tempInput, "r", colorSpace);
            // If the property held its registered default and had no texture, importPropTexture
            // leaves the value empty; record the scalar explicitly so e.g. base_metalness=0 sticks.
            if (tempInput.value.IsEmpty() && tempInput.image < 0) {
                tempInput.value = static_cast<float>(typed.Get());
                tempInput.colorspace = AdobeTokens->raw;
            }
        } else {
            // Two (or any future channel count) is not mappable here; skip rather than
            // translate a default-constructed empty input.
            continue;
        }
        inputTranslator.translateDirect(tempInput, input);
        foundAnyProperties = true;
    }

    // Apply collected 3ds Max texture maps to their targets, honoring the "_map_on" toggle.
    for (auto& it : mapTextureProps) {
        const std::string& base = it.first;
        auto enabled = mapEnabled.find(base);
        if (enabled != mapEnabled.end() && !enabled->second) {
            continue;
        }
        auto target = targetTable.find(base);
        if (target == targetTable.end()) {
            continue;
        }
        Input& input = std::get<0>(target->second);
        const FbxPropertyNumChannels numChannels = std::get<1>(target->second);
        const TfToken& colorSpace = std::get<2>(target->second);

        Input tempInput;
        auto typed = static_cast<FbxPropertyT<FbxDouble3>>(it.second);
        importPropTexture(ctx,
                          textures,
                          fbxMaterial,
                          typed,
                          tempInput,
                          numChannels == FbxPropertyNumChannels::Three ? "rgb" : "r",
                          colorSpace);
        if (tempInput.image >= 0) {
            inputTranslator.translateDirect(tempInput, input);
            foundAnyProperties = true;
        }
    }

    // Maya base normal. Only route it when authored as a tangent-space normal map
    // (Use As: Tangent Space Normals -> normalCameraUsedAs == 1); a bump/height map (0) or
    // object-space normal is not a tangent-space geometry_normal and is left alone.
    if (normalCameraProp.IsValid() && normalCameraUsedAs == 1) {
        Input tempInput;
        auto typed = static_cast<FbxPropertyT<FbxDouble3>>(normalCameraProp);
        importPropTexture(ctx, textures, fbxMaterial, typed, tempInput, "rgb", AdobeTokens->raw);
        if (tempInput.image >= 0) {
            inputTranslator.translateDirect(tempInput, usdMaterial.geometry_normal);
            foundAnyProperties = true;
        }
    } else if (normalCameraProp.IsValid()) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "Material '%s' normalCamera is not a tangent-space normal map "
                     "(normalCameraUsedAs=%d); not routed to geometry_normal\n",
                     fbxMaterial->GetName(),
                     normalCameraUsedAs);
    }

    // Maya coat normal (geometryCoatNormal) is always a normal input when textured.
    if (coatNormalProp.IsValid()) {
        Input tempInput;
        auto typed = static_cast<FbxPropertyT<FbxDouble3>>(coatNormalProp);
        importPropTexture(ctx, textures, fbxMaterial, typed, tempInput, "rgb", AdobeTokens->raw);
        if (tempInput.image >= 0) {
            inputTranslator.translateDirect(tempInput, usdMaterial.geometry_coat_normal);
            foundAnyProperties = true;
        }
    }

    return foundAnyProperties;
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
    // Determine the effective colorspace for color properties based on the originalColorSpace
    // option. If originalColorSpace is set to sRGB, color data will be converted to linear and
    // stored as raw. If originalColorSpace is not set, no conversion happens and data is passed
    // through as raw (unknown colorspace - let the client application handle color management).
    const TfToken& colorPropertySpace =
      (ctx.originalColorSpace == AdobeTokens->sRGB) ? AdobeTokens->sRGB : AdobeTokens->raw;

    // This will contain the properties that are directly mapped from the standard surface exactly
    // as is.  We need to note if they are One or Three channels and the colorspace for later usage.
    std::unordered_map<std::string, std::tuple<Input&, FbxPropertyNumChannels, const TfToken&>>
      standardSurfToUsdProperty = {
          { "base_color",
            { usdMaterial.diffuseColor, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "specular_color",
            { usdMaterial.specularColor, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "metalness", { usdMaterial.metallic, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "specular_roughness",
            { usdMaterial.roughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat", { usdMaterial.clearcoat, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_color",
            { usdMaterial.clearcoatColor, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "coat_roughness",
            { usdMaterial.clearcoatRoughness, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "coat_IOR",
            { usdMaterial.clearcoatIor, FbxPropertyNumChannels::One, AdobeTokens->raw } },
          { "sheen_color",
            { usdMaterial.sheenColor, FbxPropertyNumChannels::Three, colorPropertySpace } },
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
            { usdMaterial.absorptionColor, FbxPropertyNumChannels::Three, colorPropertySpace } },
          { "subsurface_color",
            { usdMaterial.scatteringColor, FbxPropertyNumChannels::Three, colorPropertySpace } },
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
        FbxProperty property = getProp(it.first);
        Input& input = std::get<0>(it.second);
        FbxPropertyNumChannels numChannels = std::get<1>(it.second);
        const TfToken& colorSpace = std::get<2>(it.second);
        if (numChannels == FbxPropertyNumChannels::One) {
            auto typedProp = static_cast<FbxPropertyT<FbxDouble>>(property);
            Input tempInput;
            importPropTexture(ctx, textures, fbxMaterial, typedProp, tempInput, "r", colorSpace);
            inputTranslator.translateDirect(tempInput, input);
        } else if (numChannels == FbxPropertyNumChannels::Three) {
            auto typedProp = static_cast<FbxPropertyT<FbxDouble3>>(property);
            Input tempInput;
            importPropTexture(ctx, textures, fbxMaterial, typedProp, tempInput, "rgb", colorSpace);
            inputTranslator.translateDirect(tempInput, input);
        } else {
            TF_CODING_ERROR("Unknown number of channels");
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
                      colorPropertySpace);

    // XXX I believe a more proper way to do this is to keep the emissive intensity as a
    // separate input because in UIs that use a color picker to modify this input you will lose
    // values over one upon modification.  This matches how GLTF handles it though currently, and I
    // think this also is an issue there as well
    inputTranslator.translateFactor(emissionColorInput, emissionInput, usdMaterial.emissiveColor);

    // Opacity in USD must be stored as a single value
    auto opacityProperty = getProp(kOpacity);
    if (opacityProperty.IsValid()) {
        auto opacityTypedProp = static_cast<FbxPropertyT<FbxDouble3>>(opacityProperty);
        GfVec3f opacityColor = readPropValue(opacityTypedProp);

        // Convert the opacity color to grayscale and use that as the opacity value
        float grayscaleOpacity = (opacityColor[0] + opacityColor[1] + opacityColor[2]) / 3.0f;
        usdMaterial.opacity.value = grayscaleOpacity;
        usdMaterial.opacity.colorspace = AdobeTokens->raw;
    }

    return true;
}

bool
_processHardwareShaderMaterialOpenPbr(const FbxSurfaceMaterial* fbxMaterial,
                                      ImportFbxContext& ctx,
                                      const std::unordered_map<FbxObject*, size_t>& textures,
                                      OpenPbrMaterial& usdMaterial,
                                      InputTranslator& inputTranslator)
{
    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "Attempting hardware shader material processing for '%s'\n",
                 fbxMaterial->GetName());

    // Determine colorspace based on originalColorSpace option
    const TfToken& colorPropertySpace =
      (ctx.originalColorSpace == AdobeTokens->sRGB) ? AdobeTokens->sRGB : AdobeTokens->raw;

    bool foundAnyProperties = false;

    FbxProperty prop = fbxMaterial->GetFirstProperty();
    int propertyIndex = 0;

    while (prop.IsValid()) {
        auto propType = prop.GetPropertyDataType();

        // Check for ColorAndAlpha properties (typical for 3ds Max materials)
        if (propType.GetType() == eFbxDouble4) {
            auto typedProperty = static_cast<FbxPropertyT<FbxDouble4>>(prop);
            FbxDouble4 colorWithAlpha = typedProperty.Get();
            GfVec3f colorValue(colorWithAlpha[0], colorWithAlpha[1], colorWithAlpha[2]);

            // (3ds Max Physical Material stores base_color as the first ColorAndAlpha property)
            if (colorValue != GfVec3f(0, 0, 0) &&
                !usdMaterial.base_color.value.IsHolding<GfVec3f>()) {
                usdMaterial.base_color.value = colorValue;
                usdMaterial.base_color.colorspace = colorPropertySpace;
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "  Found color at property index %d: (%f, %f, %f)\n",
                             propertyIndex,
                             colorValue[0],
                             colorValue[1],
                             colorValue[2]);
                foundAnyProperties = true;
            }
        }

        prop = fbxMaterial->GetNextProperty(prop);
        propertyIndex++;
    }

    return foundAnyProperties;
}

bool
_processHardwareShaderMaterial(const FbxSurfaceMaterial* fbxMaterial,
                               ImportFbxContext& ctx,
                               const std::unordered_map<FbxObject*, size_t>& textures,
                               Material& usdMaterial,
                               InputTranslator& inputTranslator)
{
    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "Attempting hardware shader material processing for '%s'\n",
                 fbxMaterial->GetName());

    // Determine colorspace based on originalColorSpace option
    const TfToken& colorPropertySpace =
      (ctx.originalColorSpace == AdobeTokens->sRGB) ? AdobeTokens->sRGB : AdobeTokens->raw;

    bool foundAnyProperties = false;

    FbxProperty prop = fbxMaterial->GetFirstProperty();
    int propertyIndex = 0;

    while (prop.IsValid()) {
        auto propType = prop.GetPropertyDataType();

        // Check for ColorAndAlpha properties (typical for 3ds Max materials)
        if (propType.GetType() == eFbxDouble4) {
            auto typedProperty = static_cast<FbxPropertyT<FbxDouble4>>(prop);
            FbxDouble4 colorWithAlpha = typedProperty.Get();
            GfVec3f colorValue(colorWithAlpha[0], colorWithAlpha[1], colorWithAlpha[2]);

            // (3ds Max Physical Material stores base_color as the first ColorAndAlpha property)
            if (colorValue != GfVec3f(0, 0, 0) &&
                !usdMaterial.diffuseColor.value.IsHolding<GfVec3f>()) {
                usdMaterial.diffuseColor.value = colorValue;
                usdMaterial.diffuseColor.colorspace = colorPropertySpace;
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "  Found color at property index %d: (%f, %f, %f)\n",
                             propertyIndex,
                             colorValue[0],
                             colorValue[1],
                             colorValue[2]);
                foundAnyProperties = true;
            }
        }

        prop = fbxMaterial->GetNextProperty(prop);
        propertyIndex++;
    }

    return foundAnyProperties;
}

// Resolve a material property by one of several candidate names. 3ds Max Physical Materials
// (exported as a StandardSSL hardware shader) expose their parameters as compound child properties
// whose GetName() is empty; the meaningful identifier is the hierarchical name
// ("3dsMax|Parameters|roughness"). FbxSurfaceMaterialUtils::GetProperty only matches the flat
// name, so fall back to scanning every property and comparing both the hierarchical and leaf name.
static FbxProperty
_findMaterialProperty(const FbxSurfaceMaterial* material, const std::string& name)
{
    FbxProperty direct = FbxSurfaceMaterialUtils::GetProperty(name.c_str(), material);
    if (direct.IsValid()) {
        return direct;
    }
    for (FbxProperty prop = material->GetFirstProperty(); prop.IsValid();
         prop = material->GetNextProperty(prop)) {
        if (name == prop.GetHierarchicalName().Buffer() || name == prop.GetName().Buffer()) {
            return prop;
        }
    }
    return FbxProperty();
}

// Fallback processor for materials with unknown ShadingModel that fail Lambert/Phong casting.
// This uses property-based detection to extract common material properties regardless of
// FBX material type classification.
// Returns true if the material was successfully processed as a property-based material
bool
_processUnknownShadingModelOpenPbr(const FbxSurfaceMaterial* fbxMaterial,
                                   ImportFbxContext& ctx,
                                   const std::unordered_map<FbxObject*, size_t>& textures,
                                   OpenPbrMaterial& usdMaterial,
                                   InputTranslator& inputTranslator)
{
    TF_DEBUG_MSG(
      FILE_FORMAT_FBX,
      "Processing material '%s' with unknown ShadingModel using property-based approach\n",
      fbxMaterial->GetName());

    // Determine colorspace based on originalColorSpace option
    const TfToken& colorPropertySpace =
      (ctx.originalColorSpace == AdobeTokens->sRGB) ? AdobeTokens->sRGB : AdobeTokens->raw;

    bool foundAnyProperties = false;

    // Helper to safely extract color properties with multiple naming conventions
    auto extractColorProperty = [&](const std::vector<std::string>& names,
                                    Input& targetInput) -> bool {
        for (const std::string& propName : names) {
            auto property = _findMaterialProperty(fbxMaterial, propName);
            if (property.IsValid()) {
                // Check for both FbxDouble3DT and ColorRGB types (more flexible)
                auto propType = property.GetPropertyDataType();
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "  Checking property '%s' (type: %s)\n",
                             propName.c_str(),
                             propType.GetName());

                // Check if property is compatible with FbxDouble3 or FbxDouble4 (ColorAndAlpha)
                if (propType.GetType() == eFbxDouble3) {
                    auto typedProperty = static_cast<FbxPropertyT<FbxDouble3>>(property);
                    GfVec3f colorValue = readPropValue(typedProperty);
                    if (colorValue != GfVec3f(0, 0, 0)) { // Skip if all zeros
                        targetInput.value = colorValue;
                        targetInput.colorspace = colorPropertySpace;
                        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                                     "  Found color property '%s': (%f, %f, %f)\n",
                                     propName.c_str(),
                                     colorValue[0],
                                     colorValue[1],
                                     colorValue[2]);
                        return true;
                    }
                } else if (propType.GetType() == eFbxDouble4) {
                    // Handle ColorAndAlpha (FbxDouble4) - extract RGB, ignore alpha
                    auto typedProperty = static_cast<FbxPropertyT<FbxDouble4>>(property);
                    FbxDouble4 colorWithAlpha = typedProperty.Get();
                    GfVec3f colorValue(colorWithAlpha[0], colorWithAlpha[1], colorWithAlpha[2]);
                    if (colorValue != GfVec3f(0, 0, 0)) { // Skip if all zeros
                        targetInput.value = colorValue;
                        targetInput.colorspace = colorPropertySpace;
                        TF_DEBUG_MSG(
                          FILE_FORMAT_FBX,
                          "  Found ColorAndAlpha property '%s': (%f, %f, %f, alpha=%f)\n",
                          propName.c_str(),
                          colorValue[0],
                          colorValue[1],
                          colorValue[2],
                          colorWithAlpha[3]);
                        return true;
                    }
                } else {
                    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                                 "  Property '%s' has incompatible type '%s', expected FbxDouble3 "
                                 "or FbxDouble4\n",
                                 propName.c_str(),
                                 propType.GetName());
                }
            }
        }
        return false;
    };

    // Helper to safely extract scalar properties with multiple naming conventions
    auto extractScalarProperty = [&](const std::vector<std::string>& names,
                                     Input& targetInput) -> bool {
        for (const std::string& propName : names) {
            auto property = _findMaterialProperty(fbxMaterial, propName);
            if (!property.IsValid()) {
                continue;
            }
            // Accept both Double and Float scalars; 3ds Max stores roughness/metalness as Float.
            const EFbxType propScalarType = property.GetPropertyDataType().GetType();
            double scalarValue = 0.0;
            if (propScalarType == eFbxFloat) {
                scalarValue = static_cast<FbxPropertyT<FbxFloat>>(property).Get();
            } else if (propScalarType == eFbxDouble) {
                scalarValue = static_cast<FbxPropertyT<FbxDouble>>(property).Get();
            } else {
                continue;
            }
            // A hierarchical DCC parameter (e.g. "3dsMax|Parameters|roughness") is explicitly
            // authored by the application, so keep its value even when it is zero, a smooth
            // dielectric authored as roughness=0 would otherwise be dropped. For the legacy flat
            // candidate names keep the >0 guard so unset defaults aren't written.
            const bool isDccAuthored = propName.find('|') != std::string::npos;
            if (scalarValue > 0.0 || isDccAuthored) {
                targetInput.value = static_cast<float>(scalarValue);
                targetInput.colorspace = AdobeTokens->raw;
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "  Found scalar property '%s': %f\n",
                             propName.c_str(),
                             scalarValue);
                return true;
            }
        }
        return false;
    };

    // Debug: List all properties available on this material
    if (TfDebug::IsEnabled(FILE_FORMAT_FBX)) {
        TF_DEBUG_MSG(
          FILE_FORMAT_FBX, "Debugging properties for material '%s':\n", fbxMaterial->GetName());
        FbxProperty prop = fbxMaterial->GetFirstProperty();
        int propertyCount = 0;
        while (prop.IsValid()) {
            const char* propName = prop.GetName();
            auto propType = prop.GetPropertyDataType();
            TF_DEBUG_MSG(FILE_FORMAT_FBX,
                         "  Property[%d]: name='%s' hier='%s' (type: %s)\n",
                         propertyCount++,
                         propName,
                         prop.GetHierarchicalName().Buffer(),
                         propType.GetName());
            prop = fbxMaterial->GetNextProperty(prop);
        }
    }

    // Try to extract diffuse/base color with the exact property names from FBX ASCII analysis
    const std::vector<std::string> colorNames = {
        // 3ds Max Physical Material properties
        "3dsMax|Parameters|base_color",
        // PRIMARY: Properties confirmed in FBX ASCII
        "DiffuseColor", // All materials have this exact property
        "AmbientColor", // Fallback color property
        // SECONDARY: Common variations
        "base_color",
        "baseColor",
        "BaseColor",
        "diffuseColor",
        "diffuse_color",
        "Color",
        "color",
        "Diffuse",
        "diffuse"
    };

    if (extractColorProperty(colorNames, usdMaterial.base_color)) {
        foundAnyProperties = true;
    }

    // Try to extract metallic with various naming conventions
    const std::vector<std::string> metallicNames = { "3dsMax|Parameters|metalness",
                                                     "metallic",
                                                     "Metallic",
                                                     "metalness",
                                                     "Metalness",
                                                     "metal",
                                                     "Metal" };

    if (extractScalarProperty(metallicNames, usdMaterial.base_metalness)) {
        foundAnyProperties = true;
    }

    // Try to extract roughness with various naming conventions
    const std::vector<std::string> roughnessNames = {
        "3dsMax|Parameters|roughness", "roughness",         "Roughness",       "specular_roughness",
        "SpecularRoughness",           "surface_roughness", "SurfaceRoughness"
    };

    if (extractScalarProperty(roughnessNames, usdMaterial.specular_roughness)) {
        foundAnyProperties = true;
    }

    // Try to extract emissive color with various naming conventions
    const std::vector<std::string> emissiveNames = { "emissive",       "Emissive",
                                                     "emissive_color", "EmissiveColor",
                                                     "emission",       "Emission",
                                                     "emission_color", "EmissionColor" };

    if (extractColorProperty(emissiveNames, usdMaterial.emission_color)) {
        foundAnyProperties = true;
    }

    if (foundAnyProperties) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "Successfully processed material '%s' using property-based approach\n",
                     fbxMaterial->GetName());
        return true;
    }

    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "No recognizable properties found for material '%s'\n",
                 fbxMaterial->GetName());
    return false;
}

// Fallback processor for materials with unknown ShadingModel that fail Lambert/Phong casting.
// This uses property-based detection to extract common material properties regardless of
// FBX material type classification.
// Returns true if the material was successfully processed as a property-based material
bool
_processUnknownShadingModel(const FbxSurfaceMaterial* fbxMaterial,
                            ImportFbxContext& ctx,
                            const std::unordered_map<FbxObject*, size_t>& textures,
                            Material& usdMaterial,
                            InputTranslator& inputTranslator)
{
    TF_DEBUG_MSG(
      FILE_FORMAT_FBX,
      "Processing material '%s' with unknown ShadingModel using property-based approach\n",
      fbxMaterial->GetName());

    // Determine colorspace based on originalColorSpace option
    const TfToken& colorPropertySpace =
      (ctx.originalColorSpace == AdobeTokens->sRGB) ? AdobeTokens->sRGB : AdobeTokens->raw;

    bool foundAnyProperties = false;

    // Helper to safely extract color properties with multiple naming conventions
    auto extractColorProperty = [&](const std::vector<std::string>& names,
                                    Input& targetInput) -> bool {
        for (const std::string& propName : names) {
            auto property = _findMaterialProperty(fbxMaterial, propName);
            if (property.IsValid()) {
                // Check for both FbxDouble3DT and ColorRGB types (more flexible)
                auto propType = property.GetPropertyDataType();
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "  Checking property '%s' (type: %s)\n",
                             propName.c_str(),
                             propType.GetName());

                // Check if property is compatible with FbxDouble3 or FbxDouble4 (ColorAndAlpha)
                if (propType.GetType() == eFbxDouble3) {
                    auto typedProperty = static_cast<FbxPropertyT<FbxDouble3>>(property);
                    GfVec3f colorValue = readPropValue(typedProperty);
                    if (colorValue != GfVec3f(0, 0, 0)) { // Skip if all zeros
                        targetInput.value = colorValue;
                        targetInput.colorspace = colorPropertySpace;
                        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                                     "  Found color property '%s': (%f, %f, %f)\n",
                                     propName.c_str(),
                                     colorValue[0],
                                     colorValue[1],
                                     colorValue[2]);
                        return true;
                    }
                } else if (propType.GetType() == eFbxDouble4) {
                    // Handle ColorAndAlpha (FbxDouble4) - extract RGB, ignore alpha
                    auto typedProperty = static_cast<FbxPropertyT<FbxDouble4>>(property);
                    FbxDouble4 colorWithAlpha = typedProperty.Get();
                    GfVec3f colorValue(colorWithAlpha[0], colorWithAlpha[1], colorWithAlpha[2]);
                    if (colorValue != GfVec3f(0, 0, 0)) { // Skip if all zeros
                        targetInput.value = colorValue;
                        targetInput.colorspace = colorPropertySpace;
                        TF_DEBUG_MSG(
                          FILE_FORMAT_FBX,
                          "  Found ColorAndAlpha property '%s': (%f, %f, %f, alpha=%f)\n",
                          propName.c_str(),
                          colorValue[0],
                          colorValue[1],
                          colorValue[2],
                          colorWithAlpha[3]);
                        return true;
                    }
                } else {
                    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                                 "  Property '%s' has incompatible type '%s', expected FbxDouble3 "
                                 "or FbxDouble4\n",
                                 propName.c_str(),
                                 propType.GetName());
                }
            }
        }
        return false;
    };

    // Helper to safely extract scalar properties with multiple naming conventions
    auto extractScalarProperty = [&](const std::vector<std::string>& names,
                                     Input& targetInput) -> bool {
        for (const std::string& propName : names) {
            auto property = _findMaterialProperty(fbxMaterial, propName);
            if (!property.IsValid()) {
                continue;
            }
            // Accept both Double and Float scalars; 3ds Max stores roughness/metalness as Float.
            const EFbxType propScalarType = property.GetPropertyDataType().GetType();
            double scalarValue = 0.0;
            if (propScalarType == eFbxFloat) {
                scalarValue = static_cast<FbxPropertyT<FbxFloat>>(property).Get();
            } else if (propScalarType == eFbxDouble) {
                scalarValue = static_cast<FbxPropertyT<FbxDouble>>(property).Get();
            } else {
                continue;
            }
            // A hierarchical DCC parameter (e.g. "3dsMax|Parameters|roughness") is explicitly
            // authored by the application, so keep its value even when it is zero, a smooth
            // dielectric authored as roughness=0 would otherwise be dropped. For the legacy flat
            // candidate names keep the >0 guard so unset defaults aren't written.
            const bool isDccAuthored = propName.find('|') != std::string::npos;
            if (scalarValue > 0.0 || isDccAuthored) {
                targetInput.value = static_cast<float>(scalarValue);
                targetInput.colorspace = AdobeTokens->raw;
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "  Found scalar property '%s': %f\n",
                             propName.c_str(),
                             scalarValue);
                return true;
            }
        }
        return false;
    };

    // Debug: List all properties available on this material
    if (TfDebug::IsEnabled(FILE_FORMAT_FBX)) {
        TF_DEBUG_MSG(
          FILE_FORMAT_FBX, "Debugging properties for material '%s':\n", fbxMaterial->GetName());
        FbxProperty prop = fbxMaterial->GetFirstProperty();
        int propertyCount = 0;
        while (prop.IsValid()) {
            const char* propName = prop.GetName();
            auto propType = prop.GetPropertyDataType();
            TF_DEBUG_MSG(FILE_FORMAT_FBX,
                         "  Property[%d]: name='%s' hier='%s' (type: %s)\n",
                         propertyCount++,
                         propName,
                         prop.GetHierarchicalName().Buffer(),
                         propType.GetName());
            prop = fbxMaterial->GetNextProperty(prop);
        }
    }

    // Try to extract diffuse/base color with the exact property names from FBX ASCII analysis
    const std::vector<std::string> colorNames = {
        // 3ds Max Physical Material properties
        "3dsMax|Parameters|base_color",
        // PRIMARY: Properties confirmed in FBX ASCII
        "DiffuseColor", // All materials have this exact property
        "AmbientColor", // Fallback color property
        // SECONDARY: Common variations
        "base_color",
        "baseColor",
        "BaseColor",
        "diffuseColor",
        "diffuse_color",
        "Color",
        "color",
        "Diffuse",
        "diffuse"
    };

    if (extractColorProperty(colorNames, usdMaterial.diffuseColor)) {
        foundAnyProperties = true;
    }

    // Try to extract metallic with various naming conventions
    const std::vector<std::string> metallicNames = { "3dsMax|Parameters|metalness",
                                                     "metallic",
                                                     "Metallic",
                                                     "metalness",
                                                     "Metalness",
                                                     "metal",
                                                     "Metal" };

    if (extractScalarProperty(metallicNames, usdMaterial.metallic)) {
        foundAnyProperties = true;
    }

    // Try to extract roughness with various naming conventions
    const std::vector<std::string> roughnessNames = {
        "3dsMax|Parameters|roughness", "roughness",         "Roughness",       "specular_roughness",
        "SpecularRoughness",           "surface_roughness", "SurfaceRoughness"
    };

    if (extractScalarProperty(roughnessNames, usdMaterial.roughness)) {
        foundAnyProperties = true;
    }

    // Try to extract emissive color with various naming conventions
    const std::vector<std::string> emissiveNames = { "emissive",       "Emissive",
                                                     "emissive_color", "EmissiveColor",
                                                     "emission",       "Emission",
                                                     "emission_color", "EmissionColor" };

    if (extractColorProperty(emissiveNames, usdMaterial.emissiveColor)) {
        foundAnyProperties = true;
    }

    if (foundAnyProperties) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "Successfully processed material '%s' using property-based approach\n",
                     fbxMaterial->GetName());
        return true;
    }

    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "No recognizable properties found for material '%s'\n",
                 fbxMaterial->GetName());
    return false;
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
    // and then convert path to string using utf8 representation
    normalized = convertPathToString(std::filesystem::u8path(path).lexically_normal());

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
    std::string filename = convertPathToString(path);

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
            baseName = convertPathToString(normalizePathFromAnyOS(origAbsFileName).filename());
        } else {
            // GetRelativeFileName() will use the original OS path delimiters. We must normalize it
            // before adding it to the metadata.
            // Also- despite the name, GetRelativeFileName() may return an absolute path!
            std::filesystem::path filePathNormalized =
              normalizePathFromAnyOS(fileTexture->GetRelativeFileName());

            // Add the path to the metadata even if the file is not present on disk.
            ctx.usd->importedFileNames.insert(convertPathToString(filePathNormalized));

            std::filesystem::path absFilePath;
            if (isAbsolutePathFromAnyOS(filePathNormalized)) {
                absFilePath = filePathNormalized.make_preferred();

                std::error_code error_code;
                if (!std::filesystem::exists(absFilePath, error_code)) {
                    // FBX SDK quirk: GetRelativeFileName() can return an absolute-looking
                    // path (e.g. "/foo.png" on POSIX) for textures that are actually siblings
                    // of the FBX file. If the literal path doesn't exist, fall back to
                    // resolving the basename next to the FBX before giving up. This also
                    // lets us recover when the FBX was authored on a different machine and
                    // the original absolute path is no longer valid.
                    std::filesystem::path siblingPath = parentPath / absFilePath.filename();
                    if (std::filesystem::exists(siblingPath, error_code)) {
                        absFilePath = siblingPath.make_preferred();
                    } else {
                        TF_WARN("FBX image \"%s\" not found", absFilePath.u8string().c_str());
                        continue;
                    }
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

            absFileName = convertPathToString(absFilePath);
            baseName = convertPathToString(absFilePath.filename());
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
    const bool useOpenPbr = isNativeOpenPbrProcessingEnabled();
    if (useOpenPbr) {
        ctx.usd->openPbrMaterials.resize(materialsCount);
    } else {
        ctx.usd->materials.resize(materialsCount);
    }
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "\tMaterials count: %lu \n", materialsCount);
    for (size_t i = 0; i < materialsCount; i++) {
        FbxSurfaceMaterial* material = ctx.scene->GetSrcObject<FbxSurfaceMaterial>(i);
        ctx.materials[material] = i; // Should use GetUniqueID() instead of FbxObject* as key?

        FbxProperty lP =
          FbxSurfaceMaterialUtils::GetProperty(FbxSurfaceMaterial::sShadingModel, material);
        auto shaderModel = lP.Get<FbxString>().Buffer();

        if (useOpenPbr) {
            OpenPbrMaterial& um = ctx.usd->openPbrMaterials[i];
            um.name = material->GetName();
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: material[%lu] { %s }\n", i, um.name.c_str());
            TF_DEBUG_MSG(FILE_FORMAT_FBX, " Shader model: %s\n", shaderModel);

            // Check for and process the autodesk standard surface representation first before we do
            // anything else as this is handled as a special case
            if (_mapAutodeskStandardMaterialOpenPbr(material, ctx, textures, um, inputTranslator)) {
                // Everything was done in the above util, so we can just continue
                continue;
            }

            // Native OpenPBR materials authored by Maya 2026 / 3ds Max 2026. These carry the full
            // OpenPBR parameter set under a vendor prefix and don't cast to Lambert/Phong, so
            // handle them before falling through to the traditional and heuristic fallbacks.
            if (_mapDccOpenPbrMaterialOpenPbr(material, ctx, textures, um, inputTranslator)) {
                continue;
            }
        } else {
            Material& um = ctx.usd->materials[i];
            um.name = material->GetName();
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: material[%lu] { %s }\n", i, um.name.c_str());
            TF_DEBUG_MSG(FILE_FORMAT_FBX, " Shader model: %s\n", shaderModel);

            // Check for and process the autodesk standard surface representation first before we do
            // anything else as this is handled as a special case
            if (_mapAutodeskStandardMaterial(material, ctx, textures, um, inputTranslator)) {
                // Everything was done in the above util, so we can just continue
                continue;
            }
        }

        // Try traditional Lambert/Phong casting
        FbxSurfaceLambert* lambert = FbxCast<FbxSurfaceLambert>(material);
        FbxSurfacePhong* phong = FbxCast<FbxSurfacePhong>(material);

        if (lambert || phong) {
            // Traditional material processing - MOVED UP from below
            TF_DEBUG_MSG(
              FILE_FORMAT_FBX, "Processing '%s' as Lambert/Phong material\n", material->GetName());

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
                importPropTexture(
                  ctx, textures, material, lambert->Bump, bump, "r", AdobeTokens->raw);

                // For transparent textures, we only capture the R channel of the texture as we will
                // map this directly to opacity if the texture exists. We do this because the USD
                // Preview Surface only has a single-valued opacity property.
                // HOWEVER, using the 'r' channel of the TransparentColor texture as an opacity
                // value worked for some fbx scenes with separate opacity textures but some fbx
                // scenes (possibly incorrectly) used the DiffuseColor texture as the
                // TransparentColor texture. This lead to strange results. As a consequence, we are
                // currently ignoring the TransparentColor property and will only use the
                // TransparencyFactor and Opacity fbx properties on the material to map to the USD
                // opacity property. importPropTexture(ctx, textures, material,
                // lambert->TransparentColor, transparentColor, "r", AdobeTokens->raw);

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

            // Convert Phong specular/shininess to PBR roughness even when importPhong is off, so
            // glossiness is preserved. Lambert materials don't cast to FbxSurfacePhong, so this
            // only affects materials that actually carry Phong specular/shininess data.
            const bool usePhongConversion =
              ctx.options->importPhong ||
              (phong && (!shininess.value.IsEmpty() || shininess.image >= 0));

            if (useOpenPbr) {
                OpenPbrMaterial& um = ctx.usd->openPbrMaterials[i];
                if (ctx.options->importPhong) {
                    // Caller explicitly requested full Phong-to-PBR: derive both metallic and
                    // roughness from the algorithm.
                    inputTranslator.translatePhong2PBR(diffuse,
                                                       specular,
                                                       shininess,
                                                       um.base_color,
                                                       um.base_metalness,
                                                       um.specular_roughness);
                } else if (usePhongConversion) {
                    // Default Phong import treats the surface as a dielectric: base color from
                    // diffuse, roughness from shininess, and metalness only from an authored
                    // ReflectionFactor (empty/zero stays dielectric). Inferring metalness from
                    // specular brightness chromes painted or colored surfaces, since Maya/Max
                    // export a bright default specular highlight on nearly everything, and the
                    // resulting metals render black without an environment. importPhong=true above
                    // still runs the full solve for callers that explicitly want it.
                    inputTranslator.translateDirect(diffuse, um.base_color);
                    inputTranslator.translateDirect(reflectionFactor, um.base_metalness);
                    inputTranslator.translatePhong2Roughness(
                      specular, shininess, um.specular_roughness);
                } else {
                    inputTranslator.translateDirect(diffuse, um.base_color);
                    if (phong) {
                        inputTranslator.translateDirect(reflectionFactor, um.base_metalness);
                        inputTranslator.translateDirect(specularFactor, um.specular_roughness);
                    } else {
                        // Lambert is fully diffuse with no glossiness or specular concept:
                        // author a matte, non-metallic surface and zero the dielectric specular
                        // lobe. Otherwise the glTF exporter falls back to its hardcoded 0.5
                        // roughness default and OpenPBR's specular_weight default of 1.0 leaves a
                        // highlight that Lambert never has.
                        um.specular_roughness = Input{ VtValue(1.0f) };
                        um.base_metalness = Input{ VtValue(0.0f) };
                        um.specular_weight = Input{ VtValue(0.0f) };
                        // Maya/FBX store the Lambert "Diffuse" scalar as DiffuseFactor; carry it
                        // into base_weight so the diffuse albedo isn't silently scaled up to the
                        // OpenPBR base_weight default of 1.0. No-ops when DiffuseFactor is absent.
                        inputTranslator.translateDirect(diffuseFactor, um.base_weight);
                    }
                }
                inputTranslator.translateFactor(emissive, emissiveFactor, um.emission_color);
                if (!um.emission_color.isEmpty()) {
                    um.emission_luminance = Input{ VtValue(1000.0f) };
                }
                // Ignore specular color if there is a specular factor texture but no specular
                // color texture
                if ((specular.image >= 0) || (specularFactor.image < 0)) {
                    inputTranslator.translateFactor(specular, specularFactor, um.specular_color);
                }

                // NOTE: as commented above, we are ignoring TransparentColor values so the
                // condition in the 'if' statement below should always be false, in which case
                // the 'else' block will be executed.
                if (transparentColor.image >= 0) {
                    // If there is a TransparentColor texture, use it directly as opacity
                    inputTranslator.translateDirect(transparentColor, um.geometry_opacity);
                } else {
                    // There are FBX files where both the Opacity and TransparencyFactor
                    // properties are present (even though the Opacity property has been phased
                    // out and is not defined as a property of FbxSurfaceLambert). In some
                    // cases, both properties are present in the material definition and so
                    // it's unclear which should be used. We use the "TransparencyFactor"
                    // (ie 1.0) as is when both values are present and both equal 1.0.
                    // Otherwise, we convert TransparencyFactor to an opacity value by
                    // computing 1.0 - TransparencyFactor
                    FbxProperty opacityProp = material->FindProperty("Opacity", FbxDoubleDT, true);
                    FbxProperty transparencyFactorProp =
                      material->FindProperty("TransparencyFactor", FbxDoubleDT, true);
                    if (opacityProp.IsValid() && transparencyFactorProp.IsValid() &&
                        1.0 == opacityProp.Get<double>() &&
                        1.0 == transparencyFactorProp.Get<double>()) {
                        // Use the transparencyFactor as is and treat it like an opacity value
                        inputTranslator.translateDirect(transparencyFactor, um.geometry_opacity);
                    } else {
                        // Invert transparencyFactor and assign to usd opacity
                        inputTranslator.translateTransparency2Opacity(transparencyFactor,
                                                                      um.geometry_opacity);
                    }
                }
                inputTranslator.translateNormals(bump, normal, um.geometry_normal);
            } else {
                Material& um = ctx.usd->materials[i];
                if (ctx.options->importPhong) {
                    // Caller explicitly requested full Phong-to-PBR: derive both metallic and
                    // roughness from the algorithm.
                    inputTranslator.translatePhong2PBR(
                      diffuse, specular, shininess, um.diffuseColor, um.metallic, um.roughness);
                } else if (usePhongConversion) {
                    // Default Phong import treats the surface as a dielectric: base color from
                    // diffuse, roughness from shininess, and metalness only from an authored
                    // ReflectionFactor (empty/zero stays dielectric). Inferring metalness from
                    // specular brightness chromes painted or colored surfaces, since Maya/Max
                    // export a bright default specular highlight on nearly everything, and the
                    // resulting metals render black without an environment. importPhong=true above
                    // still runs the full solve for callers that explicitly want it.
                    inputTranslator.translateDirect(diffuse, um.diffuseColor);
                    inputTranslator.translateDirect(reflectionFactor, um.metallic);
                    inputTranslator.translatePhong2Roughness(specular, shininess, um.roughness);
                } else {
                    inputTranslator.translateDirect(diffuse, um.diffuseColor);
                    if (phong) {
                        inputTranslator.translateDirect(reflectionFactor, um.metallic);
                        inputTranslator.translateDirect(specularFactor, um.roughness);
                    } else {
                        // Lambert is fully diffuse with no glossiness concept: author a matte,
                        // non-metallic surface. Otherwise the glTF exporter falls back to its
                        // hardcoded 0.5 roughness default and the surface looks glossy.
                        um.roughness = Input{ VtValue(1.0f) };
                        um.metallic = Input{ VtValue(0.0f) };
                    }
                }
                inputTranslator.translateFactor(emissive, emissiveFactor, um.emissiveColor);
                // Ignore specular color if there is a specular factor texture but no specular
                // color texture
                if ((specular.image >= 0) || (specularFactor.image < 0)) {
                    inputTranslator.translateFactor(specular, specularFactor, um.specularColor);
                }

                // NOTE: as commented above, we are ignoring TransparentColor values so the
                // condition in the 'if' statement below should always be false, in which case
                // the 'else' block will be executed.
                if (transparentColor.image >= 0) {
                    // If there is a TransparentColor texture, use it directly as opacity
                    inputTranslator.translateDirect(transparentColor, um.opacity);
                } else {
                    // There are FBX files where both the Opacity and TransparencyFactor
                    // properties are present (even though the Opacity property has been phased
                    // out and is not defined as a property of FbxSurfaceLambert). In some
                    // cases, both properties are present in the material definition and so
                    // it's unclear which should be used. We use the "TransparencyFactor"
                    // (ie 1.0) as is when both values are present and both equal 1.0.
                    // Otherwise, we convert TransparencyFactor to an opacity value by
                    // computing 1.0 - TransparencyFactor
                    FbxProperty opacityProp = material->FindProperty("Opacity", FbxDoubleDT, true);
                    FbxProperty transparencyFactorProp =
                      material->FindProperty("TransparencyFactor", FbxDoubleDT, true);
                    if (opacityProp.IsValid() && transparencyFactorProp.IsValid() &&
                        1.0 == opacityProp.Get<double>() &&
                        1.0 == transparencyFactorProp.Get<double>()) {
                        // Use the transparencyFactor as is and treat it like an opacity value
                        inputTranslator.translateDirect(transparencyFactor, um.opacity);
                    } else {
                        // Invert transparencyFactor and assign to usd opacity
                        inputTranslator.translateTransparency2Opacity(transparencyFactor,
                                                                      um.opacity);
                    }
                }
                inputTranslator.translateNormals(bump, normal, um.normal);
            }
        } else {
            // Elegant fallback: Try property-based processing for materials that failed
            // Lambert/Phong casting
            TF_DEBUG_MSG(FILE_FORMAT_FBX,
                         "Lambert/Phong casting failed for '%s', trying fallback approaches\n",
                         material->GetName());

            // First check if it's a hardware shader
            const FbxImplementation* imp = LookForNonSupportedImplementation(material);
            if (imp) {
                TF_DEBUG_MSG(
                  FILE_FORMAT_FBX, "Detected hardware shader for '%s'\n", material->GetName());
                TF_DEBUG_MSG(FILE_FORMAT_FBX, " Language: %s\n", imp->Language.Get().Buffer());
                TF_DEBUG_MSG(
                  FILE_FORMAT_FBX, " LanguageVersion: %s\n", imp->LanguageVersion.Get().Buffer());
                TF_DEBUG_MSG(FILE_FORMAT_FBX, " RenderName: %s\n", imp->RenderName.Buffer());
                TF_DEBUG_MSG(FILE_FORMAT_FBX, " RenderAPI: %s\n", imp->RenderAPI.Get().Buffer());
                TF_DEBUG_MSG(
                  FILE_FORMAT_FBX, " RenderAPIVersion: %s\n", imp->RenderAPIVersion.Get().Buffer());

                // Extract properties from the hardware shader. The hardware-shader processor only
                // recovers a base color (the first non-zero ColorAndAlpha property), so also run
                // the property-based extractor, which reads named scalars such as
                // "3dsMax|Parameters|roughness" / "metalness". A 3ds Max Physical Material exported
                // as a StandardSSL hardware shader otherwise loses its roughness and metalness.
                // The property-based extractor runs first so its named base color wins; the
                // hardware-shader heuristic then only fills the color in if nothing named was
                // found.
                bool extracted = false;
                if (useOpenPbr) {
                    OpenPbrMaterial& um = ctx.usd->openPbrMaterials[i];
                    extracted |= _processUnknownShadingModelOpenPbr(
                      material, ctx, textures, um, inputTranslator);
                    extracted |= _processHardwareShaderMaterialOpenPbr(
                      material, ctx, textures, um, inputTranslator);
                } else {
                    Material& um = ctx.usd->materials[i];
                    extracted |=
                      _processUnknownShadingModel(material, ctx, textures, um, inputTranslator);
                    extracted |=
                      _processHardwareShaderMaterial(material, ctx, textures, um, inputTranslator);
                }

                if (extracted) {
                    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                                 "Successfully processed hardware shader '%s'\n",
                                 material->GetName());
                } else {
                    TF_WARN("Hardware shader '%s' detected but no properties could be extracted\n",
                            material->GetName());
                }
                continue;
            }

            // Try standard property-based fallback for non-hardware shader materials
            if (useOpenPbr) {
                OpenPbrMaterial& um = ctx.usd->openPbrMaterials[i];
                if (_processUnknownShadingModelOpenPbr(
                      material, ctx, textures, um, inputTranslator)) {
                    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                                 "Successfully processed '%s' using property-based fallback\n",
                                 material->GetName());
                    continue;
                }
            } else {
                Material& um = ctx.usd->materials[i];
                if (_processUnknownShadingModel(material, ctx, textures, um, inputTranslator)) {
                    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                                 "Successfully processed '%s' using property-based fallback\n",
                                 material->GetName());
                    continue;
                }
            }

            // If we get here, the material couldn't be processed by any method
            TF_WARN("Unable to process material '%s' - no recognizable properties found\n",
                    material->GetName());
        }
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

    // Track animated joints, avoiding duplicates across animation layers
    std::map<FbxNode*, TfToken> animatedJointsMap;

    size_t jointCount = 0;

    // clang-format off
    std::function<void(
      size_t skeletonIndex, Skeleton& skeleton, FbxNode* fbxNode, const SdfPath& parentPath)>
      importFbxBone;
    // clang-format on
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

        // The bindTransforms will be updated later when the skeleton clusters
        // are processed but we still set them using the default global joint transform.
        // Factor out the skeleton parent's global transform so bind transforms are
        // relative to the parent (armature) node, avoiding double-application through
        // the USD hierarchy.
        FbxAMatrix adjustedGlobal = importedSkeleton.parentGlobalInverse * globalTransform;
        skeleton.bindTransforms.push_back(GetUSDMatrixFromFBX(adjustedGlobal));

        // Here also register which nodes are animated,
        // and accumulate in a map the animation keys' times.
        if (fbxNode->LclRotation.IsAnimated() || fbxNode->LclTranslation.IsAnimated() ||
            fbxNode->LclScaling.IsAnimated()) {

            for (int animationStackIndex = 0;
                 animationStackIndex < static_cast<int>(ctx.animationStacks.size());
                 animationStackIndex++) {
                const ImportedFbxStack& fbxStack = ctx.animationStacks[animationStackIndex];
                std::set<FbxTime>& frames = framesInEachStack[animationStackIndex];

                for (FbxAnimLayer* animLayer : fbxStack.animLayers) {
                    const FbxAnimCurve* translationCurve =
                      fbxNode->LclTranslation.GetCurve(animLayer);
                    const FbxAnimCurve* rotationCurve = fbxNode->LclRotation.GetCurve(animLayer);
                    const FbxAnimCurve* scalingCurve = fbxNode->LclScaling.GetCurve(animLayer);

                    // Also check curve nodes for individual channels (X, Y, Z)
                    // Sometimes GetCurve() returns null but individual channels have curves
                    FbxAnimCurveNode* translationCurveNode =
                      fbxNode->LclTranslation.GetCurveNode(animLayer);
                    FbxAnimCurveNode* rotationCurveNode =
                      fbxNode->LclRotation.GetCurveNode(animLayer);
                    FbxAnimCurveNode* scalingCurveNode =
                      fbxNode->LclScaling.GetCurveNode(animLayer);

                    bool hasTranslationAnim =
                      (translationCurve != nullptr) ||
                      (translationCurveNode && translationCurveNode->GetChannelsCount() > 0);
                    bool hasRotationAnim =
                      (rotationCurve != nullptr) ||
                      (rotationCurveNode && rotationCurveNode->GetChannelsCount() > 0);
                    bool hasScalingAnim =
                      (scalingCurve != nullptr) ||
                      (scalingCurveNode && scalingCurveNode->GetChannelsCount() > 0);

                    addAnimCurveFrameTimes(translationCurve, frames);
                    addAnimCurveFrameTimes(rotationCurve, frames);
                    addAnimCurveFrameTimes(scalingCurve, frames);

                    // Also add frame times from curve node channels
                    if (translationCurveNode) {
                        for (unsigned int channeld = 0;
                             channeld < translationCurveNode->GetChannelsCount();
                             ++channeld) {
                            addAnimCurveFrameTimes(translationCurveNode->GetCurve(channeld),
                                                   frames);
                        }
                    }
                    if (rotationCurveNode) {
                        for (unsigned int channeld = 0;
                             channeld < rotationCurveNode->GetChannelsCount();
                             ++channeld) {
                            addAnimCurveFrameTimes(rotationCurveNode->GetCurve(channeld), frames);
                        }
                    }
                    if (scalingCurveNode) {
                        for (unsigned int channeld = 0;
                             channeld < scalingCurveNode->GetChannelsCount();
                             ++channeld) {
                            addAnimCurveFrameTimes(scalingCurveNode->GetCurve(channeld), frames);
                        }
                    }

                    // Add to map to track animated joints (avoids duplicates across layers)
                    // Use the more robust channel check instead of just checking for curves
                    if (hasTranslationAnim || hasRotationAnim || hasScalingAnim) {
                        // Only add if not already in the map
                        if (animatedJointsMap.find(fbxNode) == animatedJointsMap.end()) {
                            animatedJointsMap[fbxNode] = jointPathToken;
                        }
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

    // Populate skeleton.animatedJoints.
    // Don't add these to ctx.animatedSkeletonNodes yet because we don't know
    // if this skeleton has skinned meshes. That happens during importFbxNodeHierarchy.
    // We'll add them later in markAnimatedSkeletonNodes() if the skeleton has skinned meshes.
    for (const auto& pair : animatedJointsMap) {
        skeleton.animatedJoints.push_back(pair.second);
    }

    for (int animationStackIndex = 0;
         animationStackIndex < static_cast<int>(framesInEachStack.size());
         animationStackIndex++) {
        AnimationTrack& track = ctx.usd->animationTracks[animationStackIndex];
        std::set<FbxTime>& frames = framesInEachStack[animationStackIndex];

        // Extend animation to cover the full stack duration by adding a final hold frame
        // This ensures animations hold their final values to match the declared animation length
        if (!frames.empty()) {
            FbxTime lastKeyframeTime = *frames.rbegin();
            double lastKeyframeSeconds = lastKeyframeTime.GetSecondDouble();

            // Allow a small tolerance
            if (track.maxTime > lastKeyframeSeconds + 0.001) {
                FbxTime stackEndTime;
                stackEndTime.SetSecondDouble(track.maxTime);
                frames.insert(stackEndTime);
            }
        }

        // Set the current animation stack so that EvaluateLocalTransform will return the correct
        // value
        ctx.scene->SetCurrentAnimationStack(ctx.animationStacks[animationStackIndex].stack);
        if (!frames.empty()) {
            track.hasTimepoints = true;
            ctx.usd->hasAnimations = true;

            TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: assembling animation data\n");
            skeleton.skeletonAnimations.resize(framesInEachStack.size());
            SkeletonAnimation& skeletonAnimation = skeleton.skeletonAnimations[animationStackIndex];
            skeletonAnimation.times.resize(frames.size());
            skeletonAnimation.translations.resize(frames.size(),
                                                  VtArray<GfVec3f>(animatedJointsMap.size()));
            skeletonAnimation.rotations.resize(frames.size(),
                                               VtArray<GfQuatf>(animatedJointsMap.size()));
            skeletonAnimation.scales.resize(frames.size(),
                                            VtArray<GfVec3h>(animatedJointsMap.size()));
            size_t i = 0;
            for (const auto& nodePair : animatedJointsMap) {
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

// After skeleton and mesh import, mark which skeleton nodes should have their animations
// handled by the skeletal animation system (and thus skip node animation import).
// Only mark nodes for skeletons that have skinned meshes.
void
markAnimatedSkeletonNodes(ImportFbxContext& ctx)
{
    for (size_t skeletonIndex = 0; skeletonIndex < ctx.usd->skeletons.size(); skeletonIndex++) {
        const Skeleton& skeleton = ctx.usd->skeletons[skeletonIndex];

        // Only mark animated nodes for skeletons that have skinned meshes
        // If there are no skinned meshes, the skeletal animation won't be exported,
        // so we should let the regular node animation system handle it
        bool hasSkinnedMeshes = !skeleton.meshSkinningTargets.empty();
        if (hasSkinnedMeshes) {
            // This skeleton has skinned meshes, so mark its animated nodes
            // to skip regular node animation import
            for (const auto& pair : ctx.bonesMap) {
                FbxNode* fbxNode = pair.first;
                size_t jointIndex = pair.second;
                size_t skeletonMapIndex = ctx.skeletonsMap[fbxNode];

                if (skeletonMapIndex == skeletonIndex) {
                    // This bone belongs to this skeleton
                    // Check if it's in the animated joints list
                    TfToken jointToken = skeleton.joints[jointIndex];
                    bool isAnimated = std::find(skeleton.animatedJoints.begin(),
                                                skeleton.animatedJoints.end(),
                                                jointToken) != skeleton.animatedJoints.end();
                    if (isAnimated) {
                        ctx.animatedSkeletonNodes.insert(fbxNode);
                    }
                }
            }
        }
    }
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

    {
        ScopedAnimStackDisabler animStackDisabler(ctx);
        for (auto& skel : ctx.skeletons) {
            if (skel.fbxParent) {
                FbxAMatrix parentGlobal = skel.fbxParent->EvaluateGlobalTransform();
                skel.parentGlobalInverse = parentGlobal.Inverse();
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
        // Note: - if the skeleton was parented to FBX's artificial root node (which we do not
        // represent in USD) we will not find it in the nodeMap. In that case it is better to parent
        // it to the root node- we can use a parent index of -1 to do this.
        int parentIndex = -1;
        if (ctx.nodeMap.find(skeleton.fbxParent) != ctx.nodeMap.end()) {
            parentIndex = ctx.nodeMap[skeleton.fbxParent];
        }

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
                if (ctx.options->importLights) {
                    importFbxLight(ctx, attribute, parentIndex);
                }
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

// Before converting meshes from Fbx to USD, we pre-triangulate meshes whose FBX
// edge information defines a specific triangulation (e.g. splitting quads), plus
// meshes containing an untriangulated n-gon (see below). Meshes with neither are
// left as authored.
void
triangulateMeshes(ImportFbxContext& ctx)
{
    size_t meshCount = ctx.scene->GetSrcObjectCount<FbxMesh>();
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: Total meshes:%zu\n", meshCount);
    if (meshCount == 0)
        return;

    std::vector<FbxMesh*> meshes;
    meshes.reserve(meshCount);

    // Collect meshes with non-zero edge counts, plus any mesh that still has an
    // untriangulated n-gon (>4 sided polygon). Edge count alone isn't a reliable
    // signal: some DCCs export a flat, hole-free glyph cap (eg. text extrusion caps
    // for letters like "s"/"c") as a single large n-gon with zero recorded edges,
    // since no edge-visibility data is needed for one flat face. Left untriangulated,
    // that concave n-gon gets naively fan-triangulated by downstream consumers,
    // producing garbled geometry.
    // We can't triangulate in this loop because triangulation affects the
    // ordering of meshes.
    for (size_t i = 0; i < meshCount; ++i) {
        FbxMesh* mesh = ctx.scene->GetSrcObject<FbxMesh>(i);
        int polyCount = mesh->GetPolygonCount();
        int edgeCount = mesh->GetMeshEdgeCount();

        // Only scan for n-gons when edge information hasn't already selected the
        // mesh: if edgeCount > 0 we triangulate regardless, so the scan is wasted.
        bool hasNgon = false;
        if (edgeCount <= 0) {
            for (int p = 0; p < polyCount; ++p) {
                if (mesh->GetPolygonSize(p) > 4) {
                    hasNgon = true;
                    break;
                }
            }
        }

        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "importFbx: mesh[%lu]=%s polycount=%d edgecount=%d hasNgon=%d\n",
                     i,
                     mesh->GetName(),
                     polyCount,
                     edgeCount,
                     hasNgon);

        if (edgeCount > 0 || hasNgon) {
            meshes.push_back(mesh);
        }
    }

    if (meshes.size() > 0) {
        FbxGeometryConverter conv(ctx.fbx->manager);

        // triangulate each mesh
        for (auto mesh : meshes) {
            // Triangulate with pReplace=false, then destroy the original.
            //
            // pReplace=true crashes inside FBX SDK 2020.3.9 FbxGeometryConverter::Triangulate
            // (DisconnectDstObject -> FbxPropertyHandle::GetPageDataPtr null deref) for
            // skinned meshes - both the legacy and the new triangulation paths go through
            // the same crashy replacement bookkeeping. pReplace=false adds a new triangulated
            // mesh as the node's default attribute and leaves the original alone, which we
            // then destroy ourselves. The new mesh has its own preserved skin/shape channels.
            FbxNodeAttribute* tri =
              conv.Triangulate(mesh, /* pReplace = */ false, /* pLegacy = */ true);
            if (tri && tri != mesh) {
                mesh->Destroy();
            }
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
        if (options.triangulateMeshes)
            triangulateMeshes(ctx);
        loadAnimLayers(ctx);
        importFBXSkeletons(ctx);
        importFbxNodeHierarchy(ctx);
        markAnimatedSkeletonNodes(ctx);
        setSkeletonParents(ctx);
    }

    return true;
}
}
