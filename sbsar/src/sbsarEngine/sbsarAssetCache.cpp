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
#include <sbsarEngine/sbsarAssetCache.h>

#include <pxr/base/tf/diagnosticLite.h>
#include <sbsarDebug.h>
#include <sbsarEngine/sbsarPackageCache.h>
#include <sbsarEngine/sbsarRenderThread.h>
#include <usdGeneration/usdGenerationHelpers.h>

#include <chrono>
#include <map>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE
namespace adobe::usd::sbsar {
namespace {
std::string
computeKey(const adobe::usd::sbsar::ParsePathResult& pathResult)
{
    return std::to_string(pathResult.packageHash) + pathResult.graphName +
           pathResult.inputParameters;
}
}

std::shared_ptr<PXR_NS::ArAsset>
RenderResultCache::getAsset(const std::string& usage)
{
    auto it = m_assets.find(usage);
    if (it == m_assets.end()) {
        // Every asset computed after a rendering should be in the cache.
        TF_RUNTIME_ERROR("AssetCache: Asset not found");
        return nullptr;
    }
    return it->second;
}

void
RenderResultCache::addAsset(const std::string& usage, const std::shared_ptr<pxr::ArAsset>& asset)
{
    m_assets[usage] = asset;
}

VtValue
RenderResultCache::getNumericalValue(const std::string& usage)
{
    auto it = m_numericalValues.find(usage);
    if (it == m_numericalValues.end()) {
        TF_WARN("AssetCache: Numerical value not in the cache, (could be a disconnected output)");
        return VtValue();
    }
    return it->second;
}

void
RenderResultCache::addNumericalValue(const std::string& usage, const pxr::VtValue& value)
{
    m_numericalValues[usage] = value;
}

std::size_t
RenderResultCache::getSize()
{
    return m_size;
}

void
RenderResultCache::computeSize()
{
    m_size = 0;
    for (const auto& asset : m_assets) {
        m_size += asset.second->GetSize();
    }
}

std::chrono::time_point<std::chrono::steady_clock>
RenderResultCache::getLastAccessTime() const
{
    return m_lastAccessTime;
}

void
RenderResultCache::updateLastAccessTime()
{
    m_lastAccessTime = std::chrono::steady_clock::now();
}
std::size_t
RenderResultCache::getAssetCount()
{
    return m_assets.size();
}

bool
AssetCache::hasRenderResult(const adobe::usd::sbsar::ParsePathResult& pathResult)
{
    std::string hash = computeKey(pathResult);
    return m_assets.find(hash) != m_assets.end();
}

std::shared_ptr<pxr::ArAsset>
AssetCache::getAsset(const adobe::usd::sbsar::ParsePathResult& pathResult)
{
    std::string hash = computeKey(pathResult);
    auto asset = m_assets.find(hash);
    if (asset == m_assets.end())
        return nullptr;
    asset->second.updateLastAccessTime();
    return asset->second.getAsset(pathResult.usage);
}

VtValue
AssetCache::getNumericalValue(const adobe::usd::sbsar::ParsePathResult& pathResult)
{
    std::string hash = computeKey(pathResult);
    auto asset = m_assets.find(hash);
    if (asset == m_assets.end())
        return VtValue();
    asset->second.updateLastAccessTime();
    return asset->second.getNumericalValue(pathResult.usage);
}

void
AssetCache::addRenderResult(const adobe::usd::sbsar::ParsePathResult& pathResult,
                            RenderResultCache&& renderResult)
{
    renderResult.computeSize();
    // Before adding a new entry, check the cache size and clean the cache if necessary to ensure there is enough space
    if (m_size + renderResult.getSize() > getCacheSize().getMaxAssetCacheSize())
        cleanCache();
    renderResult.updateLastAccessTime();
    std::size_t assetCount = renderResult.getAssetCount();
    std::string hash = computeKey(pathResult);
    auto [it, isInserted] = m_assets.insert_or_assign(hash, std::move(renderResult));
    if (isInserted) {
        m_size += it->second.getSize();
        getCacheStats().assetCreated += assetCount;
    } else {
        TF_RUNTIME_ERROR("AssetCache: Should never happen");
    }
}

void
AssetCache::clearCache()
{
    m_assets.clear();
    m_size = 0;
}

void
AssetCache::cleanCache()
{
    TF_DEBUG(SBSAR_RENDER).Msg("AssetCache: Cleaning cache\n");
    // Sort m_assets by creation time and delete the oldest 10%.
    std::chrono::time_point<std::chrono::steady_clock> oldtestTimeToRemove;
    std::map<std::chrono::time_point<std::chrono::steady_clock>, std::size_t> timeSizeMap;
    for (auto& [hash, asset] : m_assets) {
        timeSizeMap[asset.getLastAccessTime()] = asset.getSize();
    }
    std::size_t currentSize = 0;
    std::size_t toDeleteSize = m_size / 10;
    for (auto& [time, size] : timeSizeMap) {
        if (currentSize >= toDeleteSize)
            break;
        currentSize += size;
        oldtestTimeToRemove = time;
    }

    std::size_t nbAssetDeleted = 0;
    for (auto it = m_assets.begin(); it != m_assets.end();) {
        if (it->second.getLastAccessTime() <= oldtestTimeToRemove) {
            m_size -= it->second.getSize();
            nbAssetDeleted += it->second.getAssetCount();
            it = m_assets.erase(it);
        } else
            ++it;
    }

    getCacheStats().assetDeleted += nbAssetDeleted;
    TF_DEBUG(SBSAR_RENDER)
      .Msg("AssetCache: end of cleaning cache, Asset deleted: %zu, for %zu memory save\n",
           nbAssetDeleted,
           currentSize);
}

} // namespace adobe::usd::sbsar
