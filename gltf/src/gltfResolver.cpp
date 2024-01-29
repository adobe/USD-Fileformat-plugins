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
#include "gltf.h"
#include "gltfImport.h"
#include <pxr/usd/ar/definePackageResolver.h>
#include <resolver.h>

using namespace PXR_NS;
namespace adobe::usd {

AR_DEFINE_PACKAGE_RESOLVER(GltfResolver, ArPackageResolver);

GltfResolver::GltfResolver()
  : Resolver("GltfResolver")
{
}

void
GltfResolver::readCache(const std::string& filename, std::vector<ImageAsset>& images)
{
    UsdData usd;
    tinygltf::Model gltf;
    ImportGltfOptions options;
    options.importGeometry = false;
    options.importMaterials = true;
    options.importImages = true;
    readGltf(gltf, filename);
    importGltf(options, gltf, usd, filename);
    images = std::move(usd.images);
}

}