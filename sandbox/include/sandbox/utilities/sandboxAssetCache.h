/*
Copyright 2025 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#pragma once

#include <sandbox/api.h>
#include <sandbox/protocol/assetSerializerUtil.h>

#include "pxr/usd/ar/asset.h"

#include <mutex>
#include <unordered_map>

namespace adobe::usd::sandbox {

/**
 * Singleton class for storing texture assets that have been converted in a sandbox. Those assets
 * will be passed through shared memory from a sandboxed process. By the time the asset resolver
 * will likely need to access them, the shared memory will have been destroyed. This class
 * provides a way to store the assets so that the SandboxProxyResolver can access them.
 *
 * TODO: Add garbage collection
 */
class USDSANDBOX_API SandboxAssetCache
{
public:
    /// Return the process-wide singleton instance.
    static SandboxAssetCache& GetInstance();

    /**
     * Insert or replace an asset in the cache under @p path.
     *
     * @param path  The authored asset path used as the cache key.
     * @param asset The resolved asset to store.
     */
    void AddImageToCache(const std::string& path, const std::shared_ptr<PXR_NS::ArAsset>& asset);

    /**
     * Remove the cached entry for @p path, if present.
     *
     * @param path The authored asset path to evict.
     */
    void RemoveImageFromCache(const std::string& path);

    /**
     * Look up a cached asset by its authored path.
     *
     * @param path The authored asset path to search for.
     * @return The cached asset, or nullptr if not found.
     */
    std::shared_ptr<PXR_NS::ArAsset> FindCachedAsset(const std::string& path);

    // This function may be useful for garbage collection. Taken from assetResolver.h

    // remove items from cache with an expiration period of 60 seconds
    // and do not have the excludedPath key
    // void garbageCollectCacheExcluding(const std::string& excludedPath);

private:
    SandboxAssetCache() = default;
    SandboxAssetCache(const SandboxAssetCache& cache) = delete;
    SandboxAssetCache& operator=(const SandboxAssetCache& cache) = delete;

    std::mutex _assetCacheMutex;
    AssetMap _assetCache;
};

} // namespace adobe::usd