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
#include <api.h>
#include <memory>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE
class ArAsset;
PXR_NAMESPACE_CLOSE_SCOPE
namespace adobe::usd::sbsar {

//! \brief Resolve a request coming from the USD asset system: render a sbsar texture with the
//! substance engine and return the corresponding ArAsset.
//! \param packagePath  The complete path to the package the
//! should be opened \param packagedPath A complexe string generate by generateSbsarInfoPath() that
//! containt all information to run a rendering with the substance engine. \see
//! generateSbsarInfoPath()
USDSBSAR_API std::shared_ptr<PXR_NS::ArAsset>
renderSbsarAsset(const std::string& packagePath, const std::string& packagedPath);

//! \brief Resolve a request coming from the USD asset system: render a sbsar output value with the
//! substance engine and return the corresponding VtValue.
//! \param packagePath  The complete path to the package the should be opened
//! \param packagedPath A complexe string generate by generateSbsarInfoPath() that containt all
//! information to run a rendering with the substance engine.
//! \see generateSbsarInfoPath()
PXR_NS::VtValue
renderSbsarValue(const std::string& packagePath, const std::string& packagedPath);

//! \brief Stored in the singleton, control the size of different cache.
class USDSBSAR_API CacheSize
{
  public:
    CacheSize();
    std::size_t getMaxAssetCacheSize() const;
    std::size_t getMaxInputImageCacheSize() const;
    std::size_t getMaxPackageCacheSize() const;
    void setMaxAssetCacheSize(std::size_t size = 1'000'000'000);
    void setMaxInputImageCacheSize(std::size_t size = 1'000'000'000);
    void setMaxPackageCacheSize(std::size_t size = 10);

  private:
    std::size_t m_maxAssetCacheSize;      //! In bytes
    std::size_t m_maxInputImageCacheSize; //! In bytes
    std::size_t m_maxPackageCacheSize;    //! Max number of packages
};

USDSBSAR_API CacheSize&
getCacheSize();

//! \brief Store in the singleton, used to test the cache system.
struct USDSBSAR_API CacheStats
{
    std::size_t renderingCall = 0;
    std::size_t resultFoundInCache = 0;
    std::size_t valueFoundInCache = 0;
    std::size_t graphInstanceCreated = 0;
    std::size_t graphInstanceDeleted = 0;
    std::size_t packageCreated = 0;
    std::size_t packageDeleted = 0;
    std::size_t assetCreated = 0;
    std::size_t assetDeleted = 0;
    std::size_t inputImageCreated = 0;
    std::size_t inputImageDeleted = 0;
    std::size_t requestSend = 0;
};

USDSBSAR_API CacheStats&
getCacheStats();

USDSBSAR_API void
clearCache();
}
