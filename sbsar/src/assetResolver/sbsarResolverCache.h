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

#include <pxr/usd/ar/threadLocalScopedCache.h>

PXR_NAMESPACE_OPEN_SCOPE
class ArAsset;
PXR_NAMESPACE_CLOSE_SCOPE

namespace adobe::usd::sbsar {
class SBSARResolverCache
{
  public:
    static SBSARResolverCache& GetInstance();

    SBSARResolverCache(const SBSARResolverCache&) = delete;
    SBSARResolverCache& operator=(const SBSARResolverCache&) = delete;
    void BeginCacheScope(PXR_NS::VtValue* cacheScopeData);
    void EndCacheScope(PXR_NS::VtValue* cacheScopeData);
    std::shared_ptr<PXR_NS::ArAsset> findCachedAsset(const std::string& path);
    void addCachedAsset(std::string& path,
                        std::shared_ptr<PXR_NS::ArAsset>& asset);
    void dumpStats();

  private:
    SBSARResolverCache();

    struct _Cache;
    using _ThreadLocalCaches = PXR_NS::ArThreadLocalScopedCache<_Cache>;
    using _CachePtr = _ThreadLocalCaches::CachePtr;
    _CachePtr _GetCurrentCache();
    tbb::enumerable_thread_specific<int> threadCacheCount;

    _ThreadLocalCaches _caches;
};
}
