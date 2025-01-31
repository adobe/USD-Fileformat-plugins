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
#include <fileformatutils/usdData.h>
#include <tiny_gltf.h>
#include <unordered_map>
#include <vector>

namespace adobe::usd {

struct ImportGltfOptions;

struct ImportGltfContext
{
    const ImportGltfOptions* options = nullptr;
    const tinygltf::Model* gltf = nullptr;
    UsdData* usd = nullptr;
    std::string path;
    std::vector<int> nodeMap;
    std::vector<int> parentMap;
    std::vector<std::string> skeletonNodeNames;
    std::vector<std::vector<int>> meshes;
    std::vector<int> meshUseCount;

    // paths to files loaded on import
    PXR_NS::VtArray<std::string> filenames;

    // Caches the mapping from a GLTF texture index to the corresponding UsdImageIndex
    std::unordered_map<int, int> imageMap;

    // name uniqueness enforcer for image names
    UniqueNameEnforcer uniqueImageNameEnforcer;
};

} // end namespace adobe::usd
