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
#include <common.h>
#include <fbxsdk.h>
#include <images.h>
#include <materials.h>
#include <usdData.h>

using namespace PXR_NS;
namespace adobe::usd {

struct ExportFbxContext
{
    UsdData* usd;
    Fbx* fbx;
    std::vector<FbxSurfaceMaterial*> materials;
    std::vector<FbxMesh*> meshes;
    std::vector<FbxCamera*> cameras;
    std::vector<FbxNode*> skeletons;
    bool hasYUp;
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

bool
exportFbxTransform(ExportFbxContext& ctx, const Node& node, FbxNode* fbxNode)
{
    if (node.hasTransform) {
        FbxAMatrix localTransform = GetFBXMatrixFromUSD(node.transform);
        FbxAMatrix AdditionalRotation{};

        // If the node contains a camera, we need to apply the reverse Y axis rotation
        // to orient the camera to look down the -X axis (see importFbxCamera) which is
        // the default for fbx
        // Note that if/when light import is supported, then a similar rotation may be needed
        if (node.camera != -1) {
            FbxVector4 rotation = FbxVector4(0.0f, 90.f, 0.0f);
            AdditionalRotation.SetR(rotation);
        }

        FbxAMatrix finalTransform = localTransform * AdditionalRotation;
        FbxVector4 rotation = finalTransform.GetR();
        FbxVector4 translation = finalTransform.GetT();
        FbxVector4 scale = finalTransform.GetS();
        fbxNode->LclRotation.Set(rotation);
        fbxNode->LclTranslation.Set(translation);
        fbxNode->LclScaling.Set(scale);
    }
    return true;
}

bool
exportFbxMeshes(ExportFbxContext& ctx)
{
    ctx.meshes.resize(ctx.usd->meshes.size());
    for (size_t i = 0; i < ctx.usd->meshes.size(); i++) {
        const Mesh& m = ctx.usd->meshes[i];
        FbxMesh* fbxMesh = FbxMesh::Create(ctx.fbx->scene, m.name.c_str());
        ctx.meshes[i] = fbxMesh;

        // Positions
        size_t k = 0;
        for (size_t i = 0; i < m.faces.size(); i++) {
            fbxMesh->BeginPolygon();
            for (int j = 0; j < m.faces[i]; j++) {
                fbxMesh->AddPolygon(m.indices[k++]);
            }
            fbxMesh->EndPolygon();
        }
        fbxMesh->InitControlPoints(m.points.size());
        for (size_t i = 0; i < m.points.size(); i++) {
            GfVec3f p = m.points[i];
            fbxMesh->SetControlPointAt(FbxVector4(p[0], p[1], p[2]), i);
        }

        // Normals
        if (m.normals.values.size()) {
            FbxGeometryElement::EMappingMode normalMapping;
            if (!exportFbxMapping(m.normals.interpolation, normalMapping)) {
                TF_WARN("Normals interpolation: %s not supported, defaulting to byControlPoint\n",
                        m.normals.interpolation.GetText());
            }

            FbxGeometryElementNormal* elementNormal = fbxMesh->CreateElementNormal();
            elementNormal->SetMappingMode(normalMapping);
            for (size_t i = 0; i < m.normals.values.size(); i++) {
                GfVec3f n = m.normals.values[i];
                FbxVector4 normal = FbxVector4(n[0], n[1], n[2]);
                elementNormal->GetDirectArray().Add(normal);
            }
            if (m.normals.indices.size()) {
                elementNormal->SetReferenceMode(FbxGeometryElement::EReferenceMode::eIndexToDirect);
                for (size_t i = 0; i < m.normals.indices.size(); i++) {
                    elementNormal->GetIndexArray().Add(m.normals.indices[i]);
                }
            } else {
                elementNormal->SetReferenceMode(FbxGeometryElement::EReferenceMode::eDirect);
            }
        }

        // Uvs
        if (m.uvs.values.size()) {
            FbxGeometryElement::EMappingMode uvMapping;
            if (!exportFbxMapping(m.uvs.interpolation, uvMapping)) {
                TF_WARN("Uvs interpolation: %s not supported, defaulting to byControlPoint\n",
                        m.uvs.interpolation.GetText());
            }
            FbxGeometryElementUV* elementUvs = fbxMesh->CreateElementUV("st", FbxLayerElement::eUV);
            elementUvs->SetMappingMode(uvMapping);
            int dataSize = 0;

            for (size_t i = 0; i < m.uvs.values.size(); i++) {
                GfVec2f x = m.uvs.values[i];
                FbxVector2 uv = FbxVector2(x[0], x[1]);
                elementUvs->GetDirectArray().Add(uv);
            }
            if (m.uvs.indices.size()) {
                elementUvs->SetReferenceMode(FbxGeometryElement::EReferenceMode::eIndexToDirect);
                dataSize = m.uvs.indices.size();
                for (size_t i = 0; i < m.uvs.indices.size(); i++) {
                    elementUvs->GetIndexArray().Add(m.uvs.indices[i]);
                }
            } else {
                elementUvs->SetReferenceMode(FbxGeometryElement::EReferenceMode::eDirect);
                dataSize = m.uvs.values.size();
            }
            // TODO: do this check in usdutils instead
            int expectedDataSize = 0;
            if (m.uvs.interpolation == UsdGeomTokens->faceVarying) {
                expectedDataSize = fbxMesh->GetPolygonVertexCount();
            } else if (m.uvs.interpolation == UsdGeomTokens->uniform) {
                expectedDataSize = fbxMesh->GetPolygonCount();
            } else if (m.uvs.interpolation == UsdGeomTokens->vertex) {
                expectedDataSize = fbxMesh->GetControlPointsCount();
            } else if (m.uvs.interpolation == UsdGeomTokens->constant) {
                expectedDataSize = 1;
            }
            if (expectedDataSize != dataSize) {
                TF_WARN("Incorrect uvs length. Excepted: %d, Actual: %d, interp: %s\n",
                        expectedDataSize,
                        dataSize,
                        m.uvs.interpolation.GetText());
            }
        }

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

            // TODO: Maybe it's necessary to adjust data if indices differ
            FbxGeometryElementVertexColor* vertexColor = fbxMesh->CreateElementVertexColor();
            vertexColor->SetMappingMode(colorMapping);
            if (indices.size()) {
                vertexColor->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
                vertexColor->GetIndexArray().SetCount(indices.size());
                for (size_t i = 0; i < indices.size(); i++) {
                    vertexColor->GetIndexArray().SetAt(i, indices[i]);
                }
            } else {
                vertexColor->SetReferenceMode(FbxGeometryElement::eDirect);
            }
            vertexColor->GetDirectArray().SetCount(colorValues.size());
            for (size_t i = 0; i < colorValues.size(); i++) {
                GfVec3f c = colorValues[i];
                FbxDouble4 vector = FbxDouble4(c[0], c[1], c[2], opacityValues[i]);
                vertexColor->GetDirectArray().SetAt(i, vector);
            }
        }

        // Crease
        // TfToken interpolateBoundary;
        // if (meshSchema.GetInterpolateBoundaryAttr().Get(&interpolateBoundary, timeCode))
        // {
        //     if (UsdGeomTokens->edgeOnly == interpolateBoundary)
        //         fbxMesh->SetBoundaryRule(FbxMesh::EBoundaryRule::eCreaseEdge);
        //     else if (UsdGeomTokens->edgeAndCorner == interpolateBoundary)
        //         fbxMesh->SetBoundaryRule(FbxMesh::EBoundaryRule::eCreaseAll);
        // }

        // VtArray<int> creaseIndices;
        // VtArray<float> creaseSharpnesses;
        // VtArray<int> creaseLengths;

        // if (meshSchema.GetCreaseIndicesAttr().Get(&creaseIndices, timeCode) &&
        //     meshSchema.GetCreaseSharpnessesAttr().Get(&creaseSharpnesses, timeCode) &&
        //     meshSchema.GetCreaseLengthsAttr().Get(&creaseLengths, timeCode))
        // {
        //     bool onValuePerEdge = creaseSharpnesses.size() == creaseLengths.size();
        //     size_t loopIndex = 0;

        //     if (onValuePerEdge)
        //     {
        //         for (size_t i = 0; i < creaseLengths.size(); i++)
        //         {
        //             int edgetVertCount = creaseLengths[i];
        //             float sharpness = creaseSharpnesses[i];

        //             for (int j = 0; j < edgetVertCount; j++)
        //             {
        //                 int creaseIndex = creaseIndices[loopIndex];
        //                 fbxMesh->SetVertexCreaseInfo(creaseIndex, sharpness);
        //                 loopIndex++;
        //             }
        //         }
        //     }
        //     else
        //     {
        //         for (size_t i = 0; i < creaseLengths.size(); i++)
        //         {
        //             int edgetVertCount = creaseLengths[i];

        //             for (int j = 0; j < edgetVertCount; j++)
        //             {
        //                 int creaseIndex = creaseIndices[loopIndex];
        //                 float sharpness = creaseSharpnesses[loopIndex];
        //                 fbxMesh->SetVertexCreaseInfo(creaseIndex, sharpness);
        //                 loopIndex++;
        //             }
        //         }
        //     }
        // }

        // Smoothing
        // TODO: Add smoothing information to FBX.
        // VtArray<int> cornderSharpnessIndexes;
        // VtArray<float> cornerSharpnes;

        // if (!geomMesh.GetCornerIndicesAttr().Get(&cornderSharpnessIndexes, timeCode) ||
        //     !geomMesh.GetCornerSharpnessesAttr().Get(&cornerSharpnes, timeCode))
        //     return;

        // auto pSmoothingGroup = fbxMesh->CreateElementSmoothing();
        // pSmoothingGroup->SetMappingMode(FbxLayerElement::EMappingMode::eByControlPoint);
        // pSmoothingGroup->SetReferenceMode(FbxLayerElement::EReferenceMode::eIndexToDirect);
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
        FbxFileTexture* fbxTexture = FbxFileTexture::Create(ctx.fbx->scene, image.uri.c_str());
        fbxTexture->SetFileName(image.uri.c_str()); // File is in current directory.
        fbxTexture->SetTextureUse(textureUse);
        fbxTexture->SetWrapMode(getWrapMode(input.wrapS), getWrapMode(input.wrapT));
        fbxTexture->SetMappingType(FbxTexture::eUV);
        fbxTexture->SetMaterialUse(FbxFileTexture::eModelMaterial);
        fbxTexture->SetSwapUV(false);

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

void
exportFbxMaterials(ExportFbxContext& ctx)
{
    InputTranslator inputTranslator(true, ctx.usd->images, DEBUG_TAG);
    ctx.materials.resize(ctx.usd->materials.size());
    for (size_t i = 0; i < ctx.usd->materials.size(); i++) {
        const Material& m = ctx.usd->materials[i];
        FbxSurfacePhong* phong = FbxSurfacePhong::Create(ctx.fbx->scene, "");
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
        // Convert Input data for occlusion, metallic and roughness to single channel textures (if
        // necessary). This is done so that there is consistency on which channel to reference when
        // importing.
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
        exportFbxInput(
          ctx, inputTranslator, metallic, phong->ReflectionFactor, FbxTexture::eStandard);
        exportFbxInput(
          ctx, inputTranslator, roughness, phong->SpecularFactor, FbxTexture::eStandard);
        if (transparency.image >= 0 || !transparency.value.IsEmpty()) {
            phong->TransparentColor.Set(FbxDouble3(1));
        }
        // phong->TransparencyFactor.Set(.5);
        // TODO specularity is not achieved correctly. Probably roughness conversion to shininess is
        // wrong. exportFbxInput(ctx, m.clearcoat,     phong->Reflection, FbxTexture::eStandard);
        // exportFbxInput(ctx, m.roughness,     phong->Shininess,          FbxTexture::eStandard);
        // exportFbxInput(ctx, m.displacement,  phong->DisplacementColor,  FbxTexture::eStandard);
        // if (m.useSpecularWorkflow.value)
        // exportFbxInput(ctx, m.specularColor, phong->Specular, FbxTexture::eStandard);
        // else
        // exportFbxInput(ctx, m.metallic, phong->Specular, FbxTexture::eStandard);
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
        size_t jointCount = skeleton.joints.size();
        std::vector<FbxNode*> fbxNodes(jointCount);
        std::unordered_map<std::string, FbxNode*> skeletonNodesMap;
        for (size_t j = 0; j < jointCount; j++) {
            std::string joint = skeleton.joints[j].GetString();
            FbxNode* fbxNode = FbxNode::Create(ctx.fbx->scene, joint.c_str());
            skeletonNodesMap[joint] = fbxNode;
            fbxNodes[j] = fbxNode;
            // auto nodePath = prim.GetName().GetString() + jointPath;
            // context.AddNodePath(PXR_NS::SdfPath(nodePath), currentNode);

            FbxSkeleton* fbxSkeleton = FbxSkeleton::Create(ctx.fbx->scene, "");
            fbxNode->AddNodeAttribute(fbxSkeleton);
            if (j == 0) {
                fbxSkeleton->SetSkeletonType(fbxsdk::FbxSkeleton::eRoot);
                ctx.skeletons[i] = fbxNode;
            } else {
                fbxSkeleton->SetSkeletonType(fbxsdk::FbxSkeleton::eLimbNode);
            }

            PXR_NS::GfMatrix4d restTransform = skeleton.restTransforms[j];
            FbxAMatrix fbxMatrix = GetFBXMatrixFromUSD(restTransform);
            fbxNode->LclRotation = fbxMatrix.GetR();
            fbxNode->LclTranslation = fbxMatrix.GetT();
            fbxNode->LclScaling = fbxMatrix.GetS();

            int parent = skeleton.parents[j];
            if (parent >= 0) {
                std::string parentJoint = skeleton.joints[parent].GetString();
                FbxNode* parentNode = skeletonNodesMap[parentJoint];
                parentNode->AddChild(fbxNode);
                TF_DEBUG_MSG(FILE_FORMAT_FBX, "Adding node parent %s\n", parentJoint.c_str());
            }
        }

        // All meshes were created previously,
        // so just add skin info to the ones pointed to by skeleton::targets.
        // Also, link nodes to the meshes control points via the fbx clusters.
        for (size_t j = 0; j < skeleton.targets.size(); j++) {
            Mesh& mesh = ctx.usd->meshes[skeleton.targets[j]];
            FbxMesh* fbxMesh = ctx.meshes[skeleton.targets[j]];
            FbxSkin* fbxSkin = FbxSkin::Create(ctx.fbx->scene, "");
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
                FbxCluster* cluster = clusters[joint];
                cluster->AddControlPointIndex(currentVertex, weight);
            }
        }

        for (size_t j = 0; j < skeleton.animations.size(); j++) {
            Animation& anim = ctx.usd->animations[j];
            FbxAnimStack* animStack = FbxAnimStack::Create(ctx.fbx->scene, anim.name.c_str());
            FbxAnimLayer* animLayer = fbxsdk::FbxAnimLayer::Create(ctx.fbx->scene, "Layer0");
            animStack->AddMember(animLayer);
            for (size_t k = 0; k < jointCount; k++) {
                FbxNode* fbxNode = fbxNodes[k];
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

            // We need to convert from timeCodesPerSecond to seconds so be compute the multiplier.
            double secondsPerTimeCode =
              ctx.usd->timeCodesPerSecond != 0.0 ? 1.0 / ctx.usd->timeCodesPerSecond : 1.0;
            for (size_t t = 0; t < anim.times.size(); t++) {
                float time = anim.times[t];
                FbxTime fbxTime;
                fbxTime.SetSecondDouble(time * secondsPerTimeCode);
                TF_DEBUG_MSG(FILE_FORMAT_FBX,
                             "export animation[%lu][t = %f]: %lu joints\n",
                             j,
                             time,
                             jointCount);
                for (size_t k = 0; k < jointCount; k++) {
                    FbxNode* fbxNode = fbxNodes[k];
                    FbxAnimCurveNode* tNode =
                      fbxNode->LclTranslation.GetCurveNode(animLayer, false);
                    FbxAnimCurveNode* rNode = fbxNode->LclRotation.GetCurveNode(animLayer, false);
                    FbxAnimCurveNode* sNode = fbxNode->LclScaling.GetCurveNode(animLayer, false);
                    FbxQuaternion q = GetFBXQuat(anim.rotations[t][k]);
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
                               anim.translations[t][k][0],
                               FbxAnimCurveDef::eInterpolationConstant);
                    t1->KeySet(tIndex1,
                               fbxTime,
                               anim.translations[t][k][1],
                               FbxAnimCurveDef::eInterpolationConstant);
                    t2->KeySet(tIndex2,
                               fbxTime,
                               anim.translations[t][k][2],
                               FbxAnimCurveDef::eInterpolationConstant);
                    r0->KeySet(rIndex0, fbxTime, euler[0], FbxAnimCurveDef::eInterpolationConstant);
                    r1->KeySet(rIndex1, fbxTime, euler[1], FbxAnimCurveDef::eInterpolationConstant);
                    r2->KeySet(rIndex2, fbxTime, euler[2], FbxAnimCurveDef::eInterpolationConstant);
                    s0->KeySet(sIndex0,
                               fbxTime,
                               anim.scales[t][k][0],
                               FbxAnimCurveDef::eInterpolationConstant);
                    s1->KeySet(sIndex1,
                               fbxTime,
                               anim.scales[t][k][1],
                               FbxAnimCurveDef::eInterpolationConstant);
                    s2->KeySet(sIndex2,
                               fbxTime,
                               anim.scales[t][k][2],
                               FbxAnimCurveDef::eInterpolationConstant);
                }
            }

            for (size_t k = 0; k < jointCount; k++) {
                FbxNode* fbxNode = fbxNodes[k];
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
        }
    }
    return true;
}

void
bindMaterial(ExportFbxContext& ctx, const Mesh& mesh, FbxMesh* fbxMesh)
{
    if (mesh.material >= 0) {
        FbxSurfaceMaterial* material = ctx.materials[mesh.material];
        FbxGeometryElementMaterial* elementMaterial = fbxMesh->CreateElementMaterial();
        elementMaterial->SetMappingMode(FbxGeometryElement::eAllSame);
        elementMaterial->SetReferenceMode(FbxGeometryElement::eDirect);
        FbxNode* n = fbxMesh->GetNode();
        n->AddMaterial(material);
    }
}

bool
exportFbxNodes(ExportFbxContext& ctx)
{
    std::function<void(const Node& node, FbxNode* parent)> exportFbxNode;
    exportFbxNode = [&](const Node& node, FbxNode* parent) -> bool {
        FbxNode* fbxNode = FbxNode::Create(ctx.fbx->scene, node.name.c_str());
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

            if (node.camera != -1) {
                FbxCamera* fbxCamera = ctx.cameras[node.camera];
                fbxNode->AddNodeAttribute(fbxCamera);
            }
            for (const auto& [skeletonIndex, meshIndices] : node.skinnedMeshes) {
                Skeleton& skeleton = ctx.usd->skeletons[skeletonIndex];
                fbxNode->AddChild(ctx.skeletons[skeletonIndex]);
                for (size_t i = 0; i < skeleton.targets.size(); i++) {
                    const Mesh& m = ctx.usd->meshes[skeleton.targets[i]];
                    FbxMesh* fbxMesh = ctx.meshes[skeleton.targets[i]];
                    fbxNode->AddNodeAttribute(fbxMesh);
                    bindMaterial(ctx, m, fbxMesh);
                }
            }
            for (size_t i = 0; i < node.staticMeshes.size(); i++) {
                int meshIndex = node.staticMeshes[i];
                const Mesh& m = ctx.usd->meshes[meshIndex];
                FbxNode* container = fbxNode;
                if (node.staticMeshes.size() > 1) {
                    std::string containerName = node.name.c_str() + std::to_string(i);
                    container = FbxNode::Create(ctx.fbx->scene, containerName.c_str());
                    fbxNode->AddChild(container);
                }
                FbxMesh* fbxMesh = ctx.meshes[meshIndex];
                container->AddNodeAttribute(fbxMesh);
                bindMaterial(ctx, m, fbxMesh);
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
    exportFbxSettings(ctx);
    exportFbxMaterials(ctx);
    exportFbxCameras(ctx);
    exportFbxMeshes(ctx);
    exportSkeletons(ctx);
    exportFbxNodes(ctx);
    // AddNodesReference(mStage, context, startTime);
    // BindSceneMaterialsToGeometry(mStage, lScene, context, startTime);

    return true;
}

}
