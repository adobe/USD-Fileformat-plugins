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

#include <pxr/pxr.h>
#include <pxr/usd/ar/packageResolver.h>

// To avoid trouble when registering class we use pixar name space.
PXR_NAMESPACE_OPEN_SCOPE
class ArAsset;

//! \brief This class are in charge of resolving package asset with extension
//! ".sbsar". It then calls the substance engine with the argument encoded in
//! the asset path and creates an ArInMemoryAsset with the extension ".sbsarimage".
//! It will then be presented to renderers through an SbsarImage that provide
//! the interface to read the underlying texture.
class SBSARPackageResolver : public PXR_NS::ArPackageResolver
{
  public:
    SBSARPackageResolver();
    virtual ~SBSARPackageResolver();

    virtual std::string Resolve(const std::string& packagePath,
                                const std::string& packagedPath) override;

    virtual std::shared_ptr<PXR_NS::ArAsset> OpenAsset(const std::string& packagePath,
                                                       const std::string& packagedPath) override;

    std::shared_ptr<PXR_NS::ArAsset> OpenSbsarAsset(const std::string& packagePath,
                                                    const std::string& packagedPath);

    std::shared_ptr<PXR_NS::ArAsset> OpenAssetMtlx(const std::string& packagePath,
                                                   const std::string& packagedPath);

    std::shared_ptr<PXR_NS::ArAsset> OpenThumbnailAsset(const std::string& packagePath,
                                                        const std::string& packagedPath);

    virtual void BeginCacheScope(PXR_NS::VtValue* cacheScopeData) override;

    virtual void EndCacheScope(PXR_NS::VtValue* cacheScopeData) override;
    static std::shared_ptr<PXR_NS::ArAsset> OpenAssetExt(const std::string& packagePath,
                                                         const std::string& packagedPath,
                                                         int compression_level);
    static std::shared_ptr<PXR_NS::ArAsset> OpenAssetExt(const std::string& assetPath,
                                                         int compression_level);
};

PXR_NAMESPACE_CLOSE_SCOPE
