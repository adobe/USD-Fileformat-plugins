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

#pragma once

#include <sandbox/resolver/api.h>
#include <sandbox/utilities/quarantine.h> // kBadAssetScheme (shared with the quarantine helpers)

#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/writableAsset.h>

#include <memory>
#include <string>

namespace adobe::usd::sandbox {

/// Resolver for the `BadAsset://` quarantine scheme. Deliberately inert: `_Resolve` echoes a
/// well-formed `BadAsset://` path back unchanged (giving it a stable, opaque identity) and every
/// open returns null. It never touches the filesystem, the network, or any other resolver, so a
/// quarantined reference cannot be used to reach the bytes it names.
class USDINMEMRESOLVER_API BadAssetResolver : public PXR_NS::ArResolver
{
public:
    BadAssetResolver();
    ~BadAssetResolver() override;

protected:
    PXR_NS::ArResolvedPath _Resolve(const std::string& path) const override;
    PXR_NS::ArResolvedPath _ResolveForNewAsset(const std::string& assetPath) const override;

    std::shared_ptr<PXR_NS::ArAsset> _OpenAsset(
      const PXR_NS::ArResolvedPath& resolvedPath) const override;

    // Quarantined references are never writable; always returns null.
    std::shared_ptr<PXR_NS::ArWritableAsset> _OpenAssetForWrite(
      const PXR_NS::ArResolvedPath& resolvedPath,
      WriteMode writeMode) const override;

    std::string _CreateIdentifier(const std::string& assetPath,
                                  const PXR_NS::ArResolvedPath& anchorAssetPath) const override;

    std::string _CreateIdentifierForNewAsset(
      const std::string& assetPath,
      const PXR_NS::ArResolvedPath& anchorAssetPath) const override;
};

}
