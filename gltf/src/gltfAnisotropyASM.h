/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#pragma once
#include "gltfAnisotropy.h"
#include "gltfExport.h"
#include <fileformatutils/usdData.h>
#include <unordered_map>

namespace adobe::usd {

// Gathers the anisotropy data from a glTF material and imports non-image ASM values.
bool
importAnisotropyData(ImportGltfContext& ctx,
                     const tinygltf::ExtensionMap& extensions,
                     const tinygltf::Value& anisoExt,
                     Material& m,
                     float roughness,
                     AnisotropyData& anisotropy,
                     Image& anisotropySrcImage);

// Imports anisotropy textures from a glTF material and updates the USD material.
void
importAnisotropyTexture(ImportGltfContext& ctx,
                        const tinygltf::Material& gm,
                        Material& m,
                        float roughness,
                        const AnisotropyData& anisotropyData,
                        const Image& anisotropySrcImage,
                        std::unordered_map<std::string, int>& cache);

// Exports the anisotropy extension to a glTF material.
void
exportAnisotropyExtension(
  ExportGltfContext& ctx,
  InputTranslator& inputTranslator,
  const Material& m,
  tinygltf::Material& gm,
  std::unordered_map<std::string, ExportTextureCacheItem>& constructedAnisotropyTextureCache);

} // end namespace adobe::usd
