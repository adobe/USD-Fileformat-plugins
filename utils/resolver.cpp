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
#include "resolver.h"
#include "common.h"
#include "debugCodes.h"
#include "images.h"
#include <pxr/base/tf/fileUtils.h>
#include <pxr/pxr.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/sdf/layer.h>

#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

using namespace PXR_NS;
namespace adobe::usd {

namespace { // anonymous

struct AssetMap
{
    std::chrono::time_point<std::chrono::steady_clock> creationTime;

    // mapping of asset path to ArAsset (ie ImageArAsset)
    std::unordered_map<std::string, std::shared_ptr<PXR_NS::ArAsset>> assets;
};

// a recursive mutex is used as populateCache (which acquires a lock) can be
// called from OpenAsset which also acquires the lock.
static std::recursive_mutex assetCacheMutex;

// cache maps resolvedPackagePath to asset paths
static std::unordered_map<std::string, AssetMap> assetCache;

// remove items from cache with an expiration period of 60 seconds
// and do not have the excludedPath key
void
_garbageCollectCacheExcluding(const std::string& excludedPath)
{
    using namespace std::chrono_literals;

    std::lock_guard lock(assetCacheMutex);

    auto currentTime = std::chrono::steady_clock::now();

    // garbage collect entries in the cache older than 60 seconds
    for (auto it = assetCache.begin(); it != assetCache.end();) {
        std::chrono::seconds timePassed =
          std::chrono::duration_cast<std::chrono::seconds>(currentTime - it->second.creationTime);
        if (timePassed > 60s && it->first != excludedPath) {
            TF_DEBUG_MSG(
              UTIL_PACKAGE_RESOLVER, "Removing cached items for package '%s'\n", it->first.c_str());
            it = assetCache.erase(it);
        } else {
            ++it;
        }
    }
}

} // end anonymous namespace

// Simple ArAsset that works as a wrapper around a data vector.
// USD documents an ArInMemoryAsset, but it exists nowhere in the code.
// Ideally, when available, use that one instead of defining our own.
class ImageArAsset : public ArAsset
{
  public:
    explicit ImageArAsset(const std::vector<uint8_t>&& data)
      : _data(data){};
    const std::vector<uint8_t>& getData() const { return _data; }
    virtual size_t GetSize() const override { return _data.size(); }

  private:
    std::vector<uint8_t> _data;

    virtual std::shared_ptr<const char> GetBuffer() const override
    {
        return std::shared_ptr<const char>((const char*)_data.data(), [](const char*) {});
    }

    virtual size_t Read(void* buffer, size_t count, size_t offset) const override
    {
        return (size_t)memcpy(buffer, _data.data() + offset, count);
    }

    virtual std::pair<FILE*, size_t> GetFileUnsafe() const override
    {
        std::pair<FILE*, size_t> p(0, 0);
        return p;
    }
};

Resolver::Resolver(const std::string& name)
  : name(name)
{
    std::thread::id threadId = std::this_thread::get_id();
    std::stringstream ss;
    ss << threadId;
    TF_DEBUG_MSG(
      UTIL_PACKAGE_RESOLVER, "%s: %p::%s Created\n", name.c_str(), this, ss.str().c_str());
}

Resolver::~Resolver()
{
    std::thread::id threadId = std::this_thread::get_id();
    std::stringstream ss;
    ss << threadId;
    TF_DEBUG_MSG(
      UTIL_PACKAGE_RESOLVER, "%s: %p::%s Destroyed\n", name.c_str(), this, ss.str().c_str());
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
                 name.c_str(),
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

    std::lock_guard lock(assetCacheMutex);
    AssetMap* assetMap = nullptr;
    auto it = assetCache.find(resolvedPackagePath);
    if (it != assetCache.end()) {
        TF_DEBUG_MSG(
          UTIL_PACKAGE_RESOLVER, "%s: %p::%s Cached file", name.c_str(), this, ss.str().c_str());
        assetMap = &it->second;
    } else {
        TF_DEBUG_MSG(UTIL_PACKAGE_RESOLVER,
                     "%s: %p::%s Open file %s\n",
                     name.c_str(),
                     this,
                     ss.str().c_str(),
                     resolvedPackagePath.c_str());
        std::vector<adobe::usd::ImageAsset> images;
        readCache(resolvedPackagePath, images); // to be defined in each plugin

        Resolver::populateCache(resolvedPackagePath, std::move(images));
        assetMap = &assetCache[resolvedPackagePath];
    }
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
    std::lock_guard lock(assetCacheMutex);
    assetCache.erase(resolvedPackagePath);
}

void
Resolver::populateCache(const std::string& resolvedPackagePath, std::vector<ImageAsset>&& images)
{
    std::lock_guard lock(assetCacheMutex);
    auto it = assetCache.find(resolvedPackagePath);
    AssetMap& assetMap = assetCache[resolvedPackagePath];
    if (it == assetCache.end()) {
        assetMap.creationTime = std::chrono::steady_clock::now();
    }
    for (auto& imageAsset : images) {
        assetMap.assets[imageAsset.uri] =
          std::make_shared<ImageArAsset>(std::move(imageAsset.image));
    }

    // garbage collect after populating
    _garbageCollectCacheExcluding(resolvedPackagePath);
}

}
