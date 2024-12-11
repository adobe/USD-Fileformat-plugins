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
#include <tiny_gltf.h>
#include <fileformatutils/usdData.h>

namespace adobe::usd {

struct ImportGltfOptions
{
    bool importGeometry = true;
    bool importMaterials = true;
    bool importImages = true;
};

/// \ingroup usdgltf
/// \brief search for key in cache. The keys are the texture names and values are the image indexes
int
lookupTexture(const std::unordered_map<std::string, int>& cache, const std::string& key);

/// \ingroup usdgltf
/// \brief read a double value from a gltf value
bool
readDoubleValue(const tinygltf::Value& val, double& value);

/// \ingroup usdgltf
/// \brief read texture data from a gltf value
bool
readTextureInfo(const tinygltf::Value& val, tinygltf::TextureInfo& textureInfo);

/// \ingroup usdgltf
/// \brief read a transform of a gltf texture
bool
importTextureTransform(const tinygltf::ExtensionMap& extensions, Input& input);

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

/// \ingroup usdgltf
/// \brief setup an image input for a texture
void
setInputImage(Input& input,
              int imageIndex,
              int uvIndex,
              const PXR_NS::TfToken& channel,
              const PXR_NS::TfToken& colorspace);

/// \ingroup usdgltf
/// \brief import a gltf image into the USD data cache
int
importImage(ImportGltfContext& ctx,
            int textureIndex,
            const std::string& materialName,
            const std::string& imageName);

bool
importTexture(const tinygltf::Model* gltf,
              int imageIndex,
              int textureIndex,
              int uvIndex,
              Input& input,
              const PXR_NS::TfToken& channel,
              const PXR_NS::TfToken& colorSpace);

void
importScale1(Input& input, double factor);

void
importValue1(Input& input, double value);

}