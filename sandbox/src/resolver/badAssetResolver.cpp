/*
Copyright 2026 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#include <sandbox/resolver/badAssetResolver.h>

#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ar/defineResolver.h>

using namespace PXR_NS;

namespace adobe::usd::sandbox {

AR_DEFINE_RESOLVER(adobe::usd::sandbox::BadAssetResolver, ArResolver);

namespace {

bool
hasBadAssetScheme(const std::string& path)
{
    return TfStringStartsWith(path, std::string(kBadAssetScheme));
}

}

BadAssetResolver::BadAssetResolver() = default;

BadAssetResolver::~BadAssetResolver() = default;

ArResolvedPath
BadAssetResolver::_Resolve(const std::string& path) const
{
    // Echo a well-formed quarantine URI back unchanged so it has a stable, opaque identity. Never
    // touch the filesystem or any other resolver.
    if (hasBadAssetScheme(path)) {
        return ArResolvedPath(path);
    }
    return ArResolvedPath();
}

ArResolvedPath
BadAssetResolver::_ResolveForNewAsset(const std::string& assetPath) const
{
    if (hasBadAssetScheme(assetPath)) {
        return ArResolvedPath(assetPath);
    }
    return ArResolvedPath();
}

std::shared_ptr<ArAsset>
BadAssetResolver::_OpenAsset(const ArResolvedPath& /* resolvedPath */) const
{
    // Inert by design: a quarantined reference never yields any bytes.
    return nullptr;
}

std::shared_ptr<ArWritableAsset>
BadAssetResolver::_OpenAssetForWrite(const ArResolvedPath& /* resolvedPath */,
                                     WriteMode /* writeMode */) const
{
    return nullptr;
}

std::string
BadAssetResolver::_CreateIdentifier(const std::string& assetPath,
                                    const ArResolvedPath& /* anchorAssetPath */) const
{
    return assetPath;
}

std::string
BadAssetResolver::_CreateIdentifierForNewAsset(const std::string& assetPath,
                                               const ArResolvedPath& /* anchorAssetPath */) const
{
    return assetPath;
}

}
