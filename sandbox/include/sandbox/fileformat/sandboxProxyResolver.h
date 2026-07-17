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

#include <pxr/usd/ar/packageResolver.h>

namespace adobe::usd::sandbox {

/**
 * The sandbox proxy resolver will be called to open packaged textures that still reference the
 * original asset and texture paths. These textures will have been read from the shared memory
 * (from the sandboxed process) during import and cached, and this resolver will retrieve them.
 */
class SandboxProxyResolver : public PXR_NS::ArPackageResolver
{
public:
    SandboxProxyResolver();

private:
    // TODO: Is this necessary to keep?
    std::string Resolve(const std::string& resolvedPackagePath,
                        const std::string& packagedPath) override;

    /*
     * Open the asset with the given resolved package path and resolved packaged path. The asset
     * will be fetched from the cache if it exists, otherwise a nullptr will be returned.
     *
     * resolvedPackagePath: the path of the asset that was converted.
     * resolvedPackagedPath: the name of the texture referenced from the asset.
     *
     * Returns the asset if it exists in the cache, otherwise a nullptr.
     */
    std::shared_ptr<PXR_NS::ArAsset> OpenAsset(const std::string& resolvedPackagePath,
                                               const std::string& resolvedPackagedPath) override;

    // TODO: Implement if necessary
    void BeginCacheScope(PXR_NS::VtValue* data) override;
    void EndCacheScope(PXR_NS::VtValue* data) override;
};

}
