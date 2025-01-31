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
#include "fbx.h"
#include <fileformatutils/usdData.h>

namespace adobe::usd {

struct ImportFbxOptions
{
    bool importGeometry = true;
    bool importMaterials = true;
    bool importImages = true;
    bool importPhong = false;
    bool importAnimationStacks = false;
    PXR_NS::TfToken originalColorSpace;
};

enum class FbxPropertyNumChannels
{
    One,
    Two,
    Three,
};

/// \ingroup usdfbx
/// \brief Import FBX data into a USD data cache.
///
/// **Settings**: Units and coordinate system map well between both formats, the plugin reads what's
/// in the FBX and writes it to the USD. The default in FBX is cm, and +y up axis.
///
/// **Materials**: The plugin reads all file textures from the FBX and caches them. Then it reads
/// all materials from the FBX and maps their parameters to their USD counterparts, where textures
/// might be mixed together. Particularly, a phong to PBR conversion might take place. It then
/// writes the resulting textures and materials to USD.
///
/// **Nodes and Skeletons**:
/// TODO: refactor skeletons. Currently doing:
/// 1. Import all FBX nodes, whether joint or not, into the USD node hierarchy.
/// 2. Also import FBX joints into corresponding USD skeletons.
/// 3. Instantiate USD skeleton and mesh at the node where a skinned mesh is instantiated in fbx.
///
/// Instead we should not import as USD nodes those FBX nodes that act as joints.
///
/// **Other**: The plugin traverses the node hierarhcy to import all other objects. (TODO change via
/// GetSrcObject on the scene)
bool
importFbx(const ImportFbxOptions& options, Fbx& fbx, UsdData& usd);

}