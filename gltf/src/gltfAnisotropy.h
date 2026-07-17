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
#include "gltf.h"
#include "importGltfContext.h"
#include <fileformatutils/images.h>
#include <string>
#include <unordered_map>

namespace adobe::usd {

constexpr double PI = 3.14159265358979311600;

struct AnisotropyData
{
    double strength = 0.0;
    double rotation = 0.0;
    tinygltf::TextureInfo texture; // rg are a 2D direction, b is a strength multiplier
};

// Returns true if the image is a 4x4 containing a single anisotropy entry
bool
isSingleValueImage(const Image& image);

// Caches an image by writing it and updating the cache map.
int
cacheAndWriteImage(ImportGltfContext& ctx,
                   std::unordered_map<std::string, int>& cache,
                   const std::string& key,
                   const Image& image);

// Adds anisotropyRotation key/value to the extension map.
void
addRotationToAnisotropyExt(tinygltf::ExtensionMap& ext, float rotation);

// Adds anisotropyStrength key/value to the extension map.
void
addStrengthToAnisotropyExt(tinygltf::ExtensionMap& ext, float strength);

// Adds anisotropyTexture key/value to the extension map.
void
addTextureToAnisotropyExt(tinygltf::ExtensionMap& ext, int texIndex, int texCoord);

} // end namespace adobe::usd
