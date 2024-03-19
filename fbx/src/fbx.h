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
#pragma once
#include <fbxsdk.h>
#include <filesystem>
#include <map>
#include <pxr/pxr.h>
#include <sstream>
#include <string>
#include <usdData.h>
#include <utility>


// Dev Notes
// * FBX's `GetDirectArray()` can be troublesome when paired with `auto`! Better specify the full
// type:
//     ```
//     FbxLayerElementArrayTemplate<FbxColor>& fbxColors = colorElement->GetDirectArray();
//     ```

namespace fbxsdk {
class FbxNode;
}

namespace adobe::usd {

struct ExportFbxOptions
{
    bool embedImages;
};

struct Fbx
{
    fbxsdk::FbxScene* scene;
    fbxsdk::FbxManager* manager;
    fbxsdk::FbxImporter* importer = nullptr;
    fbxsdk::FbxEmbeddedFileCallback* readCallback = nullptr;
    std::string filename;
    std::vector<ImageAsset> images;
    std::map<std::string, std::vector<char>> embeddedData;
    bool loadImages = true;
    Fbx();
    ~Fbx();
};

bool
readFbx(Fbx& fbx, const std::string& filename, bool onlyMaterials);

bool
writeFbx(const ExportFbxOptions& options, const Fbx& fbx, const std::string& filename);

void
printFbx(Fbx& fbx);

/**
 * @brief Utility function the get the full path for a FbxNode.
 *
 * @param node Target node.
 * @return std::string Full node path.
 */
std::string
GetNodeFullPath(fbxsdk::FbxNode* node, std::string sceneRoot);

PXR_NS::TfToken
fbxGetInterpolation(fbxsdk::FbxGeometryElement::EMappingMode mappingMode);
float
readPropValue(fbxsdk::FbxPropertyT<fbxsdk::FbxDouble> prop);
PXR_NS::GfVec3f
readPropValue(fbxsdk::FbxPropertyT<fbxsdk::FbxDouble3> prop);

/**
 * Decompose a USD quaternion into a FBX vector4.
 * @param quat Target quaternion.
 * @return FBX vector4 representation.
 */
fbxsdk::FbxVector4
GetFBXRotationFromUsdQuat(PXR_NS::GfQuath quat);
fbxsdk::FbxVector4
GetFBXRotationFromUsdQuat(PXR_NS::GfQuatf quat);

fbxsdk::FbxAMatrix
GetFBXMatrixFromUSD(const PXR_NS::GfMatrix4d& matrix);
PXR_NS::GfMatrix4d
GetUSDMatrixFromFBX(const fbxsdk::FbxAMatrix& matrix);

template<class T, class J>
J
ConvertMatrix4(const T& matrix)
{
    J result{};
    result[0][0] = matrix[0][0];
    result[0][1] = matrix[0][1];
    result[0][2] = matrix[0][2];
    result[0][3] = matrix[0][3];

    result[1][0] = matrix[1][0];
    result[1][1] = matrix[1][1];
    result[1][2] = matrix[1][2];
    result[1][3] = matrix[1][3];

    result[2][0] = matrix[2][0];
    result[2][1] = matrix[2][1];
    result[2][2] = matrix[2][2];
    result[2][3] = matrix[2][3];

    result[3][0] = matrix[3][0];
    result[3][1] = matrix[3][1];
    result[3][2] = matrix[3][2];
    result[3][3] = matrix[3][3];
    return result;
}

// Vectors
fbxsdk::FbxVector4
GetFBXVec4(const PXR_NS::GfVec3f pxrVec, float w = 1.0f);
fbxsdk::FbxVector4
GetFBXVec4(const PXR_NS::GfVec4f pxrVec);

PXR_NS::GfVec3f
toVec3f(fbxsdk::FbxDouble3 v);
PXR_NS::GfVec3d
toVec3d(fbxsdk::FbxDouble3 v);
PXR_NS::GfQuatf
toQuatf(fbxsdk::FbxDouble3 v);

// Quaternions
fbxsdk::FbxQuaternion
GetFBXQuat(PXR_NS::GfQuatf pxrQuat);
fbxsdk::FbxQuaternion
GetFBXQuat(PXR_NS::GfQuatd pxrQuat);

}
