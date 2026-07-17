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

#include <sandbox/utilities/sandboxAssetCache.h>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/ar/packageUtils.h>

namespace adobe::usd::sandbox {

PXR_NAMESPACE_USING_DIRECTIVE;

namespace {

// Normalize a cache key so producer and consumer agree regardless of how their respective
// resolvers spelled the path. On Windows the import side stores keys with backslashes from
// the SdfLayer's authored asset paths (`C:\…\file.gltf[image.png]`), while the export side
// looks them up with USD-normalized forward slashes (`C:/…/file.gltf[image.png]`); without
// normalization the lookup misses and intermediate assets that only live in this cache
// (e.g. images synthesized during import) cannot be retrieved.
std::string
canonicalKey(const std::string& path)
{
    auto [outer, inner] = ArSplitPackageRelativePathInner(path);
    if (inner.empty()) {
        return TfNormPath(path);
    }
    return ArJoinPackageRelativePath(TfNormPath(outer), inner);
}

}

SandboxAssetCache&
SandboxAssetCache::GetInstance()
{
    static SandboxAssetCache instance;
    return instance;
}

void
SandboxAssetCache::AddImageToCache(const std::string& path,
                                   const std::shared_ptr<PXR_NS::ArAsset>& asset)
{
    std::lock_guard<std::mutex> lock(_assetCacheMutex);
    _assetCache[canonicalKey(path)] = asset;
}

void
SandboxAssetCache::RemoveImageFromCache(const std::string& path)
{
    std::lock_guard<std::mutex> lock(_assetCacheMutex);
    _assetCache.erase(canonicalKey(path));
}

std::shared_ptr<PXR_NS::ArAsset>
SandboxAssetCache::FindCachedAsset(const std::string& path)
{
    std::lock_guard<std::mutex> lock(_assetCacheMutex);
    const auto it = _assetCache.find(canonicalKey(path));
    return it != _assetCache.end() ? it->second : nullptr;
}

// TODO: Add garbage collection. See AssetCacheSingleton for a possible implementation

} // namespace adobe::usd::sandbox