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
#include "gltfAnisotropy.h"
#include "debugCodes.h"

#include "gltfExport.h"

using namespace PXR_NS;

namespace adobe::usd {

// Anisotropy textures can be 4x4 representing a single strength and rotation
constexpr size_t SINGLE_VALUE_IMAGE_DIM_SIZE = 4;

// Returns true if the image is a 4x4 containing a single anisotropy entry
bool
isSingleValueImage(const Image& image)
{
    return image.width == SINGLE_VALUE_IMAGE_DIM_SIZE &&
           image.height == SINGLE_VALUE_IMAGE_DIM_SIZE;
}

// Caches an image by writing it and updating the cache map.
int
cacheAndWriteImage(ImportGltfContext& ctx,
                   std::unordered_map<std::string, int>& cache,
                   const std::string& key,
                   const Image& image)
{
    auto [newIndex, usdImage] = ctx.usd->addImage();
    usdImage.name = key;
    usdImage.uri = key + ".png";
    usdImage.format = ImageFormatPng;

    if (!image.write(usdImage)) {
        TF_WARN("Failed to write anisotropy image: %s", key.c_str());
    }

    cache[key] = newIndex;

    // This commented call saves the image to disk which is a debugging aid. It should not be
    // enabled for normal operation.
    //
    // imageWrite(usdImage, "test/" + usdImage.uri, true);

    return newIndex;
}

void
addRotationToAnisotropyExt(tinygltf::ExtensionMap& ext, float rotation)
{
    addFloatValueToExt(ext, "anisotropyRotation", rotation);
}
void
addStrengthToAnisotropyExt(tinygltf::ExtensionMap& ext, float strength)
{
    addFloatValueToExt(ext, "anisotropyStrength", strength);
}

void
addTextureToAnisotropyExt(tinygltf::ExtensionMap& ext, int texIndex, int texCoord)
{
    // add anisotropy texture info to the extension
    std::map<std::string, tinygltf::Value> textureInfo;
    textureInfo["index"] = tinygltf::Value(texIndex);
    // the default texCoord is 0, so only add it to the extension if it's greater than 0
    if (texCoord > 0) {
        textureInfo["texCoord"] = tinygltf::Value(texCoord);
    }
    ext["anisotropyTexture"] = tinygltf::Value(textureInfo);
}

} // end namespace adobe::usd
