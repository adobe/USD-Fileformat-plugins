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
#include <assetResolver/sbsarResolverCache.h>

#include <tbb/concurrent_hash_map.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sbsar {
SBSARResolverCache&
SBSARResolverCache::GetInstance()
{
    static SBSARResolverCache cache;
    return cache;
}

SBSARResolverCache::SBSARResolverCache() = default;

struct SBSARResolverCache::_Cache
{
    using _Map =
      tbb::concurrent_hash_map<std::string, std::shared_ptr<ArAsset>>;
    _Map _pathToEntryMap;
};

void
SBSARResolverCache::BeginCacheScope(VtValue* cacheScopeData)
{
    _caches.BeginCacheScope(cacheScopeData);
    threadCacheCount.local()++;
    // TF_STATUS("Cache count %d", threadCacheCount.local());
}

void
SBSARResolverCache::EndCacheScope(VtValue* cacheScopeData)
{
    _caches.EndCacheScope(cacheScopeData);
    threadCacheCount.local()--;
    // TF_STATUS("Cache count %d", threadCacheCount.local());
}

SBSARResolverCache::_CachePtr
SBSARResolverCache::_GetCurrentCache()
{
    return _caches.GetCurrentCache();
}

std::shared_ptr<ArAsset>
SBSARResolverCache::findCachedAsset(const std::string& path)
{
    _CachePtr currentCache = _GetCurrentCache();
    if (currentCache) {
        _Cache::_Map::const_accessor accessor;
        if (currentCache->_pathToEntryMap.find(accessor, path)) {
            return accessor->second;
        }
    } else {
        // TF_STATUS("No cache found");
    }
    return std::shared_ptr<ArAsset>();
}

void
SBSARResolverCache::addCachedAsset(std::string& path,
                                   std::shared_ptr<ArAsset>& asset)
{
    _CachePtr currentCache = _GetCurrentCache();
    if (currentCache) {
        _Cache::_Map::accessor accessor;
        if (currentCache->_pathToEntryMap.insert(accessor, path)) {
            // TF_STATUS("Adding to cache %s", path.c_str());
            accessor->second = asset;
        }
    } else {
        // TF_STATUS("No cache found");
    }
}

void
SBSARResolverCache::dumpStats()
{
    _CachePtr currentCache = _GetCurrentCache();
    if (currentCache) {
    }
}
}
