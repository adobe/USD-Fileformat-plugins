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
/// TODO refactor Skeleton import. Currently doing:
/// 1. Import all glTF nodes, whether joint or not, as Xforms in the USD prim hierarchy.
/// 2. Also import glTF joints into the joints attribute in USD skeletons.
/// 3. XForms acting as root joints housing the hierarchy from (1) and UsdSkelRoot prims housing a
///    skeleton from (2), are authored as siblings, so as to prevent the (root joint) XForm's
///    transform to take effect 2 times.
///
/// Instead we should identify complete isolated skeleton joint hierarchies
/// in the glTF node hierarchy (so no embedded skeletons), then create USD skeletons from this.
/// Then associate glTF skin objects to USD SkelBindings.
bool
importGltf(const ImportGltfOptions& options, tinygltf::Model& model, UsdData& usd, const std::string& filename);

}