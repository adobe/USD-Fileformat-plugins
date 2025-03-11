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
#include <fileformatutils/usdData.h>
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

// Scale between intensity of FBX lights and USD lights. This can easily be changed if the USD
// lighting doesn't match
constexpr float FBX_TO_USD_INTENSITY_SCALE_FACTOR = 1.0;

constexpr float DEFAULT_POINT_LIGHT_RADIUS = 0.01; // 1 cm
constexpr float DEFAULT_SPOT_LIGHT_RADIUS = 0.1;   // 10 cm

// Camera rotation to apply to revert to FBX coordinates, on export. Inspired by the Blender code
// base, which converts from -Z to +X with a 90º rotation around the Y axis:
// https://github.com/blender/blender/blob/e1a44ad129d53fbd47215845be2c42fb0850135d/scripts/addons_core/io_scene_fbx/fbx_utils.py#L74C64-L74C88
inline FbxDouble3 CAMERA_ROTATION_OFFSET_EXPORT(0.0f, 90.f, 0.f);

// Light rotation to apply to revert to FBX coordinates, on export. Inspired by the Blender code
// base, which converts from -Z to -Y with a 90º rotation around the X axis:
// https://github.com/blender/blender/blob/e1a44ad129d53fbd47215845be2c42fb0850135d/scripts/addons_core/io_scene_fbx/fbx_utils.py#L73C63-L73C87
inline FbxDouble3 LIGHT_ROTATION_OFFSET_EXPORT(90.f, 0.f, 0.f);

struct ExportFbxOptions
{
    bool embedImages = false;
    std::string exportParentPath;
    PXR_NS::TfToken outputColorSpace;
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

/*
 * importImages Indicates whether the fbx should be set to load image data. It should be true if
 * the images are being written out, and false otherwise
 *
 * onlyMaterials Indicates whether the fbx should only load materials. It should only be true if
 * the file is being loaded just to separately load image textures, and nothing else is being used
 */
bool
readFbx(Fbx& fbx, const std::string& filename, bool importImages, bool onlyMaterials);

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
