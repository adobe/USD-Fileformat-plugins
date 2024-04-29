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
#include "gltf.h"
#include <tiny_gltf.h>
#include <usdData.h>

namespace adobe::usd {

struct ImportGltfOptions
{
    bool importGeometry = true;
    bool importMaterials = true;
    bool importImages = true;
};

/// \ingroup usdgltf
/// \brief Import glTF data into a USD data cache.
/// Imported metersPerUnits will be = 1, and upAxis = +y, as is the norm for all glTF.
///
/// All gltf nodes are imported as Xforms in the USD hierarchy.
/// Gltf skins are imported as Skeletons in USD with joints, bindTransforms and restTransforms.
/// For nodes with a mesh and skin we position the mesh under the root joint of the associated
/// skeleton.
bool
importGltf(const ImportGltfOptions& options,
           tinygltf::Model& model,
           UsdData& usd,
           const std::string& filename);

}