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

#include <assetPath/assetPathParser.h>
#include <chrono>
#include <memory>
#include <pxr/base/vt/value.h>
#include <pxr/usd/ar/asset.h>
#include <unordered_map>

namespace SubstanceAir {
class PackageDesc;
}

namespace adobe::usd::sbsar {
struct ParsePathResult;
struct CacheStats;

//! \brief class to store a full render result for a specific graph and parameters.
class RenderResultCache
{
  public:
    void updateLastAccessTime();
    std::chrono::time_point<std::chrono::steady_clock> getLastAccessTime() const;
    std::shared_ptr<PXR_NS::ArAsset> getAsset(const std::string& usage);
    void addAsset(const std::string& usage, const std::shared_ptr<PXR_NS::ArAsset>& asset);
    PXR_NS::VtValue getNumericalValue(const std::string& usage);
    void addNumericalValue(const std::string& usage, const PXR_NS::VtValue& value);

    std::size_t getSize();
    void computeSize();
    std::size_t getAssetCount();

  private:
    //! Key : usage of the asset
    std::unordered_map<std::string, std::shared_ptr<PXR_NS::ArAsset>> m_assets;
    //! Key : usage of the value
    std::unordered_map<std::string, PXR_NS::VtValue> m_numericalValues;
    //! Time of creation of the assets or the last time it was used.
    std::chrono::time_point<std::chrono::steady_clock> m_lastAccessTime;
    //! Total size of all asset in the map in bytes.
    std::size_t m_size;
};

//! \brief Cache to store all assets render by the substance engine.
//! The assets are grouped in renderResult.
//! The cache size is controled by CacheSize. When the cache is full, 10% of the oldest render
//! result are erased.
//! @see RenderResultCache, CacheSize
class AssetCache
{
  public:
    AssetCache() = default;
    ~AssetCache() = default;
    //! Check is a render result for a combo graph + parameters exist in the cache.
    bool hasRenderResult(const ParsePathResult& pathResult);
    //! Return corresponding asset if it exist in the cache, return nullptr otherwise.
    //! Update time creation of the corresponding render result.
    std::shared_ptr<PXR_NS::ArAsset> getAsset(const ParsePathResult& pathResult);
    //! Return corresponding asset if it exist in the cache, return nullptr otherwise.
    //! Update time creation of the corresponding render result.
    PXR_NS::VtValue getNumericalValue(const ParsePathResult& pathResult);
    //! Add a render result to the cache. If the cache is fulle, erase 10% of the oldest render
    //! result.
    void addRenderResult(const ParsePathResult& pathResult, RenderResultCache&& renderResult);
    //! Erase all the cache.
    void clearCache();

  private:
    //! Erase 10% of the cache.
    void cleanCache();
    //! Key: Package hash + graph name + input parameters.
    std::unordered_map<std::string, RenderResultCache> m_assets;
    //! Total size of all asset in the cache in bytes.
    //! @note This value is not totally correct because some assets can be shared between render
    //! result. So the release size can be inferior to this value.
    std::size_t m_size = 0;
};

}
