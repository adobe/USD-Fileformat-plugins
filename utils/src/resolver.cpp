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
#include <fileformatutils/resolver.h>
#include <fileformatutils/assetresolver.h>
#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>
#include <fileformatutils/images.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/pxr.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/sdf/layer.h>

#include <fstream>
#include <thread>

using namespace PXR_NS;
namespace adobe::usd {

Resolver::Resolver(const std::string& name)
  : mName(name)
{
    std::thread::id threadId = std::this_thread::get_id();
    std::stringstream ss;
    ss << threadId;
    TF_DEBUG_MSG(
      UTIL_PACKAGE_RESOLVER, "%s: %p::%s Created\n", mName.c_str(), this, ss.str().c_str());
}

Resolver::~Resolver()
{
    std::thread::id threadId = std::this_thread::get_id();
    std::stringstream ss;
    ss << threadId;
    TF_DEBUG_MSG(
      UTIL_PACKAGE_RESOLVER, "%s: %p::%s Destroyed\n", mName.c_str(), this, ss.str().c_str());
}

std::string
Resolver::Resolve(const std::string& resolvedPackagePath, const std::string& packagedPath)
{
    std::thread::id threadId = std::this_thread::get_id();
    std::stringstream ss;
    ss << threadId;
    std::string resolvedPackagedPath = packagedPath;
    TF_DEBUG_MSG(UTIL_PACKAGE_RESOLVER,
                 "%s: %p::%s Resolved: %s\n",
                 mName.c_str(),
                 this,
                 ss.str().c_str(),
                 resolvedPackagedPath.c_str());
    return resolvedPackagedPath;
}

std::shared_ptr<ArAsset>
Resolver::OpenAsset(const std::string& resolvedPackagePath, const std::string& resolvedPackagedPath)
{
    std::thread::id threadId = std::this_thread::get_id();
    std::stringstream ss;
    ss << threadId;

    AssetMap* assetMap = AssetCacheSingleton::getInstance().acquireAssetMap(
        resolvedPackagePath, resolvedPackagedPath, ss,
        [this](const std::string& path, std::vector<adobe::usd::ImageAsset>& images) {
            readCache(path, images); // Assuming 'readCache' is a member function of the 'Resolver' class
        }
    );
    if (assetMap) {
        TF_DEBUG_MSG(UTIL_PACKAGE_RESOLVER, " : %s \n", resolvedPackagedPath.c_str());
        auto it = assetMap->assets.find(resolvedPackagedPath);
        if (it != assetMap->assets.end()) {
            return it->second;
        }
    }
    return std::shared_ptr<ArAsset>();
}

void
Resolver::BeginCacheScope(VtValue* data)
{
}

void
Resolver::EndCacheScope(VtValue* data)
{
}

void
Resolver::clearCache(const std::string& resolvedPackagePath)
{
    AssetCacheSingleton::getInstance().clearCache(resolvedPackagePath);
}

void
Resolver::populateCache(const std::string& resolvedPackagePath, std::vector<ImageAsset>&& images)
{
    AssetCacheSingleton& assetCacheInstance = AssetCacheSingleton::getInstance();
    assetCacheInstance.populateCache(resolvedPackagePath, std::move(images));

    // garbage collect after populating
    assetCacheInstance.garbageCollectCacheExcluding(resolvedPackagePath);
}

}
