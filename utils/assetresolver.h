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
#include "pxr/usd/ar/packageResolver.h"
#include "usdData.h"
#include <unordered_map>
#include <mutex>

namespace adobe::usd {

// Structure to represent asset mapping information.
struct AssetMap
{
    std::chrono::time_point<std::chrono::steady_clock> creationTime;

    // mapping of asset path to ArAsset (ie ImageArAsset)
    std::unordered_map<std::string, std::shared_ptr<PXR_NS::ArAsset>> assets;
};

// Singleton class for managing asset caching.
class AssetCacheSingleton {
public:
    static AssetCacheSingleton& getInstance() {
        static AssetCacheSingleton instance; // Instantiated once, when first accessed.
        return instance;
    }

    // remove items from cache with an expiration period of 60 seconds
    // and do not have the excludedPath key
    void garbageCollectCacheExcluding(const std::string& excludedPath);

    // clear the cache for a specific package
    void clearCache(const std::string& resolvedPackagePath);

    // add images to the asset cache
    void populateCache(const std::string& resolvedPackagePath, std::vector<ImageAsset>&& images);

    // acquire the asset map for a specific package
    AssetMap* acquireAssetMap(const std::string& resolvedPackagePath,
                              const std::string& resolvedPackagedPath,
                              std::stringstream& ss,
                              std::function<void(const std::string&, std::vector<adobe::usd::ImageAsset>&)> readCache);

private:
    AssetCacheSingleton() = default;
    std::recursive_mutex mAssetCacheMutex;
    std::unordered_map<std::string, AssetMap> mAssetCache;
};

} // namespace adobe::usd