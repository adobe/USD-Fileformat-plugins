/*
Copyright 2023 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#pragma once
#include "api.h"
#include "pxr/usd/ar/packageResolver.h"
#include "usdData.h"

namespace adobe::usd {

/// \ingroup utils_materials
/// \brief Custom package resolver to read in image data in an imported USD file.
///
/// The process is as follows:
/// 1) During import (file format -> usd), a SdfFileFormat plugin should compute and store asset
///    paths in the generated USD.
/// 2) During compositing, this package resolver reads the source file images, caches them, and
///    matches them to asset paths as needed. The exact mechanism on how to fill this image cache
///    is delegated to the 'readCache' function, specific to each SdfFileFormat plugin from (1).
///
class USDFFUTILS_API Resolver : public PXR_NS::ArPackageResolver
{
  public:
    Resolver(const std::string& name);
    ~Resolver();

    virtual std::string Resolve(const std::string& resolvedPackagePath,
                                const std::string& packagedPath) override;

    virtual std::shared_ptr<PXR_NS::ArAsset> OpenAsset(
      const std::string& resolvedPackagePath,
      const std::string& resolvedPackagedPath) override;

    virtual void BeginCacheScope(PXR_NS::VtValue* data) override;

    virtual void EndCacheScope(PXR_NS::VtValue* data) override;

    // remove cache entry associated with resolvedPackagePath
    static void clearCache(const std::string& resolvedPackagePath);

    // add image assets to cache for resolvedPackagePath
    static void populateCache(const std::string& resolvedPackagePath,
                              std::vector<ImageAsset>&& images);

  protected:
    virtual void readCache(const std::string& resolvedPackagePath,
                           std::vector<ImageAsset>& images) = 0;

  private:
    // Name of resolver
    std::string mName;
};

}