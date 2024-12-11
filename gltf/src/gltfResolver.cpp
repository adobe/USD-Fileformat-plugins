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
#include "gltfResolver.h"
#include "debugCodes.h"
#include "fileFormat.h"
#include "gltf.h"
#include "gltfImport.h"

#include <pxr/usd/ar/definePackageResolver.h>

using namespace PXR_NS;
namespace adobe::usd {

AR_DEFINE_PACKAGE_RESOLVER(GltfResolver, ArPackageResolver);

GltfResolver::GltfResolver()
  : Resolver("GltfResolver")
{
}

void
GltfResolver::readCache(const std::string& resolvedPath, std::vector<ImageAsset>& images)
{
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "readCache: %s\n", resolvedPath.c_str());

    std::shared_ptr<ArAsset> asset;
    std::string baseDir;
    bool isAscii = false;
    if (!UsdGltfFileFormat::OpenGltfAsset(resolvedPath, asset, baseDir, isAscii)) {
        return;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    size_t bufferSize = asset->GetSize();
    TF_DEBUG_MSG(
      FILE_FORMAT_GLTF, "Type: %s, Size: %zu KB\n", isAscii ? "GLTF" : "GLB", bufferSize >> 10);

    tinygltf::Model gltf;
    VOID_GUARD(readGltfFromMemory(gltf, baseDir, isAscii, &*buffer, bufferSize),
               "Error reading glTF file\n");

    UsdData usd;
    ImportGltfOptions options;
    options.importGeometry = false;
    options.importMaterials = true;
    options.importImages = true;
    importGltf(options, gltf, usd, resolvedPath);

    images = std::move(usd.images);
}

}