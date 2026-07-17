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

// Imports anisotropy data from a glTF material into an OpenPBR material.
void
importAnisotropyDataOpenPBR(ImportGltfContext& ctx,
                            const tinygltf::Material& gm,
                            const tinygltf::Value& anisoExt,
                            OpenPbrMaterial& m,
                            std::unordered_map<std::string, int>& anisotropyTextureCache);

// Exports the anisotropy extension to a glTF material from an OpenPBR material.
void
exportAnisotropyExtensionOpenPBR(
  ExportGltfContext& ctx,
  InputTranslator& inputTranslator,
  const OpenPbrMaterial& m,
  tinygltf::Material& gm,
  std::unordered_map<std::string, ExportTextureCacheItem>& constructedAnisotropyTextureCache,
  Input& newRoughnessInput);

} // end namespace adobe::usd
