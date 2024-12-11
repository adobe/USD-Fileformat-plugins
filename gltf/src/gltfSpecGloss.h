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
#include "importGltfContext.h"
#include <fileformatutils/usdData.h>
#include <unordered_map>


namespace adobe::usd {

/// Convert a specular-glossiness material to a metallic-roughness material
bool
translateSpecularGlossinessToMetallicRoughness(ImportGltfContext& ctx,
                                               std::unordered_map<std::string, int>& cache,
                                               const Input& diffuseIn,
                                               const Input& specularIn,
                                               const Input& opacityIn,
                                               const std::string& alphaMode,
                                               Input& diffuseOut,
                                               Input& opacityOut,
                                               Input& metallicOut,
                                               Input& roughnessOut);

} // end namespace adobe::usd
