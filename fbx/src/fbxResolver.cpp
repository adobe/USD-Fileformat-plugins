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
#include "fbxResolver.h"
#include "debugCodes.h"
#include "fbx.h"
#include "fbxImport.h"
#include <fileformatutils/common.h>
#include <mutex>
#include <pxr/base/tf/stopwatch.h>
#include <pxr/usd/ar/definePackageResolver.h>

using namespace PXR_NS;
namespace adobe::usd {

AR_DEFINE_PACKAGE_RESOLVER(FbxResolver, ArPackageResolver);
static std::mutex mutex;

FbxResolver::FbxResolver()
  : Resolver("FbxResolver")
{
}

void
FbxResolver::readCache(const std::string& filename, std::vector<ImageAsset>& images)
{
    const std::lock_guard<std::mutex> lock(mutex); // FBX SDK is not thread safe
    Fbx fbx;
    UsdData usd;
    TfStopwatch watch;
    TF_DEBUG_MSG(FBX_PACKAGE_RESOLVER, "START TOTAL: %ld\n", static_cast<long int>(watch.GetMilliseconds()));
    watch.Start();
    VOID_GUARD(readFbx(fbx, filename, true, true), "Error reading FBX from %s\n", filename.c_str());
    watch.Stop();
    TF_DEBUG_MSG(FBX_PACKAGE_RESOLVER, "STOP TOTAL: %ld\n", static_cast<long int>(watch.GetMilliseconds()));
    ImportFbxOptions options;
    options.importGeometry = false;
    options.importMaterials = true;
    options.importImages = true;
    VOID_GUARD(importFbx(options, fbx, usd), "Error translating FBX to USD\n");
    images = std::move(usd.images);
}

}
