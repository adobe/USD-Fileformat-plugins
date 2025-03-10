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
#include "fbxExport.h"
#include "debugCodes.h"
#include <fbxsdk.h>
#include <fileformatutils/layerWriteShared.h>
#include <fileformatutils/common.h>
#include <fileformatutils/images.h>
#include <fileformatutils/materials.h>
#include <fileformatutils/usdData.h>

#include <optional>

using namespace PXR_NS;
namespace adobe::usd {

// Shininess max value derived from Cosine Power
// https://help.autodesk.com/view/MAYAUL/2025/ENU/?guid=GUID-3EDEB1B3-4E48-485A-9714-9998F6E4944D
static constexpr float MAX_FBX_SHININESS = 100.f;

struct ExportFbxAnimStackData
{
    FbxAnimStack* animStack = nullptr;
    // We will export all animations to this layer. For currently unknown reasons,
    // attempting to export more than one layer causes layers beyond the first to be ignored when
    // loading into other applications
    FbxAnimLayer* animLayer = nullptr;
};

struct ExportFbxContext
{
    UsdData* usd = nullptr;
    Fbx* fbx = nullptr;
    std::vector<FbxSurfaceMaterial*> materials;
    std::vector<FbxMesh*> meshes;
    std::vector<FbxCamera*> cameras;
    std::vector<FbxLight*> lights;
    std::vector<FbxNode*> skeletons;
    std::string exportParentPath;
    bool hasYUp = true;
    bool convertColorSpaceToSRGB = false;

    std::vector<ExportFbxAnimStackData> animStackData;
};

bool
exportFbxMapping(const TfToken& interpolation, FbxGeometryElement::EMappingMode& mapping)
{
    if (interpolation == UsdGeomTokens->faceVarying) {
        mapping = FbxGeometryElement::eByPolygonVertex;
        return true;
    } else if (interpolation == UsdGeomTokens->uniform) {
        mapping = FbxGeometryElement::eByPolygon;
        return true;
    } else if (interpolation == UsdGeomTokens->vertex) {
        mapping = FbxGeometryElement::eByControlPoint;
        return true;
    } else if (interpolation == UsdGeomTokens->constant) {
        mapping = FbxGeometryElement::eAllSame;
        return true;
    }
    mapping = FbxGeometryElement::eByControlPoint;
    return false;
}

// template <class T>
// bool GetDefaultAttribute(UsdAttribute &attribute, T *value)
// {
//     if (!attribute.Get<T>(value))
//     {
//         if (!attribute.Get<T>(value, UsdTimeCode::Default()))
//         {
//             auto startTime = attribute.GetPrim().GetStage()->GetStartTimeCode();

//             if (!attribute.Get<T>(value, startTime))
//             {
//                 return false;
//             }
//         }
//     }

//     return true;
// }

// void AddNodesReference(UsdStageRefPtr stagePtr,
//                         const ExportContext &context,
//                         const UsdTimeCode &timeCode)
// {
//     for (auto nodePairs : context.GetReferencingNodes())
//     {
//         auto primPath = nodePairs.NodePath;
//         FbxNode *node;
//         if (context.TryGetFBXNode(primPath, &node))
//         {
//             auto prim = stagePtr->GetPrimAtPath(primPath);
//             if ((nodePairs.Type & ReferenceNode::Instancing) != 0)
//                 ApplyPointInstancerToNode(prim, node, context, timeCode);
//         }
//     }
// }

bool
exportFbxSettings(ExportFbxContext& ctx)
{
    // eParityOdd means: if up = x then front = z, if up = y then front = z, if up = z then front =
    // y
    FbxAxisSystem::EUpVector up;
    FbxAxisSystem::EFrontVector front;
    if (UsdGeomTokens->y == ctx.usd->upAxis) {
        ctx.hasYUp = true;
        // up = +y, front = z, right = +x
        up = FbxAxisSystem::EUpVector::eYAxis;
        front = FbxAxisSystem::EFrontVector::eParityOdd;
    } else {
        ctx.hasYUp = false;
        // up = +z, front = -y, right = +x
        up = FbxAxisSystem::EUpVector::eZAxis;
        // strange, but FbxAxisSystem really expects negative
        front =
          static_cast<FbxAxisSystem::EFrontVector>(-1 * FbxAxisSystem::EFrontVector::eParityOdd);
    }
    // USD defaults to right-handed. We neeed to check indidivual prims that might override to
    // left-handed.
    FbxAxisSystem::ECoordSystem coordSystem = FbxAxisSystem::ECoordSystem::eRightHanded;
    FbxAxisSystem axisSystem = FbxAxisSystem(up, front, coordSystem);
    axisSystem.ConvertScene(ctx.fbx->scene);

    float cmPerUnit = ctx.usd->metersPerUnit > 0 ? ctx.usd->metersPerUnit * 100 : 1;

    FbxSystemUnit systemUnits(cmPerUnit, 1);
    systemUnits.ConvertScene(ctx.fbx->scene);
    TF_DEBUG_MSG(FILE_FORMAT_FBX,
                 "FBX scene settings { upAxis: %s, cmPerUnit: %f }\n",
                 ctx.hasYUp ? "+y" : "+z",
                 cmPerUnit);
    return true;
}

/**
 * A helper function to extract animation data from the USD and properly initialize the FBX context
 * with that data. Curves will be created within the given animation layer, associated with the
 * given animation property
 *
 * @param animLayer The animation layer to which the animation data will be added. This must not be
 * null.
 * @param property The property that corresponds with the animation data, to which animation curves
 * will be attached. This property should be associated with the node that is animated, and
 * currently must be of type FbxDouble3. This usually will be LclTranslation, LclRotation, or
 * LclScaling. This must not be null
 * @param numTimeSamples The number of time samples that to extract
 * @param indexToKeyframe A function that takes a time index and returns a pair representing the
 * keyframe to store. The first element should be the FbxTime at that time sample, and the second
 * should be the animated value. This function will be queried for every time sample in the range,
 * and it must return a valid pair where the time is nonnegative and less than numTimeSamples.
 * @param errorString An optional pointer to a string that will be populated with an error message
 * if one occurs
 *
 * @return Whether the animation samples were successfully processed
 */
bool
extractAnimatedTransformationData(
  FbxAnimLayer* animLayer,
  FbxPropertyT<FbxDouble3>* property,
  size_t numTimeSamples,
  const std::function<std::pair<FbxTime, FbxDouble3>(size_t)>& indexToKeyframe,
  std::string* errorString = nullptr)
{
    if (!animLayer) {
        if (errorString) {
            *errorString = "Cannot extract animation data with a null animation layer";
        }
        return false;
    }

    if (!property) {
        if (errorString) {
            *errorString = "Cannot extract animation data with a null property";
        }
        return false;
    }

    FbxAnimCurveNode* curveNode = property->GetCurveNode(animLayer, true);

    if (!curveNode) {
        if (errorString) {
            *errorString = "Unable to get or create an animation curve node";
        }
        return false;
    }

    // We must create an animation curve for each component of the translation, and
    // indicate that they are actively being modified
    for (size_t i = 0; i < 3; ++i) {
        curveNode->CreateCurve(curveNode->GetName(), i)->KeyModifyBegin();
    }

    std::array<FbxAnimCurve*, 3> curves = {
        curveNode->GetCurve(0),
        curveNode->GetCurve(1),
        curveNode->GetCurve(2),
    };

    for (size_t timeIndex = 0; timeIndex < numTimeSamples; ++timeIndex) {
        auto [time, value] = indexToKeyframe(timeIndex);

        // Add a keyframe in each channel
        std::array<int, 3> curveIndices = {
            curves[0]->KeyAdd(time),
            curves[1]->KeyAdd(time),
            curves[2]->KeyAdd(time),
        };

        // Set the keyframe values
        for (size_t channel = 0; channel < 3; ++channel) {
            curves[channel]->KeySet(
              curveIndices[channel], time, value[channel], FbxAnimCurveDef::eInterpolationConstant);
        }
    }

    // Indicate that we have finished modifying the curves
    for (size_t channel = 0; channel < 3; ++channel) {
        curves[channel]->KeyModifyEnd();
    }

    return true;
}
void
exportFbxAnimationTracks(ExportFbxContext& ctx)
{
    if (ctx.usd->hasAnimations) {
        ctx.animStackData.resize(ctx.usd->animationTracks.size());
        for (int animationTrackIndex = 0; animationTrackIndex < ctx.usd->animationTracks.size();
             animationTrackIndex++) {
            const AnimationTrack& track = ctx.usd->animationTracks[animationTrackIndex];
            ExportFbxAnimStackData& exportAnimStackData = ctx.animStackData[animationTrackIndex];

            // Create anim stack
            exportAnimStackData.animStack = FbxAnimStack::Create(
              ctx.fbx->scene, getNodeName(ctx.usd->animationTracks[animationTrackIndex]).c_str());

            // Create anim layer
            std::string animLayerName = "AnimLayer" + std::to_string(animationTrackIndex);
            exportAnimStackData.animLayer =
              fbxsdk::FbxAnimLayer::Create(ctx.fbx->scene, animLayerName.c_str());
            exportAnimStackData.animStack->AddMember(exportAnimStackData.animLayer);
        }
    }
}

bool
exportFbxTransform(ExportFbxContext& ctx, const Node& node, FbxNode* fbxNode)
{
    if (!fbxNode) {
        TF_WARN("ExportFbxTransform: Cannot export node %s transform to null FBX node\n",
                getNodeName(node).c_str());
        return false;
    }

    // For use in returning the errors from helper functions
    std::string errorStr;

    double secondsPerTimeCode =
      ctx.usd->timeCodesPerSecond != 0.0 ? 1.0 / ctx.usd->timeCodesPerSecond : 1.0;

    // We only calculate the transformation matrix if needed, which is if the USD node's
    // hasTransform property is true AND if at least one component of the transformation is not
    // animated
    std::optional<FbxAMatrix> transformation;

    // Helper function calculates the transformation matrix, only to be called if needed
    auto getTransformationMatrix = [node]() {
        FbxAMatrix localTransform = GetFBXMatrixFromUSD(node.transform);
        FbxAMatrix additionalRotation{};

        // Account for FBX's different coordinate system, and take the inverse on import. See
        // comment at definition of CAMERA_ROTATION_OFFSET_EXPORT for more information
        if (node.camera >= 0) {
            TF_DEBUG_MSG(
              FILE_FORMAT_FBX,
              "exportFbxTransform: Applying 90 degree rotation around Y axis to camera node\n");
            additionalRotation.SetR(CAMERA_ROTATION_OFFSET_EXPORT);
        }
        // Account for FBX's different coordinate system, and take the inverse on import. See
        // comment at definition of LIGHT_ROTATION_OFFSET_EXPORT for more information
        if (node.light >= 0) {
            TF_DEBUG_MSG(
              FILE_FORMAT_FBX,
              "exportFbxTransform: Applying 90 degree rotation around X axis to light node\n");
            additionalRotation.SetR(LIGHT_ROTATION_OFFSET_EXPORT);
        }
        return localTransform * additionalRotation;
    };

    // Translation

    if (node.hasTransform) {
        // Extract the translation from the transformation matrix

        // No need to check if transformation has already been assigned
        transformation = getTransformationMatrix();
        FbxVector4 translation = transformation->GetT();
        fbxNode->LclTranslation.Set(translation);

    } else {
        // Copy the translation value from the USD node

        // This code path will likely never be run, since LayerRead currently always converts to
        // matrix transformations (with getLocalTransformation). If that is changed, this should
        // handle alternate situations

        fbxNode->LclTranslation.Set(
          FbxDouble3(node.translation[0], node.translation[1], node.translation[2]));
    }

    for (int animationTrackIndex = 0; animationTrackIndex < node.animations.size();
         animationTrackIndex++) {
        const NodeAnimation& nodeAnimation = node.animations[animationTrackIndex];
        ExportFbxAnimStackData& exportAnimStackData = ctx.animStackData[animationTrackIndex];

        if (nodeAnimation.translations.times.size() > 0) {
            // Extract the translation animation data

            // Helper function gets the translation time and value for a given index
            std::function<std::pair<FbxTime, FbxDouble3>(size_t)> timeIndexToKeyframe =
              [&](size_t timeIndex) {
                  // Convert the time to FBX time
                  float time = nodeAnimation.translations.times[timeIndex];
                  FbxTime fbxTime;
                  fbxTime.SetSecondDouble(time * secondsPerTimeCode);

                  return std::make_pair(
                    fbxTime,
                    FbxDouble3(nodeAnimation.translations.values[timeIndex][0],
                               nodeAnimation.translations.values[timeIndex][1],
                               nodeAnimation.translations.values[timeIndex][2]));
              };
            if (!extractAnimatedTransformationData(exportAnimStackData.animLayer,
                                                   &fbxNode->LclTranslation,
                                                   nodeAnimation.translations.times.size(),
                                                   timeIndexToKeyframe,
                                                   &errorStr)) {
                TF_WARN("ExportFbxTransform: Failed to extract translation animation data for node "
                        "%s: %s\n",
                        getNodeName(node).c_str(),
                        errorStr.c_str());
            }
        }
    }

    // Rotation

    if (node.hasTransform) {
        // Extract the rotation from the transformation matrix

        if (!transformation) {
            transformation = getTransformationMatrix();
        }

        FbxVector4 rotation = transformation->GetR();
        fbxNode->LclRotation.Set(rotation);

    } else {
        // Convert the USD node's quaternion to Euler angles and use the resulting value
        FbxQuaternion fbxQuat = GetFBXQuat(node.rotation);

        // This code path will likely never be run, since LayerRead currently always converts to
        // matrix transformations (with getLocalTransformation). If that is changed, this should
        // handle alternate situations, although the camera and light transformations have not
        // been properly tested

        if (node.camera >= 0) {
            FbxQuaternion additionalQuat;

            TF_DEBUG_MSG(
              FILE_FORMAT_FBX,
              "exportFbxTransform: Applying 90 degree rotation around Y axis to camera node\n");
            FbxVector4 rotation(CAMERA_ROTATION_OFFSET_EXPORT);
            additionalQuat.ComposeSphericalXYZ(rotation);
            fbxQuat = fbxQuat * additionalQuat;
        }
        if (node.light >= 0) {
            FbxQuaternion additionalQuat;

            TF_DEBUG_MSG(
              FILE_FORMAT_FBX,
              "exportFbxTransform: Applying 90 degree rotation around X axis to light node\n");
            FbxVector4 rotation(LIGHT_ROTATION_OFFSET_EXPORT);
            additionalQuat.ComposeSphericalXYZ(rotation);
            fbxQuat = fbxQuat * additionalQuat;
        }

        FbxVector4 euler;
        euler.SetXYZ(fbxQuat);
        fbxNode->LclRotation.Set(FbxDouble3(euler[0], euler[1], euler[2]));
    }

    for (int animationTrackIndex = 0; animationTrackIndex < node.animations.size();
         animationTrackIndex++) {
        const NodeAnimation& nodeAnimation = node.animations[animationTrackIndex];
        ExportFbxAnimStackData& exportAnimStackData = ctx.animStackData[animationTrackIndex];

        if (nodeAnimation.rotations.times.size() > 0) {
            // Extract the rotation animation data

            // Helper function gets the rotation time and value for a given index
            std::function<std::pair<FbxTime, FbxDouble3>(size_t)> timeIndexToKeyframe =
              [&](size_t timeIndex) {
                  // Convert the time to FBX time
                  float time = nodeAnimation.rotations.times[timeIndex];
                  FbxTime fbxTime;
                  fbxTime.SetSecondDouble(time * secondsPerTimeCode);

                  // Convert the USD quaternion to FBX Euler angles
                  FbxQuaternion fbxQuat = GetFBXQuat(nodeAnimation.rotations.values[timeIndex]);
                  FbxVector4 euler;
                  euler.SetXYZ(fbxQuat);
                  FbxDouble3 rotation(euler[0], euler[1], euler[2]);

                  return std::make_pair(fbxTime, rotation);
              };

            if (!extractAnimatedTransformationData(exportAnimStackData.animLayer,
                                                   &fbxNode->LclRotation,
                                                   nodeAnimation.rotations.times.size(),
                                                   timeIndexToKeyframe,
                                                   &errorStr)) {
                TF_WARN(
                  "ExportFbxTransform: Failed to extract rotation animation data for node %s: %s\n",
                  getNodeName(node).c_str(),
                  errorStr.c_str());
            }
        }
    }

    // Scale

    if (node.hasTransform) {
        // Extract the scale from the transformation matrix

        // This code path will likely never be run, since LayerRead currently always converts to
        // matrix transformations (with getLocalTransformation). If that is changed, this should
        // handle alternate situations

        if (!transformation) {
            transformation = getTransformationMatrix();
        }

        FbxVector4 scale = transformation->GetS();
        fbxNode->LclScaling.Set(scale);

    } else {
        // Copy the scale value from the USD node
        fbxNode->LclScaling.Set(FbxDouble3(node.scale[0], node.scale[1], node.scale[2]));
    }

    for (int animationTrackIndex = 0; animationTrackIndex < node.animations.size();
         animationTrackIndex++) {
        const NodeAnimation& nodeAnimation = node.animations[animationTrackIndex];
        ExportFbxAnimStackData& exportAnimStackData = ctx.animStackData[animationTrackIndex];

        if (nodeAnimation.scales.times.size() > 0) {
            // Extract the scale animation data

            // Helper function gets the scale time and value for a given index
            std::function<std::pair<FbxTime, FbxDouble3>(size_t)> timeIndexToKeyframe =
              [&](size_t timeIndex) {
                  // Convert the time to FBX time
                  float time = nodeAnimation.scales.times[timeIndex];
                  FbxTime fbxTime;
                  fbxTime.SetSecondDouble(time * secondsPerTimeCode);

                  return std::make_pair(fbxTime,
                                        FbxDouble3(nodeAnimation.scales.values[timeIndex][0],
                                                   nodeAnimation.scales.values[timeIndex][1],
                                                   nodeAnimation.scales.values[timeIndex][2]));
              };

            if (!extractAnimatedTransformationData(exportAnimStackData.animLayer,
                                                   &fbxNode->LclScaling,
                                                   nodeAnimation.scales.times.size(),
                                                   timeIndexToKeyframe,
                                                   &errorStr)) {
                TF_WARN(
                  "ExportFbxTransform: Failed to extract scale animation data for node %s: %s\n",
                  getNodeName(node).c_str(),
                  errorStr.c_str());
            }
        }
    }
    return true;
}

void
setElementUVs(FbxMesh* fbxMesh, FbxGeometryElementUV* elementUvs, const Primvar<GfVec2f>& uvs)
{
    FbxGeometryElement::EMappingMode uvMapping;
    if (!exportFbxMapping(uvs.interpolation, uvMapping)) {
        TF_WARN("Uvs interpolation: %s not supported, defaulting to byControlPoint\n",
                uvs.interpolation.GetText());
    }

    elementUvs->SetMappingMode(uvMapping);
    int dataSize = 0;

    for (size_t i = 0; i < uvs.values.size(); i++) {
        GfVec2f x = uvs.values[i];
        FbxVector2 uv = FbxVector2(x[0], x[1]);
        elementUvs->GetDirectArray().Add(uv);
    }
    if (uvs.indices.size()) {
        elementUvs->SetReferenceMode(FbxGeometryElement::EReferenceMode::eIndexToDirect);
        dataSize = uvs.indices.size();
        for (size_t i = 0; i < uvs.indices.size(); i++) {
            elementUvs->GetIndexArray().Add(uvs.indices[i]);
        }
    } else {
        elementUvs->SetReferenceMode(FbxGeometryElement::EReferenceMode::eDirect);
        dataSize = uvs.values.size();
    }
    // TODO: do this check in usdutils instead
    int expectedDataSize = 0;
    if (uvs.interpolation == UsdGeomTokens->faceVarying) {
        expectedDataSize = fbxMesh->GetPolygonVertexCount();
    } else if (uvs.interpolation == UsdGeomTokens->uniform) {
        expectedDataSize = fbxMesh->GetPolygonCount();
    } else if (uvs.interpolation == UsdGeomTokens->vertex) {
        expectedDataSize = fbxMesh->GetControlPointsCount();
    } else if (uvs.interpolation == UsdGeomTokens->constant) {
        expectedDataSize = 1;
    }
    if (expectedDataSize != dataSize) {
        TF_WARN("Incorrect uvs length. Excepted: %d, Actual: %d, interp: %s\n",
                expectedDataSize,
                dataSize,
                uvs.interpolation.GetText());
    }
}

void
createMeshMaterial(ExportFbxContext& ctx, const Mesh& mesh, FbxMesh* fbxMesh)
{
    if (mesh.material >= 0) {
        FbxGeometryElementMaterial* elementMaterial = fbxMesh->CreateElementMaterial();
        elementMaterial->SetMappingMode(FbxGeometryElement::eAllSame);
        elementMaterial->SetReferenceMode(FbxGeometryElement::eDirect);
    }
}

void
bindMaterial(ExportFbxContext& ctx, const Mesh& mesh, FbxMesh* fbxMesh)
{
    if (mesh.material >= 0) {
        FbxSurfaceMaterial* material = ctx.materials[mesh.material];
        FbxNode* n = fbxMesh->GetNode();
        if (material && n)
            n->AddMaterial(material);
    }
}

bool
exportFbxMeshes(ExportFbxContext& ctx)
{
    ctx.meshes.resize(ctx.usd->meshes.size());
    for (size_t i = 0; i < ctx.usd->meshes.size(); i++) {
        const Mesh& m = ctx.usd->meshes[i];
        FbxMesh* fbxMesh = FbxMesh::Create(ctx.fbx->scene, getNodeName(m).c_str());
        if (fbxMesh != nullptr) {
            ctx.meshes[i] = fbxMesh;
            createMeshMaterial(ctx, m, fbxMesh);

            // Positions
            size_t k = 0;
            for (size_t j = 0; j < m.faces.size(); j++) {
                fbxMesh->BeginPolygon();
                for (int l = 0; l < m.faces[j]; l++) {
                    fbxMesh->AddPolygon(m.indices[k++]);
                }
                fbxMesh->EndPolygon();
            }
            fbxMesh->InitControlPoints(m.points.size());
            for (size_t j = 0; j < m.points.size(); j++) {
                GfVec3f p = m.points[j];
                fbxMesh->SetControlPointAt(FbxVector4(p[0], p[1], p[2]), j);
            }

            // Normals
            if (m.normals.values.size()) {
                FbxGeometryElement::EMappingMode normalMapping;
                if (!exportFbxMapping(m.normals.interpolation, normalMapping)) {
                    TF_WARN(
                      "Normals interpolation: %s not supported, defaulting to byControlPoint\n",
                      m.normals.interpolation.GetText());
                }

                FbxGeometryElementNormal* elementNormal = fbxMesh->CreateElementNormal();
                elementNormal->SetMappingMode(normalMapping);
                for (size_t j = 0; j < m.normals.values.size(); j++) {
                    GfVec3f n = m.normals.values[j];
                    FbxVector4 normal = FbxVector4(n[0], n[1], n[2]);
                    elementNormal->GetDirectArray().Add(normal);
                }
                if (m.normals.indices.size()) {
                    elementNormal->SetReferenceMode(
                      FbxGeometryElement::EReferenceMode::eIndexToDirect);
                    for (size_t j = 0; j < m.normals.indices.size(); j++) {
                        elementNormal->GetIndexArray().Add(m.normals.indices[j]);
                    }
                } else {
                    elementNormal->SetReferenceMode(FbxGeometryElement::EReferenceMode::eDirect);
                }
            }

            // Uvs
            if (m.uvs.values.size()) {
                FbxGeometryElementUV* elementUvs =
                  fbxMesh->CreateElementUV("st", FbxLayerElement::eUV);
                setElementUVs(fbxMesh, elementUvs, m.uvs);
                int numExtra = 0;
                for (auto const& uvs : m.extraUVSets) {
                    if (uvs.values.size()) {
                        std::string newName = "st" + std::to_string(numExtra + 1);
                        FbxGeometryElementUV* elementUvs =
                          fbxMesh->CreateElementUV(newName.c_str(), FbxLayerElement::eUV);
                        setElementUVs(fbxMesh, elementUvs, uvs);
                        numExtra++;
                    }
                }
            }

            // Colors and Opacities
            if (m.colors.size() || m.opacities.size()) {
                TfToken interpolation;
                VtIntArray indices;
                VtVec3fArray colorValues;
                VtFloatArray opacityValues;
                if (m.colors.size() && m.opacities.size()) {
                    interpolation = m.colors[0].interpolation;
                    indices = m.colors[0].indices;
                    colorValues = m.colors[0].values;
                    if (m.colors[0].values.size() == m.opacities[0].values.size()) {
                        opacityValues = m.opacities[0].values;
                    } else {
                        TF_WARN("Colors and opacities length differ. Dropping opacities\n");
                        opacityValues.assign(m.colors[0].values.size(), 1.0f);
                    }
                } else if (m.colors.size()) {
                    interpolation = m.colors[0].interpolation;
                    indices = m.colors[0].indices;
                    colorValues = m.colors[0].values;
                    opacityValues.assign(m.colors[0].values.size(), 1.0f);
                    TF_DEBUG_MSG(FILE_FORMAT_FBX, "Empty opacities, defaulting to 1.0\n");
                } else { // opacities.size()
                    interpolation = m.opacities[0].interpolation;
                    indices = m.opacities[0].indices;
                    colorValues.assign(m.opacities[0].values.size(), GfVec3f(1.0f));
                    TF_DEBUG_MSG(FILE_FORMAT_FBX, "Empty colors, defaulting to <1.0, 1.0, 1.0>\n");
                }

                FbxGeometryElement::EMappingMode colorMapping;
                if (!exportFbxMapping(interpolation, colorMapping)) {
                    TF_WARN("Color interpolation: %s not supported, defaulting to byControlPoint\n",
                            interpolation.GetText());
                }

                // Convert colors to sRGB if needed
                if (ctx.convertColorSpaceToSRGB) {
                    for (size_t j = 0; j < colorValues.size(); j++) {
                        colorValues[j][0] = linearToSRGB(colorValues[j][0]);
                        colorValues[j][1] = linearToSRGB(colorValues[j][1]);
                        colorValues[j][2] = linearToSRGB(colorValues[j][2]);
                    }
                }

                FbxGeometryElementVertexColor* vertexColor = fbxMesh->CreateElementVertexColor();
                vertexColor->SetMappingMode(colorMapping);
                if (indices.size()) {
                    vertexColor->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
                    vertexColor->GetIndexArray().SetCount(indices.size());
                    for (size_t j = 0; j < indices.size(); j++) {
                        vertexColor->GetIndexArray().SetAt(j, indices[j]);
                    }
                } else {
                    vertexColor->SetReferenceMode(FbxGeometryElement::eDirect);
                }
                vertexColor->GetDirectArray().SetCount(colorValues.size());
                for (size_t j = 0; j < colorValues.size(); j++) {
                    GfVec3f c = colorValues[j];
                    FbxDouble4 vector = FbxDouble4(c[0], c[1], c[2], opacityValues[j]);
                    vertexColor->GetDirectArray().SetAt(j, vector);
                }
            }
        } else {
            TF_WARN("Failed to create mesh %s\n", getNodeName(m).c_str());
        }
    }
    return true;
}

static float mm2inch = 1.0f / 25.4f;

void
exportFbxCameras(ExportFbxContext& ctx)
{
    ctx.cameras.resize(ctx.usd->cameras.size());
    for (size_t i = 0; i < ctx.usd->cameras.size(); i++) {
        const Camera& c = ctx.usd->cameras[i];

        FbxCamera* fbxCamera = FbxCamera::Create(ctx.fbx->scene, "camera");
        ctx.cameras[i] = fbxCamera;

        FbxCamera::EProjectionType p = c.projection == GfCamera::Projection::Perspective
                                         ? FbxCamera::EProjectionType::ePerspective
                                         : FbxCamera::EProjectionType::eOrthogonal;

        fbxCamera->SetName(getNodeName(c).c_str());
        fbxCamera->ProjectionType.Set(p);
        fbxCamera->FocalLength.Set(c.f);
        fbxCamera->FieldOfView.Set(c.fov);
        fbxCamera->SetApertureMode(FbxCamera::EApertureMode::eVertical);
        fbxCamera->SetNearPlane(c.nearZ);
        fbxCamera->SetFarPlane(c.farZ);
        if (c.projection == GfCamera::Projection::Orthographic) {
            // the vertical aperture was computed from the orthoZoom by dividing by the
            // aperture unit (ie. converting from cm to mm) so we need to reverse that.
            // Also, we need to divide by 30 to bring the zoom factor into fbx units.
            // See here:
            // https://forums.autodesk.com/t5/fbx-forum/how-do-i-get-the-quot-orthographic-width-quot-for-a-camera/td-p/4227903
            // for some relevent background.
            float orthoZoom = c.verticalAperture * GfCamera::APERTURE_UNIT / 30.0f;
            fbxCamera->OrthoZoom.Set(orthoZoom);
        } else {
            fbxCamera->SetApertureWidth(c.horizontalAperture *
                                        mm2inch); // horizontal aperture in inches
            fbxCamera->SetApertureHeight(c.verticalAperture *
                                         mm2inch); // vertical aperture in inches
        }
    }
}

void
exportFbxLights(ExportFbxContext& ctx)
{
    ctx.lights.resize(ctx.usd->lights.size());
    for (size_t i = 0; i < ctx.usd->lights.size(); ++i) {
        const Light& light = ctx.usd->lights[i];

        // We use point lights as the default for unsupported light types
        FbxLight::EType lightType = FbxLight::EType::ePoint;

        std::string type = "point";

        float innerAngle = 0;
        float outerAngle = 0;
        switch (light.type) {
            case LightType::Disk:
                type = "spot (from USD disk light)";
                lightType = FbxLight::EType::eSpot;

                // FBX inner cone angle is from the center to where falloff begins, and outer cone
                // angle is from the center to where falloff ends. Meanwhile, in USD, angle is from
                // the center to the edge of the cone, and softness is a number from 0 to 1
                // indicating how close to the center the falloff begins.

                // USD's cone angle is the entire shape of the spot light, corresponding to FBX's
                // outer angle
                outerAngle = light.coneAngle;

                // Use the fraction of the cone containing the falloff to calculate the inner cone
                innerAngle = (1 - light.coneFalloff) * outerAngle;

                break;
            case LightType::Rectangle:
                TF_WARN("exportFbxLight: ignoring unsupported light of type \"rectangle\". "
                        "Defaulting to point light.\n");

                // type = "area rectangle (from USD rectangle light)";

                // fbxLight->LightType.Set(FbxLight::EType::eArea);
                // fbxLight->AreaLightShape.Set(FbxLight::EAreaLightShape::eRectangle);

                // TODO: Set rectangle shape from light.length vector

                break;
            case LightType::Sphere:
                type = "point (from USD sphere light)";
                lightType = FbxLight::EType::ePoint;

                // Eventually, we may want to export this as a sphere area light. For now, we will
                // export it as a point light, to be consistent with the FBX light import

                // type = "area sphere (from USD sphere light)";

                // fbxLight->LightType.Set(FbxLight::EType::eArea);
                // fbxLight->AreaLightShape.Set(FbxLight::EAreaLightShape::eSphere);

                // TODO: Set radius from light.radius

                break;
            case LightType::Environment:
                TF_WARN("exportFbxLight: encountered unsupported light of type \"environment\". "
                        "Defaulting to point light.\n");

                break;
            case LightType::Sun:
                type = "directional (from USD sun light)";
                lightType = FbxLight::EType::eDirectional;

                break;
            default:
                TF_WARN("exportFbxLight: encountered light of unknown type. Defaulting to point "
                        "light.\n");

                break;
        }

        FbxLight* fbxLight = FbxLight::Create(ctx.fbx->scene, getNodeName(light).c_str());

        fbxLight->LightType.Set(lightType);
        fbxLight->Color.Set(FbxDouble3(light.color[0], light.color[1], light.color[2]));
        fbxLight->Intensity.Set(light.intensity);

        if (lightType == FbxLight::EType::eSpot) {
            fbxLight->InnerAngle.Set(innerAngle);
            fbxLight->OuterAngle.Set(outerAngle);
        }

        ctx.lights[i] = fbxLight;

        TF_DEBUG_MSG(FILE_FORMAT_FBX,
                     "exportFbx: light[%d]{ %s } of type %s\n",
                     (int)i,
                     getNodeName(light).c_str(),
                     type.c_str());
    }
}

void
exportFbxProperty(const VtValue& value, FbxPropertyT<FbxDouble>& property)
{
    if (value.IsHolding<float>()) {
        property.Set(value.Get<float>());
    }
}
void
exportFbxProperty(const VtValue& value, FbxPropertyT<FbxDouble3>& property)
{
    if (value.IsHolding<GfVec3f>()) {
        GfVec3f v = value.Get<GfVec3f>();
        property.Set(FbxDouble3(v[0], v[1], v[2]));
    }
}

void
exportFbxPropertyAsSRGB(const VtValue& value, FbxPropertyT<FbxDouble>& property)
{
    // This version is needed because there must to be a match used by the templated
    // exportFbxInput() function. It should never be called but we issue a warning
    // if it is and call the default method.
    TF_WARN("Unexpected call to exportFbxPropertyAsSRGB with single double value\n");
    exportFbxProperty(value, property);
}

void
exportFbxPropertyAsSRGB(const VtValue& value, FbxPropertyT<FbxDouble3>& property)
{
    if (value.IsHolding<GfVec3f>()) {
        GfVec3f v = value.Get<GfVec3f>();
        property.Set(FbxDouble3(linearToSRGB(v[0]), linearToSRGB(v[1]), linearToSRGB(v[2])));
    }
}

FbxTexture::EWrapMode
getWrapMode(const TfToken& wrap)
{
    return (wrap == AdobeTokens->clamp) ? FbxTexture::EWrapMode::eClamp
                                        : FbxTexture::EWrapMode::eRepeat;
}

template<typename T>
bool
exportFbxInput(ExportFbxContext& ctx,
               const InputTranslator& inputTranslator,
               const Input& input,
               FbxPropertyT<T>& property,
               const FbxTexture::ETextureUse& textureUse,
               const TfToken& colorSpace = AdobeTokens->raw)
{
    if (input.image >= 0) {
        const ImageAsset& image = inputTranslator.getImage(input.image);

        FbxFileTexture* fbxTexture = FbxFileTexture::Create(ctx.fbx->scene, image.name.c_str());
        std::string path = ctx.exportParentPath + image.uri;
        fbxTexture->SetFileName(path.c_str()); // File is in current directory.
        fbxTexture->SetTextureUse(textureUse);
        fbxTexture->SetWrapMode(getWrapMode(input.wrapS), getWrapMode(input.wrapT));
        fbxTexture->SetMappingType(FbxTexture::eUV);
        fbxTexture->SetMaterialUse(FbxFileTexture::eModelMaterial);
        fbxTexture->SetSwapUV(false);
        fbxTexture->UVSet.Set(FbxString(getSTPrimvarAttrToken(input.uvIndex).GetText()));

        if (input.transformScale.IsHolding<GfVec2f>()) {
            GfVec2f scale = input.transformScale.UncheckedGet<GfVec2f>();
            fbxTexture->SetScale(scale[0], scale[1]);
        }
        if (input.transformRotation.IsHolding<float>()) {
            float rot = input.transformRotation.UncheckedGet<float>();
            fbxTexture->SetRotation(0.0, 0.0, rot);
        }
        if (input.transformTranslation.IsHolding<GfVec2f>()) {
            GfVec2f trans = input.transformTranslation.UncheckedGet<GfVec2f>();
            fbxTexture->SetTranslation(trans[0], trans[1]);
        }

        property.ConnectSrcObject(fbxTexture);
        return true;
    } else if (!input.value.IsEmpty()) {
        // using the sRGB colorspace should only be used for vec3 values
        if (colorSpace == AdobeTokens->sRGB) {
            exportFbxPropertyAsSRGB(input.value, property);
        } else {
            exportFbxProperty(input.value, property);
        }
        return true;
    }
    return false;
}

/** If metallic value is present do the following mapping
 * 1. Decrease the diffuse color based on how smooth and metallic the material is
 * 2. Specular color is also the diffuse color, weighted opposite the diffuse color
 * 3. Set shininess which is calculated from roughness
 *
 * "input" should be the metallic input
 *
 * Returns whether the material is at least somewhat metallic (if metallic > 0)
 */
bool
exportMetallicValueInput(ExportFbxContext& ctx,
                         const InputTranslator& inputTranslator,
                         const Input& input,
                         float roughness,
                         FbxSurfacePhong* phong)
{
    float metallic = 0.0;
    if (!input.value.IsEmpty() && input.value.IsHolding<float>()) {
        metallic = input.value.UncheckedGet<float>();
    }
    // The more metallic the surface is, the less diffuse should be present. But increasing
    // roughness decreases the metal look of metallic surfaces, so we modulate the metallic factor
    // based on roughness
    float diffuseFactor = (1.0 - (metallic * (1.0 - roughness)));
    if (metallic > 0) {
        // If the material is more metallic (and less diffuse), it will be shinier with specular
        // components
        float specularFactor = 1.0 - diffuseFactor;

        FbxFileTexture* diffuseTexture =
          FbxCast<FbxFileTexture>(phong->Diffuse.GetSrcObject<FbxFileTexture>());
        if (diffuseTexture) {
            phong->Specular.ConnectSrcObject(diffuseTexture);
            phong->Diffuse.ConnectSrcObject(diffuseTexture);
        } else {
            FbxDouble3 oldBaseColor = phong->Diffuse.Get();
            phong->Specular.Set(oldBaseColor);
        }
        float shininess = (1.0f - roughness) * MAX_FBX_SHININESS;
        if (shininess > 0.0f) {
            phong->Shininess.Set(shininess);
        }
        phong->DiffuseFactor.Set(diffuseFactor);
        phong->SpecularFactor.Set(specularFactor);
    }
    exportFbxInput(ctx, inputTranslator, input, phong->ReflectionFactor, FbxTexture::eStandard);
    return metallic > 0;
}

void
exportFbxMaterials(ExportFbxContext& ctx)
{
    InputTranslator inputTranslator(true, ctx.usd->images, DEBUG_TAG);
    ctx.materials.resize(ctx.usd->materials.size());
    for (size_t i = 0; i < ctx.usd->materials.size(); i++) {
        const Material& m = ctx.usd->materials[i];
        FbxSurfacePhong* phong = FbxSurfacePhong::Create(ctx.fbx->scene, getNodeName(m).c_str());
        ctx.materials[i] = phong;

        Input diffuseColor;
        Input transparency;
        Input normal;
        Input emissiveColor;
        Input occlusion;
        Input metallic;
        Input roughness;

        inputTranslator.translateDirect(m.diffuseColor, diffuseColor);
        inputTranslator.translateOpacity2Transparency(m.opacity, transparency);
        inputTranslator.translateDirect(m.normal, normal);
        inputTranslator.translateDirect(m.emissiveColor, emissiveColor);
        // Convert Input data for occlusion, metallic and roughness to single channel textures
        // (if necessary). This is done so that there is consistency on which channel to
        // reference when importing.
        inputTranslator.translateToSingle("occlusion", m.occlusion, occlusion);
        inputTranslator.translateToSingle("metallic", m.metallic, metallic);
        inputTranslator.translateToSingle("roughness", m.roughness, roughness);
        exportFbxInput(ctx,
                       inputTranslator,
                       diffuseColor,
                       phong->Diffuse,
                       FbxTexture::eStandard,
                       AdobeTokens->sRGB);
        exportFbxInput(ctx,
                       inputTranslator,
                       emissiveColor,
                       phong->Emissive,
                       FbxTexture::eStandard,
                       AdobeTokens->sRGB);
        exportFbxInput(ctx, inputTranslator, normal, phong->NormalMap, FbxTexture::eBumpNormalMap);
        exportFbxInput(
          ctx, inputTranslator, occlusion, phong->AmbientFactor, FbxTexture::eStandard);
        exportFbxInput(
          ctx, inputTranslator, transparency, phong->TransparencyFactor, FbxTexture::eStandard);

        // Detemine if the material has a metallic value and if so approximate its properties
        float roughnessValue = 0.0f;
        if (!roughness.value.IsEmpty() && roughness.value.IsHolding<float>()) {
            roughnessValue = roughness.value.UncheckedGet<float>();
        }
        if (!exportMetallicValueInput(ctx, inputTranslator, metallic, roughnessValue, phong)) {
            exportFbxInput(
              ctx, inputTranslator, roughness, phong->SpecularFactor, FbxTexture::eStandard);
        }
        if (transparency.image >= 0 || !transparency.value.IsEmpty()) {
            phong->TransparentColor.Set(FbxDouble3(1));
        }
    }
    ctx.fbx->images = inputTranslator.getImages();
}

bool
exportSkeletons(ExportFbxContext& ctx)
{
    ctx.skeletons.resize(ctx.usd->skeletons.size());
    for (size_t i = 0; i < ctx.usd->skeletons.size(); i++) {
        Skeleton& skeleton = ctx.usd->skeletons[i];

        // Add skeleton joints as fbx nodes with an attribute FbxSkeleton.
        // Also associate an fbx cluster to each of those fbx nodes.
        // Clusters will serve to link the nodes to the meshes control points.
        std::unordered_map<std::string, FbxNode*> skeletonNodesMap;

        {
            size_t jointCount = skeleton.joints.size();
            std::vector<FbxNode*> fbxNodes(jointCount);
            for (size_t j = 0; j < jointCount; j++) {
                std::string joint = skeleton.joints[j].GetString();
                FbxNode* fbxNode = FbxNode::Create(ctx.fbx->scene, joint.c_str());
                skeletonNodesMap[joint] = fbxNode;
                fbxNodes[j] = fbxNode;

                GfMatrix4d restTransform = skeleton.restTransforms[j];
                FbxAMatrix fbxMatrix = GetFBXMatrixFromUSD(restTransform);
                fbxNode->LclRotation = fbxMatrix.GetR();
                fbxNode->LclTranslation = fbxMatrix.GetT();
                fbxNode->LclScaling = fbxMatrix.GetS();
            }

            // Now that all joints are created, set up skeleton types and parenting relationships.
            // We use a non-skeleton node to act as a parent for all root bones
            FbxNode* skeletonParentNode =
              FbxNode::Create(ctx.fbx->scene, getNodeName(skeleton).c_str());
            ctx.skeletons[i] = skeletonParentNode;

            for (size_t j = 0; j < jointCount; j++) {
                std::string joint = skeleton.joints[j].GetString();
                FbxNode* fbxNode = fbxNodes[j];

                FbxSkeleton* fbxSkeleton =
                  FbxSkeleton::Create(ctx.fbx->scene, getNodeName(skeleton).c_str());
                fbxNode->AddNodeAttribute(fbxSkeleton);
                int parent = skeleton.jointParents[j];
                if (parent < 0) {
                    fbxSkeleton->SetSkeletonType(fbxsdk::FbxSkeleton::eRoot);
                    skeletonParentNode->AddChild(fbxNode);
                } else {
                    fbxSkeleton->SetSkeletonType(fbxsdk::FbxSkeleton::eLimbNode);
                    std::string parentJoint = skeleton.joints[parent].GetString();
                    FbxNode* parentNode = skeletonNodesMap[parentJoint];
                    parentNode->AddChild(fbxNode);
                    TF_DEBUG_MSG(FILE_FORMAT_FBX, "Adding node parent %s\n", parentJoint.c_str());
                }
            }

            // All meshes were created previously,
            // so just add skin info to the ones pointed to by skeleton::targets.
            // Also, link nodes to the meshes control points via the fbx clusters.
            for (size_t j = 0; j < skeleton.meshSkinningTargets.size(); j++) {
                int meshTargetIndex = skeleton.meshSkinningTargets[j];
                if (meshTargetIndex < 0 || meshTargetIndex >= ctx.usd->meshes.size() ||
                    meshTargetIndex >= ctx.meshes.size()) {
                    TF_RUNTIME_ERROR(
                      FILE_FORMAT_FBX, "Invalid target index: %d\n", meshTargetIndex);
                    continue;
                }
                Mesh& mesh = ctx.usd->meshes[meshTargetIndex];
                FbxMesh* fbxMesh = ctx.meshes[meshTargetIndex];
                if (fbxMesh == nullptr) {
                    TF_WARN("Invalid mesh: %d\n", meshTargetIndex);
                    continue;
                }
                FbxSkin* fbxSkin = FbxSkin::Create(ctx.fbx->scene, "");
                if (fbxSkin == nullptr) {
                    TF_WARN("Invalid skin: %d\n", meshTargetIndex);
                    continue;
                }
                // fbxMesh->AddDeformer(fbxSkin);
                fbxSkin->SetGeometry(fbxMesh);

                std::vector<FbxCluster*> clusters(jointCount);
                for (size_t k = 0; k < jointCount; k++) {
                    FbxAMatrix fbxGeomBindTransform = GetFBXMatrixFromUSD(mesh.geomBindTransform);
                    FbxAMatrix fbxLinkTransform = GetFBXMatrixFromUSD(skeleton.bindTransforms[k]);
                    FbxCluster* cluster = FbxCluster::Create(ctx.fbx->scene, "");
                    cluster->SetUserData("JointIndex", std::to_string(k).c_str());
                    cluster->SetTransformMatrix(fbxGeomBindTransform);
                    cluster->SetTransformLinkMatrix(fbxLinkTransform);
                    cluster->SetLinkMode(fbxsdk::FbxCluster::ELinkMode::eNormalize);
                    cluster->SetLink(fbxNodes[k]);
                    fbxSkin->AddCluster(cluster);
                    clusters[k] = cluster;
                }

                for (size_t k = 0; k < mesh.weights.size(); k++) {
                    size_t currentVertex = k / mesh.influenceCount;
                    float weight = mesh.weights[k];
                    int joint = mesh.joints[k];
                    if (joint < 0 || joint >= clusters.size()) {
                        TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Invalid joint index: %d\n", joint);
                        continue;
                    }
                    FbxCluster* cluster = clusters[joint];
                    cluster->AddControlPointIndex(currentVertex, weight);
                }
            }
        }

        size_t animatedJointCount = skeleton.animatedJoints.size();
        std::vector<FbxNode*> animatedFbxNodes(animatedJointCount);
        for (size_t j = 0; j < animatedJointCount; j++) {
            animatedFbxNodes[j] = skeletonNodesMap[skeleton.animatedJoints[j].GetString()];
        }

        int animationTrackIndex = 0;
        for (SkeletonAnimation& skeletonAnimation : skeleton.skeletonAnimations) {
            ExportFbxAnimStackData& exportAnimStackData = ctx.animStackData[animationTrackIndex];
            FbxAnimLayer* animLayer = exportAnimStackData.animLayer;

            for (size_t k = 0; k < animatedJointCount; k++) {
                FbxNode* fbxNode = animatedFbxNodes[k];
                FbxAnimCurveNode* tNode = fbxNode->LclTranslation.GetCurveNode(animLayer, true);
                FbxAnimCurveNode* rNode = fbxNode->LclRotation.GetCurveNode(animLayer, true);
                FbxAnimCurveNode* sNode = fbxNode->LclScaling.GetCurveNode(animLayer, true);
                tNode->CreateCurve(tNode->GetName(), 0U)->KeyModifyBegin();
                tNode->CreateCurve(tNode->GetName(), 1U)->KeyModifyBegin();
                tNode->CreateCurve(tNode->GetName(), 2U)->KeyModifyBegin();
                rNode->CreateCurve(rNode->GetName(), 0U)->KeyModifyBegin();
                rNode->CreateCurve(rNode->GetName(), 1U)->KeyModifyBegin();
                rNode->CreateCurve(rNode->GetName(), 2U)->KeyModifyBegin();
                sNode->CreateCurve(sNode->GetName(), 0U)->KeyModifyBegin();
                sNode->CreateCurve(sNode->GetName(), 1U)->KeyModifyBegin();
                sNode->CreateCurve(sNode->GetName(), 2U)->KeyModifyBegin();
            }

            // We need to convert from timeCodesPerSecond to seconds so be compute the
            // multiplier.
            double secondsPerTimeCode =
              ctx.usd->timeCodesPerSecond != 0.0 ? 1.0 / ctx.usd->timeCodesPerSecond : 1.0;
            for (size_t t = 0; t < skeletonAnimation.times.size(); t++) {
                float time = skeletonAnimation.times[t];
                FbxTime fbxTime;
                fbxTime.SetSecondDouble(time * secondsPerTimeCode);
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "export skeleton[%lu] animation[%lu][t = %f]: %lu joints\n",
                             i,
                             animationTrackIndex,
                             time,
                             animatedJointCount);
                for (size_t k = 0; k < animatedJointCount; k++) {
                    FbxNode* fbxNode = animatedFbxNodes[k];
                    FbxAnimCurveNode* tNode =
                      fbxNode->LclTranslation.GetCurveNode(animLayer, false);
                    FbxAnimCurveNode* rNode = fbxNode->LclRotation.GetCurveNode(animLayer, false);
                    FbxAnimCurveNode* sNode = fbxNode->LclScaling.GetCurveNode(animLayer, false);
                    FbxQuaternion q = GetFBXQuat(skeletonAnimation.rotations[t][k]);
                    FbxVector4 euler;
                    euler.SetXYZ(q);
                    FbxAnimCurve* t0 = tNode->GetCurve(0);
                    FbxAnimCurve* t1 = tNode->GetCurve(1);
                    FbxAnimCurve* t2 = tNode->GetCurve(2);
                    FbxAnimCurve* r0 = rNode->GetCurve(0);
                    FbxAnimCurve* r1 = rNode->GetCurve(1);
                    FbxAnimCurve* r2 = rNode->GetCurve(2);
                    FbxAnimCurve* s0 = sNode->GetCurve(0);
                    FbxAnimCurve* s1 = sNode->GetCurve(1);
                    FbxAnimCurve* s2 = sNode->GetCurve(2);
                    int tIndex0 = t0->KeyAdd(fbxTime);
                    int tIndex1 = t1->KeyAdd(fbxTime);
                    int tIndex2 = t2->KeyAdd(fbxTime);
                    int rIndex0 = r0->KeyAdd(fbxTime);
                    int rIndex1 = r1->KeyAdd(fbxTime);
                    int rIndex2 = r2->KeyAdd(fbxTime);
                    int sIndex0 = s0->KeyAdd(fbxTime);
                    int sIndex1 = s1->KeyAdd(fbxTime);
                    int sIndex2 = s2->KeyAdd(fbxTime);
                    t0->KeySet(tIndex0,
                               fbxTime,
                               skeletonAnimation.translations[t][k][0],
                               FbxAnimCurveDef::eInterpolationConstant);
                    t1->KeySet(tIndex1,
                               fbxTime,
                               skeletonAnimation.translations[t][k][1],
                               FbxAnimCurveDef::eInterpolationConstant);
                    t2->KeySet(tIndex2,
                               fbxTime,
                               skeletonAnimation.translations[t][k][2],
                               FbxAnimCurveDef::eInterpolationConstant);
                    r0->KeySet(rIndex0, fbxTime, euler[0], FbxAnimCurveDef::eInterpolationConstant);
                    r1->KeySet(rIndex1, fbxTime, euler[1], FbxAnimCurveDef::eInterpolationConstant);
                    r2->KeySet(rIndex2, fbxTime, euler[2], FbxAnimCurveDef::eInterpolationConstant);
                    s0->KeySet(sIndex0,
                               fbxTime,
                               skeletonAnimation.scales[t][k][0],
                               FbxAnimCurveDef::eInterpolationConstant);
                    s1->KeySet(sIndex1,
                               fbxTime,
                               skeletonAnimation.scales[t][k][1],
                               FbxAnimCurveDef::eInterpolationConstant);
                    s2->KeySet(sIndex2,
                               fbxTime,
                               skeletonAnimation.scales[t][k][2],
                               FbxAnimCurveDef::eInterpolationConstant);
                }
            }

            for (size_t k = 0; k < animatedJointCount; k++) {
                FbxNode* fbxNode = animatedFbxNodes[k];
                FbxAnimCurveNode* tNode = fbxNode->LclTranslation.GetCurveNode(animLayer, false);
                FbxAnimCurveNode* rNode = fbxNode->LclRotation.GetCurveNode(animLayer, false);
                FbxAnimCurveNode* sNode = fbxNode->LclScaling.GetCurveNode(animLayer, false);
                tNode->GetCurve(0U)->KeyModifyEnd();
                tNode->GetCurve(1U)->KeyModifyEnd();
                tNode->GetCurve(2U)->KeyModifyEnd();
                rNode->GetCurve(0U)->KeyModifyEnd();
                rNode->GetCurve(1U)->KeyModifyEnd();
                rNode->GetCurve(2U)->KeyModifyEnd();
                sNode->GetCurve(0U)->KeyModifyEnd();
                sNode->GetCurve(1U)->KeyModifyEnd();
                sNode->GetCurve(2U)->KeyModifyEnd();
            }

            animationTrackIndex++;
        }
    }
    return true;
}

bool
exportFbxNodes(ExportFbxContext& ctx)
{
    std::function<void(const Node& node, FbxNode* parent)> exportFbxNode;
    exportFbxNode = [&](const Node& node, FbxNode* parent) -> bool {
        FbxNode* fbxNode = FbxNode::Create(ctx.fbx->scene, getNodeName(node).c_str());
        if (fbxNode != nullptr) {

            //     context.AddNodePath(primPath, node);
            // if (prim.IsA<UsdGeomXformable>())
            //     ExportFBXNodeTransform(prim, node, timeCode, yUp);
            // if (prim.IsA<UsdGeomCamera>())
            //     ExportFBXCameraAttribute(prim, node, timeCode);
            // if (prim.IsA<UsdGeomMesh>())
            //     ExportFBXMeshAttribute(prim, node, context, timeCode);
            // if (prim.IsA<UsdGeomNurbsPatch>() || prim.IsA<UsdGeomBasisCurves>())
            //     AddFBXCurveAttribute(prim, node, timeCode);
            // if (prim.IsA<UsdGeomPointInstancer>())
            //     context.FlagInstanceNode(primPath);

            parent->AddChild(fbxNode);
            exportFbxTransform(ctx, node, fbxNode);

            if (node.markedInvisible) {
                fbxNode->SetVisibility(false);
            }
            if (node.camera >= 0) {
                // Ignore camera invisibility, since it isn't important enough to add a new node
                FbxCamera* fbxCamera = ctx.cameras[node.camera];
                fbxNode->AddNodeAttribute(fbxCamera);
            }
            if (node.light >= 0) {
                FbxLight* fbxLight = ctx.lights[node.light];
                FbxNode* container = fbxNode;
                if (ctx.usd->lights[node.light].markedInvisible) {
                    container = FbxNode::Create(ctx.fbx->scene, "light_visibility");
                    container->SetVisibility(false);
                    fbxNode->AddChild(container);
                }
                container->AddNodeAttribute(fbxLight);
            }

            for (const auto& [skeletonIndex, meshIndices] : node.skinnedMeshes) {
                fbxNode->AddChild(ctx.skeletons[skeletonIndex]);

                const Skeleton& skeleton = ctx.usd->skeletons[skeletonIndex];
                for (int skinningTargetIdx : skeleton.meshSkinningTargets) {
                    const Mesh& mesh = ctx.usd->meshes[skinningTargetIdx];
                    FbxNode* fbxMeshNode =
                      FbxNode::Create(ctx.fbx->scene, getNodeName(mesh).c_str());
                    if (fbxMeshNode != nullptr) {
                        fbxNode->AddChild(fbxMeshNode);

                        FbxMesh* fbxMesh = ctx.meshes[skinningTargetIdx];
                        if (fbxMesh != nullptr) {
                            fbxMeshNode->AddNodeAttribute(fbxMesh);
                            bindMaterial(ctx, mesh, fbxMesh);
                        } else {
                            TF_WARN("Invalid mesh: %d", skinningTargetIdx);
                        }
                    }
                }
            }
            for (size_t i = 0; i < node.staticMeshes.size(); i++) {
                int meshIndex = node.staticMeshes[i];
                if (meshIndex < 0 || meshIndex >= ctx.usd->meshes.size() ||
                    meshIndex >= ctx.meshes.size()) {
                    TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Invalid mesh index: %d\n", meshIndex);
                    continue;
                }
                const Mesh& m = ctx.usd->meshes[meshIndex];
                FbxNode* container = fbxNode;
                if (node.staticMeshes.size() > 1 || m.markedInvisible) {
                    // Name the node based on the child index, unless there is only one child, in
                    // which case the node is only present to preserve visibility
                    std::string containerName =
                      node.staticMeshes.size() > 1
                        ? getNodeName(node).c_str() + std::to_string(i)
                        : getNodeName(node).c_str() + std::string("_visibility");
                    container = FbxNode::Create(ctx.fbx->scene, containerName.c_str());

                    if (m.markedInvisible) {
                        container->SetVisibility(false);
                    }
                    fbxNode->AddChild(container);
                }
                FbxMesh* fbxMesh = ctx.meshes[meshIndex];
                if (fbxMesh != nullptr) {
                    container->AddNodeAttribute(fbxMesh);
                    bindMaterial(ctx, m, fbxMesh);
                } else {
                    TF_WARN("Invalid mesh: %d", meshIndex);
                }
            }

            for (size_t i = 0; i < node.children.size(); i++) {
                exportFbxNode(ctx.usd->nodes[node.children[i]], fbxNode);
            }
        }
        return true;
    };
    FbxNode* rootNode = ctx.fbx->scene->GetRootNode();
    for (size_t i = 0; i < ctx.usd->rootNodes.size(); i++) {
        exportFbxNode(ctx.usd->nodes[ctx.usd->rootNodes[i]], rootNode);
    }

    return true;
}

bool
exportFbx(const ExportFbxOptions& options, UsdData& usd, Fbx& fbx)
{
    ExportFbxContext ctx;
    ctx.usd = &usd;
    ctx.fbx = &fbx;
    ctx.exportParentPath = options.exportParentPath;
    ctx.convertColorSpaceToSRGB = shouldConvertToSRGB(usd, options.outputColorSpace);
    exportFbxAnimationTracks(ctx);
    exportFbxSettings(ctx);
    exportFbxMaterials(ctx);
    exportFbxCameras(ctx);
    exportFbxLights(ctx);
    exportFbxMeshes(ctx);
    exportSkeletons(ctx);
    exportFbxNodes(ctx);
    // AddNodesReference(mStage, context, startTime);
    // BindSceneMaterialsToGeometry(mStage, lScene, context, startTime);

    return true;
}

}
