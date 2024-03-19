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
#include <common.h>
#include <fstream>
#include <images.h>
#include <iomanip>
#include <materials.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdSkel/utils.h>
#include <usdData.h>

using namespace PXR_NS;
using namespace fbxsdk;
namespace adobe::usd {

struct ImportFbxContext
{
    const ImportFbxOptions* options;
    UsdStageRefPtr stage;
    UsdData* usd;
    Fbx* fbx;
    FbxScene* scene;

    std::unordered_map<FbxMesh*, int> meshes;
    std::unordered_map<FbxObject*, int> materials;

    // Maps a mesh index to a skin index, if the mesh is skinned.
    std::unordered_map<int, int> meshSkinsMap;
    // Maps an FbxNode* to a joint index in a skeleton. We expect no repeated entries.
    std::unordered_map<FbxNode*, size_t> bonesMap;
    // Maps an FbxNode* to a skeleton index. No repeated entries expected.
    std::unordered_map<FbxNode*, size_t> skeletonsMap;

    // Maps an FbxNode* (parent) to a list of FbxSkeleton* (children of parent)
    std::unordered_map<FbxNode*, std::vector<FbxSkeleton*>> skelRootsMap;

    // A cache of all anim layers
    std::vector<FbxAnimLayer*> animLayers;
};

// Metadata on USD will be stored uniformily in the CustomLayerData dictionary.
void
importMetadata(ImportFbxContext& ctx)
{
    ctx.usd->metadata.SetValueAtPath("generator", PXR_NS::VtValue("Adobe usdFbx 1.0"));
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

void
importFbxTransform(ImportFbxContext& ctx,
                   FbxNode* fbxNode,
                   Node& node,
                   GfVec3d& t,
                   GfQuatf& r,
                   GfVec3f& s)
{
    bool flatTransformMatrix = true;

    if (flatTransformMatrix) {
        FbxAMatrix fbxTransform = fbxNode->EvaluateLocalTransform();
        GfMatrix4d transform;
        transform[0][0] = fbxTransform.mData[0][0];
        transform[0][1] = fbxTransform.mData[0][1];
        transform[0][2] = fbxTransform.mData[0][2];
        transform[0][3] = fbxTransform.mData[0][3];
        transform[1][0] = fbxTransform.mData[1][0];
        transform[1][1] = fbxTransform.mData[1][1];
        transform[1][2] = fbxTransform.mData[1][2];
        transform[1][3] = fbxTransform.mData[1][3];
        transform[2][0] = fbxTransform.mData[2][0];
        transform[2][1] = fbxTransform.mData[2][1];
        transform[2][2] = fbxTransform.mData[2][2];
        transform[2][3] = fbxTransform.mData[2][3];
        transform[3][0] = fbxTransform.mData[3][0];
        transform[3][1] = fbxTransform.mData[3][1];
        transform[3][2] = fbxTransform.mData[3][2];
        transform[3][3] = fbxTransform.mData[3][3];

        node.transform = transform;
        node.hasTransform = true;
    }

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
// in UsdData, to drive the instantiation of a UsdGeomMesh later in layerWrite.
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
            node.skinnedMeshes[skeletonIndex].push_back(meshIndex);
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
    for (size_t i = 0; i < elementUVsCount; i++) {
        FbxGeometryElementUV* elementUVs = fbxMesh->GetElementUV(i);
        if (elementUVs == nullptr) {
            TF_WARN("Mesh[%s].uvs[%lu] is null. Skipping\n", mesh.name.c_str(), i);
            continue;
        }
        if (i >= 1) {
            TF_WARN("Mesh[%s].uvs[%lu] Multiple uvs not supported\n", mesh.name.c_str(), i);
            break;
        }
        mesh.uvs.interpolation = fbxGetInterpolation(elementUVs->GetMappingMode());
        FbxLayerElementArrayTemplate<FbxVector2>& uvs = elementUVs->GetDirectArray();
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: uvs size %d\n", uvs.GetCount());
        mesh.uvs.values.resize(uvs.GetCount());
        for (int j = 0; j < uvs.GetCount(); j++) {
            mesh.uvs.values[j] =
              GfVec2f{ static_cast<float>(uvs[j][0]), static_cast<float>(uvs[j][1]) };
        }
        if (elementUVs->GetReferenceMode() != FbxLayerElement::EReferenceMode::eDirect) {
            FbxLayerElementArrayTemplate<int>& uvIndices = elementUVs->GetIndexArray();
            size_t uvIndicesCount = uvIndices.GetCount();
            mesh.uvs.indices.resize(uvIndicesCount);
            for (size_t j = 0; j < uvIndicesCount; j++) {
                mesh.uvs.indices[j] = uvIndices[j];
            }
        }
    }

    // Color
    int displayColorCount = fbxMesh->GetElementVertexColorCount();
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
            colorSet.values[j] = GfVec3f{ static_cast<float>(fbxColors[j][0]),
                                          static_cast<float>(fbxColors[j][1]),
                                          static_cast<float>(fbxColors[j][2]) };
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
    for (int i = 0; i < skinCount;
         i++) { // shouldn't really expect > 1 deformer! it would overwrite our Mesh
        FbxSkin* skin = FbxCast<FbxSkin>(fbxMesh->GetDeformer(i, FbxDeformer::eSkin));

        int controlPointsCount = fbxMesh->GetControlPointsCount();
        std::vector<std::vector<int>> indexes(controlPointsCount);
        std::vector<std::vector<float>> weights(controlPointsCount);

        // set default link mode
        FbxCluster::ELinkMode linkMode = FbxCluster::ELinkMode::eNormalize;
        int clusterCount = skin->GetClusterCount();
        for (int j = 0; j < clusterCount; j++) {
            FbxCluster* cluster = skin->GetCluster(j);
            FbxNode* link = cluster->GetLink();

            size_t jointIndex = ctx.bonesMap[link];
            size_t skeletonIndex = ctx.skeletonsMap[link];

            // if the linkMode for any cluster is not eNormalize, then we will disable weight
            // normalization
            FbxCluster::ELinkMode clusterLinkMode = cluster->GetLinkMode();
            if (FbxCluster::ELinkMode::eNormalize != clusterLinkMode)
                linkMode = clusterLinkMode;

            if (j == 0) {
                ctx.meshSkinsMap[meshIndex] = skeletonIndex;
                node.skinnedMeshes[skeletonIndex].push_back(meshIndex);
                isSkinnedMesh = true;
            }

            // Set the bindTransform for the joint
            Skeleton& skeleton = ctx.usd->skeletons[skeletonIndex];
            FbxAMatrix linkTransform;
            cluster->GetTransformLinkMatrix(linkTransform);

            skeleton.bindTransforms[jointIndex] = GetUSDMatrixFromFBX(linkTransform);

            if (jointIndex == 0) {
                TF_DEBUG_MSG(FILE_FORMAT_FBX, "JOINT 0: link:[%s]\n", link->GetName());

                // set the mesh geomBindTransform based on the root joint cluster transform
                FbxAMatrix geomBindTransform;
                cluster->GetTransformMatrix(geomBindTransform);
                mesh.geomBindTransform = GetUSDMatrixFromFBX(geomBindTransform);
            }

            int clusterControlPointIndicesCount = cluster->GetControlPointIndicesCount();
            int* clusterControlPointIndices = cluster->GetControlPointIndices();
            double* pointsWeights = cluster->GetControlPointWeights();
            for (int k = 0; k < clusterControlPointIndicesCount; k++) {
                int controlPointIndex = clusterControlPointIndices[k];
                double influenceWeight = pointsWeights[k];
                indexes[controlPointIndex].push_back(jointIndex);
                weights[controlPointIndex].push_back(influenceWeight);
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

    int materialCount = fbxMesh->GetNode()->GetMaterialCount();
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
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "byControlPoint material mapping mode not supported\n");
        } else if (mappingMode == FbxLayerElement::EMappingMode::eByPolygonVertex) {
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "byPolygonVertex material mapping mode not supported\n");
        } else if (mappingMode == FbxLayerElement::EMappingMode::eByPolygon) {
            for (int i = 0; i < materialCount; i++) {
                auto [subsetIndex, subset] = ctx.usd->addSubset(meshIndex);
                FbxSurfaceMaterial* fbxMaterial = fbxMesh->GetNode()->GetMaterial(i);
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
            FbxSurfaceMaterial* fbxMaterial = fbxMesh->GetNode()->GetMaterial(i);
            const auto& it = ctx.materials.find(fbxMaterial);
            if (it != ctx.materials.end()) {
                mesh.material = it->second;
            }
        }
    }
    printMesh("importFbx:", mesh, DEBUG_TAG);
    return true;
}

TfToken
fbxWrapModeToToken(FbxTexture::EWrapMode wrap)
{
    return (wrap == FbxTexture::EWrapMode::eRepeat) ? AdobeTokens->repeat : AdobeTokens->clamp;
}

void
importPropFileTexture(const std::unordered_map<FbxObject*, size_t>& textures,
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
            importPropFileTexture(textures, texture, input, channel);
        } else {
            importPropFileTexture(textures, texture, input, channel);
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
LookForImplementation(FbxSurfaceMaterial* m)
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
importFbxMaterials(ImportFbxContext& ctx)
{
    std::unordered_map<FbxObject*, size_t> textures;
    std::vector<ImageAsset> images(ctx.scene->GetTextureCount());
    const std::string parentPath = TfGetPathName(ctx.fbx->filename);
    for (int i = 0; i < ctx.scene->GetTextureCount(); i++) {
        FbxTexture* texture = ctx.scene->GetTexture(i);
        FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(texture);
        if (fileTexture == nullptr)
            continue;
        std::string filename = fileTexture->GetFileName();
        auto embedded = ctx.fbx->embeddedData.find(filename);
        bool isEmbedded = embedded != ctx.fbx->embeddedData.end();
        if (isEmbedded) {
            // If the texture is embedded, the filename may be a file path for a different OS. We
            // can't use the TfGetBaseName() function below (which is platform specific) to extract
            // just the file name. Instead we look for either a forward slash or backslash character
            // as delimiters.
            std::string::size_type i = filename.find_last_of("\\/");
            if (i == filename.size() - 1) { // ends in directory delimiter
                filename = filename.substr(0, i);
                i = filename.find_last_of("\\/");
            }
            if (i != std::string::npos)
                filename = filename.substr(i + 1);
        } else if (!TfPathExists(filename)) {
            TF_DEBUG_MSG(FILE_FORMAT_FBX,
                         "FBX image not found at \"%s\", attempt to find beside the fbx file\n",
                         filename.c_str());
            std::string siblingFilename = parentPath + TfGetBaseName(filename);
            if (!TfPathExists(siblingFilename)) {
                TF_WARN("FBX image \"%s\" not found in current path or relative to source file",
                        filename.c_str());
                continue;
            } else {
                filename = siblingFilename;
            }
        }
        textures[texture] = i;

        const std::string name = TfGetBaseName(filename);
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
                if (TfIsRelativePath(filename)) {
                    filename = parentPath + filename;
                }
                std::ifstream file(filename, std::ios::binary);
                if (!file.is_open()) {
                    TF_RUNTIME_ERROR("Failed to open file \"%s\"", filename.c_str());
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

        const FbxImplementation* imp = LookForImplementation(material);
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
    auto [cameraIndex, camera] = ctx.usd->addCamera();
    auto [nodeIndex, node] = ctx.usd->getParent(parent);
    node.camera = cameraIndex;

    // If the camera doesn't have a specific look-at target, we need to compensate
    // for the default orientation of the fbx camera looking down the X axis.
    FbxNode* cameraNode = fbxCamera->GetNode();
    if (cameraNode && !cameraNode->GetTarget()) {
        // for FBX, the camera is oriented to look down the -X axis. We apply
        // a Y-axis rotation to orient the camera to look down the -Z axis
        GfMatrix4d additionalRotation =
          GfMatrix4d(1.).SetRotate(GfRotation(GfVec3d::YAxis(), -90.0));
        node.transform = additionalRotation * node.transform;
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

        // FBX time unit is seconds so we set the USD timeCodesPerSecond to 1.0.
        ctx.usd->timeCodesPerSecond = 1;
        ctx.usd->minTime = localStartSeconds;
        ctx.usd->maxTime = localStopSeconds;
        ctx.usd->hasAnimations = true;

        size_t animLayersCount = stack->GetMemberCount<FbxAnimLayer>();
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: Animation stack: %s \n", stack->GetName());
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: \tLocalStart: %f s \n", localStartSeconds);
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: \tLocalStop: %f s \n", localStopSeconds);
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFBX: \tanimLayersCount: %zu\n", animLayersCount);

        for (size_t animLayerIndex = 0; animLayerIndex < animLayersCount; animLayerIndex++) {
            FbxAnimLayer* layer = stack->GetMember<FbxAnimLayer>(animLayerIndex);
            ctx.animLayers.push_back(layer);
            TF_DEBUG_MSG(
              FILE_FORMAT_FBX, "importFbx: found animation layer: %s \n", layer->GetName());
        }
    }
    return true;
}

void
addAnimCurveFrameTimes(FbxAnimCurve* curve, std::unordered_map<FbxLongLong, FbxTime>& frames)
{
    if (curve != nullptr) {
        int keyCount = curve->KeyGetCount();
        for (int i = 0; i < keyCount; i++) {
            FbxAnimCurveKey animKey = curve->KeyGet(i);
            FbxTime time = animKey.GetTime();
            FbxLongLong frameKey = time.Get();
            if (frames.find(frameKey) == frames.end()) {
                frames[frameKey] = time;
            }
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
importFbxSkeleton(ImportFbxContext& ctx,
                  FbxNode* parent,
                  const std::vector<FbxSkeleton*>& skelRoots)
{
    auto [skeletonIndex, skeleton] = ctx.usd->addSkeleton();

    std::unordered_map<FbxLongLong, FbxTime> frames;
    std::vector<FbxNode*> animatedNodes;
    VtTokenArray jointPaths;
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

        FbxAMatrix localTransform = fbxNode->EvaluateLocalTransform();
        FbxAMatrix globalTransform = fbxNode->EvaluateGlobalTransform();
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
            animatedNodes.push_back(fbxNode);
            jointPaths.push_back(jointPathToken);
            for (FbxAnimLayer* animLayer : ctx.animLayers) {
                addAnimCurveFrameTimes(fbxNode->LclTranslation.GetCurve(animLayer), frames);
                addAnimCurveFrameTimes(fbxNode->LclRotation.GetCurve(animLayer), frames);
                addAnimCurveFrameTimes(fbxNode->LclScaling.GetCurve(animLayer), frames);
            }
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "Importing animation for bone %s \n", fbxNode->GetName());
        }

        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "import joint %s:\t %s\n",
                     fbxNode->GetName(),
                     skeleton.joints[jointIndex].GetText());
        for (int i = 0; i < fbxNode->GetChildCount(); i++) {
            importFbxBone(skeletonIndex, skeleton, fbxNode->GetChild(i), jointPath);
        }
    };

    // There may be multiple root joints so add each root to skeleton
    for (auto skel : skelRoots) {
        importFbxBone(skeletonIndex, skeleton, skel->GetNode(), SdfPath());
    }

    if (animatedNodes.size()) {
        TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: assembling animation data\n");
        auto [animationIndex, animation] = ctx.usd->addAnimation();
        skeleton.animations.push_back(animationIndex);
        animation.joints = jointPaths;
        animation.times.resize(frames.size());
        animation.translations.resize(frames.size(), VtArray<GfVec3f>(animatedNodes.size()));
        animation.rotations.resize(frames.size(), VtArray<GfQuatf>(animatedNodes.size()));
        animation.scales.resize(frames.size(), VtArray<GfVec3h>(animatedNodes.size()));
        for (size_t i = 0; i < animatedNodes.size(); i++) {
            FbxNode* fbxNode = animatedNodes[i];
            size_t j = 0;
            for (auto frameIt : frames) {
                FbxTime frameTime = frameIt.second;
                FbxAMatrix localTransform = fbxNode->EvaluateLocalTransform(frameTime);
                GfMatrix4d usdLocalTransform =
                  ConvertMatrix4<FbxAMatrix, GfMatrix4d>(localTransform);
                GfVec3f translation;
                GfQuatf rotation;
                GfVec3h scale;
                UsdSkelDecomposeTransform(usdLocalTransform, &translation, &rotation, &scale);
                animation.times[j] = frameTime.GetSecondDouble();
                animation.translations[j][i] = translation;
                animation.rotations[j][i] = rotation;
                animation.scales[j][i] = scale;
                j++;
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
    ctx.skelRootsMap.clear();
    for (int i = 0; i < skeletonCount; i++) {
        FbxSkeleton* fbxSkeleton = ctx.scene->GetSrcObject<FbxSkeleton>(i);
        if (fbxSkeleton->IsSkeletonRoot()) {
            FbxNode* node = fbxSkeleton->GetNode();
            ctx.skelRootsMap[node->GetParent()].push_back(fbxSkeleton);
        }
    }

    for (auto const& [root, skelRoots] : ctx.skelRootsMap) {
        importFbxSkeleton(ctx, root, skelRoots);
    }

    return true;
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
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "importFbx: node %s\n", node.name.c_str());
    GfVec3d t(0);
    GfQuatf r(0);
    GfVec3f s(1);
    importFbxTransform(ctx, fbxNode, node, t, r, s);

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
        auto attrType = attribute->GetAttributeType();
        switch (attrType) {
            case FbxNodeAttribute::eMesh: {
                FbxMesh* fbxMesh = FbxCast<FbxMesh>(attribute);
                // If the mesh is skinned, we clear the transform as it will be placed
                // at the root of the scene.

                // XXX There are still issues with importing FBX skinned meshes that do not live at
                // the root level which needs to be addressed. USD wants the skinned mesh to be
                // placed next to the skeleton and GLTF wants skinned meshes to be at the root
                // level. FBX maps the world space skeletal transformations to the local local space
                // of the mesh by applying the inv(localToWorld) of the mesh to the skeleton's
                // parentToWorld matrix. It is not yet understood how to handle this with the
                // FBX->USD conversion. This results in the mesh missing the transformation from
                // skeletal space to world space.
                if (isSkinnedMesh(fbxMesh)) {
                    node.transform = GfMatrix4d(1);
                    node.hasTransform = false;
                }
                importFbxMesh(ctx, fbxMesh, parentIndex);
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
        importFbxNodes(ctx, fbxNode->GetChild(i), nodeIndex);
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

    importMetadata(ctx);
    importFbxSettings(ctx);

    if (options.importMaterials) {
        importFbxMaterials(ctx);
    }
    if (options.importGeometry) {
        triangulateMeshes(ctx);
        loadAnimLayers(ctx);
        importFBXSkeletons(ctx);
        importFbxNodes(ctx, ctx.scene->GetRootNode(), -1);
    }
    return true;
}
}
