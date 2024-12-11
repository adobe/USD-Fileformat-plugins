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
#include <fileformatutils/materials.h>
#include <fileformatutils/usdData.h>

namespace adobe::usd {

using ExtMap = tinygltf::ExtensionMap;

struct ExportGltfOptions
{
    bool binary = false;
    bool embedImages = false;
    bool useMaterialExtensions = true;
};

struct ExportGltfContext
{
    ExportGltfOptions options;
    UsdData* usd = nullptr;
    tinygltf::Model* gltf = nullptr;
    // Any GLTF extensions used should be added here and marked as required if needed
    // These will be written to the GLTF model in the end, but the set is more efficient for adding
    // things only once.
    std::unordered_set<std::string> extensionsUsed;
    std::unordered_set<std::string> extensionsRequired;
    // Maps USD meshes to 1 or more glTF primitives.
    // If a USD mesh has no subsets, the USD mesh is mapped to a single glTF primitive.
    // If a USD mesh has subsets, each subset maps to a glTF primitive.
    std::vector<std::vector<tinygltf::Primitive>> primitiveMap;

    // Map used to detect mesh instancing
    std::unordered_map<int, int> usdMeshIndexToGltfMeshIndexMap;

    // Map to convert from USD node indices to glTF node indices. Created in exportNode()
    std::unordered_map<int, int> usdNodesToGltfNodes;
};

/// \ingroup usdgltf
/// \brief Add a Material to an extension.
void
addMaterialExt(ExportGltfContext& ctx,
               tinygltf::Material& gltfMaterial,
               const std::string& extensionName,
               const ExtMap& ext);

/// \ingroup usdgltf
/// \brief Add a float value to an extension.
void
addFloatValueToExt(ExtMap& ext, const std::string& name, float value);

/// \ingroup usdgltf
/// \brief Add an extension to the glTF model.
bool
addTextureToExt(ExportGltfContext& ctx,
                InputTranslator& inputTranslator,
                ExtMap& ext,
                const Input& input,
                const std::string& textureName,
                const std::string& factorName = std::string(),
                float factorDefaultValue = 0.0f);

/// \ingroup usdgltf
/// \brief Export a texture.
void
exportTexture(ExportGltfContext& ctx, const Input& input, int& textureIndex, int& texCoord);

/// \ingroup usdgltf
/// \brief Export USD data to a glTF model.
bool
exportGltf(const ExportGltfOptions& options, UsdData& data, tinygltf::Model& model);

}