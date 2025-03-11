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
#include "fbx.h"
#include "debugCodes.h"
#include <algorithm>
#include <fileformatutils/common.h>
#include <fbxsdk.h>
#include <fstream>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/hermiteCurves.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/nurbsCurves.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <string>

using namespace PXR_NS;
using namespace fbxsdk;

namespace adobe::usd {

const char* upVectorName[] = { "none", "eXAxis", "eYAxis", "eZAxis" };
const char* coordSystemName[] = { "eRightHanded", "eLeftHanded" };
const char* attributeName[] = { "eUnknown",
                                "eNull",
                                "eMarker",
                                "eSkeleton",
                                "eMesh",
                                "eNurbs",
                                "ePatch",
                                "eCamera",
                                "eCameraStereo",
                                "eCameraSwitcher",
                                "eLight",
                                "eOpticalReference",
                                "eOpticalMarker",
                                "eNurbsCurve",
                                "eTrimNurbsSurface",
                                "eBoundary",
                                "eNurbsSurface",
                                "eShape",
                                "eLODGroup",
                                "eSubDiv",
                                "eCachedEffect",
                                "eLine" };

Fbx::Fbx()
{
    manager = FbxManager::Create();
    scene = FbxScene::Create(manager, "root");
}

Fbx::~Fbx()
{
    if (scene) {
        scene->Destroy();
    }
    if (readCallback) {
        readCallback->Destroy();
    }
    if (importer) {
        importer->Destroy();
    }
    if (manager) {
        manager->Destroy();
    }
}

// Debug print the FBX node hierarchy
void
printFbx(Fbx& fbx)
{
    int sign = 0;
    FbxGlobalSettings& globalSettings = fbx.scene->GetGlobalSettings();
    FbxSystemUnit systemUnit = globalSettings.GetSystemUnit();
    FbxAxisSystem axis = globalSettings.GetAxisSystem();
    FbxAxisSystem::ECoordSystem coordSystem = axis.GetCoorSystem();
    FbxAxisSystem::EUpVector upVector = axis.GetUpVector(sign);
    TF_DEBUG_MSG(
      FILE_FORMAT_FBX,
      "FBX Settings: units scale: %f, units multiplier: %f, axis: %s, sign: %d, coordSystem: %s\n",
      systemUnit.GetScaleFactor(),
      systemUnit.GetMultiplier(),
      upVectorName[upVector],
      sign,
      coordSystemName[coordSystem]);
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "FBX Hierarchy\n");
    int indentSize = 2;
    bool debugSkels = true; // Meshes are printed together with the eSkeletons names that skin them.
    std::function<void(FbxNode * node, int indent)> printNode;
    printNode = [&](FbxNode* node, int indent) {
        std::string msg = std::string(node->GetName()) + " { ";
        int nodeAttrCount = node->GetNodeAttributeCount();
        for (int i = 0; i < nodeAttrCount; i++) {
            FbxNodeAttribute* attribute = node->GetNodeAttributeByIndex(i);
            auto attrType = attribute->GetAttributeType();
            msg += std::string(attributeName[attrType]);
            if (debugSkels) {
                if (attrType == FbxNodeAttribute::eMesh) {
                    FbxMesh* fbxMesh = FbxCast<FbxMesh>(attribute);
                    if (fbxMesh != nullptr) {
                        int deformerCount = fbxMesh->GetDeformerCount(FbxDeformer::eSkin);
                        for (int j = 0; j < deformerCount; j++) {
                            FbxSkin* skin =
                              FbxCast<FbxSkin>(fbxMesh->GetDeformer(j, FbxDeformer::eSkin));
                            if (skin != nullptr) {
                                msg += " skin [";
                                for (int k = 0; k < skin->GetClusterCount(); k++) {
                                    FbxCluster* cluster = skin->GetCluster(k);
                                    if (cluster != nullptr) {
                                        FbxNode* link = cluster->GetLink();
                                        if (link != nullptr) {
                                            msg += " skel::" + std::string(link->GetName());
                                        } else {
                                            TF_WARN("Cluster link is nullptr");
                                        }
                                    } else {
                                        TF_WARN("Failed to retrieve cluster from skin");
                                    }
                                }
                                msg += "]";
                            } else {
                                TF_WARN("Failed to cast Deformer to FbxSkin");
                            }
                        }
                    } else {
                        TF_WARN("Failed to cast FbxNodeAttribute to FbxMesh");
                    }
                }
                if (i < nodeAttrCount - 1) {
                    msg += ", ";
                }
            }
            msg += " }";
            TF_DEBUG_MSG(FILE_FORMAT_FBX, "%*s%s\n", indent, "   ", msg.c_str());

            indent += indentSize;
            for (int j = 0; j < node->GetChildCount(); j++) {
                FbxNode* childNode = node->GetChild(j);
                if (childNode == nullptr) {
                    TF_WARN("Child node at index %d is null for node '%s'. Skipping.",
                            j,
                            node->GetName());
                    continue;
                }
                printNode(childNode, indent);
            }
        }
    };
    printNode(fbx.scene->GetRootNode(), indentSize);
}

// This function is registered as a callback for reading embedded data in an fbx file.
// It avoids having the embedded data be saved to disk in an fbm folder in order to be read.
//
FbxCallback::State
EmbedReadCBFunction(void* pUserData,
                    FbxClassId pDataHint,
                    const char* pFileName,
                    const void* pFileBuffer,
                    size_t pSizeInBytes)
{
    if (!pUserData || !pFileName || !pFileBuffer || pSizeInBytes < 1) {
        return FbxCallback::State::eNotHandled;
    }

    Fbx* fbx = reinterpret_cast<Fbx*>(pUserData);
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "EmbedReadCBFunction: %s\n", pFileName);

    auto const it = fbx->embeddedData.find(pFileName);
    if (it == fbx->embeddedData.cend()) {
        if (fbx->loadImages) {
            // copy the embedded data and add to map of filename to data
            std::vector<char> data(pSizeInBytes);
            memcpy(data.data(), pFileBuffer, pSizeInBytes);
            fbx->embeddedData[pFileName] = std::move(data);
        } else {
            // We don't need the image data yet so just add a map entry with an empty vector.
            // An entry indicates that there is embedded data and we don't need to load it
            // from a file.
            // This will get replaced when it comes time to load the images.
            fbx->embeddedData[pFileName] = std::vector<char>();
        }
        return FbxCallback::State::eHandled;
    }
    return FbxCallback::State::eNotHandled;
}

bool
readFbx(Fbx& fbx, const std::string& filename, bool importImages, bool onlyMaterials)
{
    GUARD(fbx.manager != nullptr, "Invalid fbx manager");

    FbxImporter* importer = FbxImporter::Create(fbx.manager, IOSROOT);
    GUARD(importer != nullptr, "Invalid fbx importer");

    FbxIOSettings* ios = FbxIOSettings::Create(fbx.manager, IOSROOT);
    if (!ios) {
        TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Failed to create FbxIOSettings");
        importer->Destroy();
        return false;
    }

    fbx.filename = filename;
    ios->SetBoolProp(IMP_FBX_MATERIAL, true);
    ios->SetBoolProp(IMP_FBX_TEXTURE, true);
    ios->SetBoolProp(IMP_FBX_ANIMATION, !onlyMaterials);
    ios->SetBoolProp(IMP_FBX_MODEL, !onlyMaterials);
    fbx.loadImages = importImages;

    if (!importer->Initialize(filename.c_str(), -1, ios)) {
        FbxString error = importer->GetStatus().GetErrorString();
        TF_RUNTIME_ERROR(FILE_FORMAT_FBX,
                         "Call to FbxImporter::Initialize() failed on opening file %s \n",
                         filename.c_str());
        TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Error returned: %s\n\n", error.Buffer());
        importer->Destroy();
        ios->Destroy();
        return false;
    }

    // let fbx own importer
    fbx.importer = importer;

    // Create the read callback to handle loading embedded data (ie images)
    FbxEmbeddedFileCallback* readCallback =
      FbxEmbeddedFileCallback::Create(fbx.manager, "EmbeddedFileReadCallback");

    if (!readCallback) {
        TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Failed to create FbxEmbeddedFileCallback");
        importer->Destroy();
        ios->Destroy();
        return false;
    }

    readCallback->RegisterReadFunction(EmbedReadCBFunction, (void*)&fbx);
    importer->SetEmbeddedFileReadCallback(readCallback);

    // let fbx own readCallback
    fbx.readCallback = readCallback;

    TF_DEBUG_MSG(FILE_FORMAT_FBX, "FBX importer opened file %s \n", filename.c_str());
    if (!importer->Import(fbx.scene)) {
        FbxString error = importer->GetStatus().GetErrorString();
        TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Call to FbxImporter::Import() failed.\n");
        TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Error returned: %s\n\n", error.Buffer());
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_FBX, "FBX read success \n");
    printFbx(fbx);
    return true;
}

std::string
extractFileName(const char* pFileName)
{
    std::string fileName(pFileName);
    std::replace(fileName.begin(),
                 fileName.end(),
                 '\\',
                 '/'); // Normalize slashes for cross-platform consistency
    size_t lastSlash = fileName.find_last_of('/');
    if (lastSlash != std::string::npos) {
        fileName = fileName.substr(lastSlash + 1);
    }
    return fileName;
}

bool
populateFileBufferAndSize(Fbx* fbx,
                          const char* pFileName,
                          const void**& pFileBuffer,
                          size_t*& pSizeInBytes)
{
    for (const ImageAsset& image : fbx->images) {
        if (image.uri == pFileName) {
            *pFileBuffer = image.image.data();
            *pSizeInBytes = image.image.size();
            if (pFileBuffer && *pSizeInBytes > 0) {
                return true;
            }
            break;
        }
    }
    return false;
}

FbxCallback::State
EmbedWriteCBFunction(void* pUserData,
                     FbxClassId pDataHint,
                     const char* pFileName,
                     const void** pFileBuffer,
                     size_t* pSizeInBytes)
{
    if (!pUserData || !pFileName) {
        return FbxCallback::State::eNotHandled;
    }

    Fbx* fbx = reinterpret_cast<Fbx*>(pUserData);
    TF_DEBUG_MSG(FILE_FORMAT_FBX, "EmbedWriteCBFunction: %s\n", pFileName);
    if (populateFileBufferAndSize(fbx, pFileName, pFileBuffer, pSizeInBytes)) {
        return FbxCallback::State::eHandled;
    } else {
        // Embedded pFilename's do not have the exportParentPath or a filepath, so we need to
        // extract the file name
        std::string fileName = extractFileName(pFileName);
        if (populateFileBufferAndSize(fbx, fileName.c_str(), pFileBuffer, pSizeInBytes)) {
            return FbxCallback::State::eHandled;
        }
    }
    return FbxCallback::State::eNotHandled;
}

bool
writeFbx(const ExportFbxOptions& options, const Fbx& fbx, const std::string& filename)
{
    GUARD(fbx.manager != nullptr, "Invalid fbx manager");
    const char* format = "FBX binary (*.fbx)"; // binary 7
    // const char* format = "FBX ascii (*.fbx)";      // ascii 7
    // const char* format = "FBX 6.0 binary (*.fbx)"; // binary 6
    // const char* format = "FBX 6.0 ascii (*.fbx)";  // ascii 6
    int fileFormat = fbx.manager->GetIOPluginRegistry()->FindWriterIDByDescription(format);
    FbxExporter* exporter = FbxExporter::Create(fbx.manager, "");
    FbxIOSettings* ios = FbxIOSettings::Create(fbx.manager, IOSROOT);

    GUARD(exporter != nullptr, "Invalid fbx exporter");
    if (!ios) {
        TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Failed to create FbxIOSettings");
        exporter->Destroy();
        return false;
    }
    ios->SetBoolProp(EXP_FBX_MATERIAL, true);
    ios->SetBoolProp(EXP_FBX_TEXTURE, true);
    ios->SetBoolProp(EXP_FBX_ANIMATION, true);
    if (options.embedImages) {
        ios->SetBoolProp(EXP_FBX_EMBEDDED, true);
    }
    fbx.manager->SetIOSettings(ios);

    const std::string parentPath = TfGetPathName(filename);
    TfMakeDirs(parentPath, -1, true);
    if (!options.embedImages) {
        for (const ImageAsset& image : fbx.images) {
            const std::string imageFilename = parentPath + image.uri;
            std::ofstream file(imageFilename, std::ios::out | std::ios::binary);
            if (!file.is_open()) {
                TF_DEBUG_MSG(FILE_FORMAT_FBX, "Error writing image %s\n", imageFilename.c_str());
                continue;
            }
            file.write(reinterpret_cast<const char*>(image.image.data()), image.image.size());
            file.close();
        }
    }

    bool exportResult = false;
    if (!exporter->Initialize(filename.c_str(), fileFormat, ios)) {
        FbxString error = exporter->GetStatus().GetErrorString();
        TF_FATAL_ERROR("FbxExporter::Initialize() failed: %s.\n", error.Buffer());
    } else {
        FbxEmbeddedFileCallback* writeCallback =
          FbxEmbeddedFileCallback::Create(fbx.manager, "EmbeddedFileCallback");
        writeCallback->RegisterWriteFunction(EmbedWriteCBFunction, (void*)&fbx);
        exporter->SetEmbeddedFileWriteCallback(writeCallback);
        exportResult = exporter->Export(fbx.scene);
        if (!exportResult) {
            FbxString error = exporter->GetStatus().GetErrorString();
            TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Call to FbxExporter::Export() failed.\n");
            TF_RUNTIME_ERROR(FILE_FORMAT_FBX, "Error returned: %s\n\n", error.Buffer());
        }
        writeCallback->Destroy();
    }

    exporter->Destroy();
    ios->Destroy();
    return exportResult;
}

std::string
GetNodeFullPath(FbxNode* node, std::string sceneRoot)
{
    if (node->GetScene()->GetRootNode() == node)
        return sceneRoot;

    return GetNodeFullPath(node->GetParent(), sceneRoot) + "/" + node->GetName();
}

TfToken
fbxGetInterpolation(FbxGeometryElement::EMappingMode mappingMode)
{
    switch (mappingMode) {
        case FbxGeometryElement::EMappingMode::eByPolygonVertex:
            return UsdGeomTokens->faceVarying;
        case FbxGeometryElement::EMappingMode::eByPolygon:
            return UsdGeomTokens->uniform;
        case FbxGeometryElement::EMappingMode::eByControlPoint:
            return UsdGeomTokens->vertex;
        case FbxGeometryElement::EMappingMode::eAllSame:
            return UsdGeomTokens->constant;
        default:
            return UsdGeomTokens->vertex;
    }
}

float
readPropValue(FbxPropertyT<FbxDouble> prop)
{
    return prop.Get();
}

GfVec3f
readPropValue(FbxPropertyT<FbxDouble3> prop)
{
    GfVec3f value;
    FbxDouble3 propValue = prop.Get();
    value[0] = propValue[0];
    value[1] = propValue[1];
    value[2] = propValue[2];
    return value;
}

fbxsdk::FbxVector4
GetFBXRotationFromUsdQuat(PXR_NS::GfQuath quat)
{
    PXR_NS::GfRotation rotation{ quat };
    GfVec3d rotationAxis = rotation.GetAxis();
    double rotationDegrees = rotation.GetAngle();
    fbxsdk::FbxQuaternion FbxQuaternion{
        fbxsdk::FbxVector4(rotationAxis[0], rotationAxis[1], rotationAxis[2]), rotationDegrees
    };
    return FbxQuaternion.DecomposeSphericalXYZ();
}

fbxsdk::FbxVector4
GetFBXRotationFromUsdQuat(PXR_NS::GfQuatf quat)
{
    PXR_NS::GfRotation rotation{ quat };
    GfVec3d rotationAxis = rotation.GetAxis();
    double rotationDegrees = rotation.GetAngle();
    fbxsdk::FbxQuaternion FbxQuaternion{
        fbxsdk::FbxVector4(rotationAxis[0], rotationAxis[1], rotationAxis[2]), rotationDegrees
    };
    return FbxQuaternion.DecomposeSphericalXYZ();
}

PXR_NS::GfMatrix4d
GetUSDMatrixFromFBX(const fbxsdk::FbxAMatrix& fbxMatrix)
{
    PXR_NS::GfMatrix4d matrix{};

    matrix[0][0] = fbxMatrix[0][0];
    matrix[0][1] = fbxMatrix[0][1];
    matrix[0][2] = fbxMatrix[0][2];
    matrix[0][3] = fbxMatrix[0][3];

    matrix[1][0] = fbxMatrix[1][0];
    matrix[1][1] = fbxMatrix[1][1];
    matrix[1][2] = fbxMatrix[1][2];
    matrix[1][3] = fbxMatrix[1][3];

    matrix[2][0] = fbxMatrix[2][0];
    matrix[2][1] = fbxMatrix[2][1];
    matrix[2][2] = fbxMatrix[2][2];
    matrix[2][3] = fbxMatrix[2][3];

    matrix[3][0] = fbxMatrix[3][0];
    matrix[3][1] = fbxMatrix[3][1];
    matrix[3][2] = fbxMatrix[3][2];
    matrix[3][3] = fbxMatrix[3][3];

    return matrix;
}

fbxsdk::FbxAMatrix
GetFBXMatrixFromUSD(const PXR_NS::GfMatrix4d& matrix)
{
    fbxsdk::FbxAMatrix fbxMatrix{};

    fbxMatrix[0][0] = matrix[0][0];
    fbxMatrix[0][1] = matrix[0][1];
    fbxMatrix[0][2] = matrix[0][2];
    fbxMatrix[0][3] = matrix[0][3];

    fbxMatrix[1][0] = matrix[1][0];
    fbxMatrix[1][1] = matrix[1][1];
    fbxMatrix[1][2] = matrix[1][2];
    fbxMatrix[1][3] = matrix[1][3];

    fbxMatrix[2][0] = matrix[2][0];
    fbxMatrix[2][1] = matrix[2][1];
    fbxMatrix[2][2] = matrix[2][2];
    fbxMatrix[2][3] = matrix[2][3];

    fbxMatrix[3][0] = matrix[3][0];
    fbxMatrix[3][1] = matrix[3][1];
    fbxMatrix[3][2] = matrix[3][2];
    fbxMatrix[3][3] = matrix[3][3];

    return fbxMatrix;
}

fbxsdk::FbxVector4
GetFBXVec4(const PXR_NS::GfVec3f pxrVec, float w)
{
    return fbxsdk::FbxVector4{ pxrVec[0], pxrVec[1], pxrVec[2], w };
}

fbxsdk::FbxVector4
GetFBXVec4(const PXR_NS::GfVec4f pxrVec)
{
    return fbxsdk::FbxVector4{ pxrVec[0], pxrVec[1], pxrVec[2], pxrVec[3] };
}

PXR_NS::GfVec3f
toVec3f(fbxsdk::FbxDouble3 v)
{
    return GfVec3f(v[0], v[1], v[2]);
}

PXR_NS::GfVec3d
toVec3d(fbxsdk::FbxDouble3 v)
{
    return GfVec3d(v[0], v[1], v[2]);
}

// v is: { roll (x), pitch (Y), yaw (z) }
// Refer to https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
PXR_NS::GfQuatf
toQuatf(fbxsdk::FbxDouble3 v)
{
    double rx = v[0] * deg2rad * 0.5;
    double ry = v[1] * deg2rad * 0.5;
    double rz = v[2] * deg2rad * 0.5;
    double cr = cos(rx);
    double sr = sin(rx);
    double cp = cos(ry);
    double sp = sin(ry);
    double cy = cos(rz);
    double sy = sin(rz);
    float w = cr * cp * cy + sr * sp * sy;
    float x = sr * cp * cy - cr * sp * sy;
    float y = cr * sp * cy + sr * cp * sy;
    float z = cr * cp * sy - sr * sp * cy;
    return GfQuatf(w, x, y, z);
}

fbxsdk::FbxQuaternion
GetFBXQuat(PXR_NS::GfQuatf pxrQuat)
{
    float w = pxrQuat.GetReal();
    GfVec3f xyz = pxrQuat.GetImaginary();
    return fbxsdk::FbxQuaternion{ xyz[0], xyz[1], xyz[2], w };
}

fbxsdk::FbxQuaternion
GetFBXQuat(PXR_NS::GfQuatd pxrQuat)
{
    float w = pxrQuat.GetReal();
    GfVec3d xyz = pxrQuat.GetImaginary();
    return fbxsdk::FbxQuaternion{ xyz[0], xyz[1], xyz[2], w };
}

}
