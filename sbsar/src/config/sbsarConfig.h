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

#include <config/sbsarConfigRegistry.h>

#include "pxr/base/tf/declarePtrs.h"
#include <pxr/base/tf/refBase.h>
#include <pxr/base/tf/weakBase.h>
#include <pxr/pxr.h>

#include <pxr/base/tf/staticData.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_WEAK_AND_REF_PTRS(SbsarConfig);

class SbsarConfig
  : public TfRefBase
  , public TfWeakBase
{
  public:
    SbsarConfig();
    virtual ~SbsarConfig();

    SbsarConfig(const SbsarConfig&) = delete;
    SbsarConfig& operator=(const SbsarConfig&) = delete;

    USDSBSAR_API void init();
    USDSBSAR_API void setAssetCacheSize(std::size_t size);
    USDSBSAR_API void setInputImageCacheSize(std::size_t size);
    USDSBSAR_API void setPackageCacheSize(std::size_t size);
    USDSBSAR_API std::size_t getAssetCacheSize() const;
    USDSBSAR_API std::size_t getInputImageCacheSize() const;
    USDSBSAR_API std::size_t getPackageCacheSize() const;

  private:
    std::atomic<std::size_t> m_assetCacheSize;      //! In bytes
    std::atomic<std::size_t> m_inputImageCacheSize; //! In bytes
    std::atomic<std::size_t> m_packageCacheSize;    //! Max number of packages
};

USDSBSAR_API SbsarConfigRefPtr
getSbsarConfig();

PXR_NAMESPACE_CLOSE_SCOPE
