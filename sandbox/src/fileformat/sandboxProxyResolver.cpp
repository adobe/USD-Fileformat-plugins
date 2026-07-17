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

#include <sandbox/fileformat/sandboxProxyResolver.h>

#include <sandbox/debugCodes.h>
#include <sandbox/utilities/sandboxAssetCache.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/usd/ar/definePackageResolver.h>
#include <pxr/usd/ar/packageUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE;
namespace adobe::usd::sandbox {

AR_DEFINE_PACKAGE_RESOLVER(adobe::usd::sandbox::SandboxProxyResolver, ArPackageResolver);

SandboxProxyResolver::SandboxProxyResolver() {}

std::string
SandboxProxyResolver::Resolve(const std::string& resolvedPackagePath,
                              const std::string& packagedPath)
{
    std::string joinedAssetPath = ArJoinPackageRelativePath(resolvedPackagePath, packagedPath);
    SandboxAssetCache& sandboxAssetCache = SandboxAssetCache::GetInstance();
    std::shared_ptr<ArAsset> asset = sandboxAssetCache.FindCachedAsset(joinedAssetPath);
    // Use the joined path to verify if the asset exists. If it does, only the packagedPath is
    // needed. If it doesn't exist, return the empty string
    return asset ? packagedPath : "";
}

std::shared_ptr<PXR_NS::ArAsset>
SandboxProxyResolver::OpenAsset(const std::string& resolvedPackagePath,
                                const std::string& resolvedPackagedPath)
{
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "OpenAsset: %s %s\n",
                 resolvedPackagePath.c_str(),
                 resolvedPackagedPath.c_str());
    std::string assetPath = ArJoinPackageRelativePath(resolvedPackagePath, resolvedPackagedPath);
    SandboxAssetCache& sandboxAssetCache = SandboxAssetCache::GetInstance();
    std::shared_ptr<ArAsset> asset = sandboxAssetCache.FindCachedAsset(assetPath);
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "SandboxProxyResolver::OpenAsset: %s asset in cache for uri: %s\n",
                 asset ? "Successfully found" : "ERROR: Failed to find",
                 assetPath.c_str());
    return asset;
}

void
SandboxProxyResolver::BeginCacheScope(PXR_NS::VtValue* data)
{
    // TODO: Add cache functionality here
}

void
SandboxProxyResolver::EndCacheScope(PXR_NS::VtValue* data)
{
    // TODO: Add cache functionality here
}
}
