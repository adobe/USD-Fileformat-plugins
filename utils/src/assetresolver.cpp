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
#include <fileformatutils/assetresolver.h>
#include <fileformatutils/debugCodes.h>
#include <pxr/usd/ar/asset.h>

#include <chrono>

using namespace PXR_NS;
namespace adobe::usd {

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

void AssetCacheSingleton::garbageCollectCacheExcluding(const std::string& excludedPath) {
    using namespace std::chrono_literals;
    
    auto currentTime = std::chrono::steady_clock::now();
    std::lock_guard<std::recursive_mutex> lock(mAssetCacheMutex);

    // Garbage collect entries in the cache older than 60 seconds
    for (auto it = mAssetCache.begin(); it != mAssetCache.end();) {
        std::chrono::seconds timePassed =
            std::chrono::duration_cast<std::chrono::seconds>(currentTime - it->second.creationTime);
        if (timePassed > 60s && it->first != excludedPath) {
            TF_DEBUG_MSG(
                UTIL_PACKAGE_RESOLVER, "Removing cached items for package '%s'\n", it->first.c_str());
            it = mAssetCache.erase(it);
        } else {
            ++it;
        }
    }
}

void AssetCacheSingleton::clearCache(const std::string& resolvedPackagePath) {
    std::lock_guard lock(mAssetCacheMutex);
    mAssetCache.erase(resolvedPackagePath);
}

void AssetCacheSingleton::populateCache(const std::string& resolvedPackagePath, std::vector<ImageAsset>&& images) {
    std::lock_guard<std::recursive_mutex> lock(mAssetCacheMutex);

    auto it = mAssetCache.find(resolvedPackagePath);
    AssetMap& assetMap = mAssetCache[resolvedPackagePath];
    if (it == mAssetCache.end()) {
        assetMap.creationTime = std::chrono::steady_clock::now();
    }
    for (auto& imageAsset : images) {
        assetMap.assets[imageAsset.uri] =
          std::make_shared<ImageArAsset>(std::move(imageAsset.image));
    }
}

AssetMap* AssetCacheSingleton::acquireAssetMap(const std::string& resolvedPackagePath,
                         const std::string& resolvedPackagedPath,
                         std::stringstream& ss,
                         std::function<void(const std::string&, std::vector<adobe::usd::ImageAsset>&)> readCache) {
    AssetMap* assetMap = nullptr;    
    std::lock_guard<std::recursive_mutex> lock(mAssetCacheMutex);
    
    auto it = mAssetCache.find(resolvedPackagePath);
    if (it != mAssetCache.end()) {
        TF_DEBUG_MSG(
            UTIL_PACKAGE_RESOLVER, "%s: %p::%s Cached file", resolvedPackagedPath.c_str(), this, ss.str().c_str());
        assetMap = &it->second;
    } else {
        TF_DEBUG_MSG(UTIL_PACKAGE_RESOLVER,
                     "%s: %p::%s Open file %s\n",
                     resolvedPackagedPath.c_str(),
                     this,
                     ss.str().c_str(),
                     resolvedPackagePath.c_str());
        std::vector<adobe::usd::ImageAsset> images;
        readCache(resolvedPackagePath, images); // to be defined in each plugin

        populateCache(resolvedPackagePath, std::move(images));
        assetMap = &mAssetCache[resolvedPackagePath];
    }
    return assetMap;
}


} // namespace adobe::usd