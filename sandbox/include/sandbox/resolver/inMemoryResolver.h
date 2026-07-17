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

#include <sandbox/resolver/api.h>

#include <pxr/base/tf/registryManager.h>
#include <pxr/usd/ar/api.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/writableAsset.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace adobe::usd::sandbox {

class InMemoryWritableAsset;

class USDINMEMRESOLVER_API InMemoryResolver : public PXR_NS::ArResolver
{
public:
    InMemoryResolver();
    virtual ~InMemoryResolver();

    // Methods to manage in-memory data
    bool SetData(const std::string& uri, const std::vector<char>& data);
    bool GetData(const std::string& uri, std::vector<char>& outData) const;

protected:
    // Override cache scope methods (no-op for in-memory)
    void _BeginCacheScope(PXR_NS::VtValue* cacheScopeData) override;
    void _EndCacheScope(PXR_NS::VtValue* cacheScopeData) override;

    // Override Resolve methods
    PXR_NS::ArResolvedPath _Resolve(const std::string& path) const override;
    PXR_NS::ArResolvedPath _ResolveForNewAsset(const std::string& assetPath) const override;

    // Override OpenAsset method for read operations
    std::shared_ptr<PXR_NS::ArAsset> _OpenAsset(
      const PXR_NS::ArResolvedPath& resolvedPath) const override;

    // Override OpenAssetForWrite method for write operations
    std::shared_ptr<PXR_NS::ArWritableAsset> _OpenAssetForWrite(
      const PXR_NS::ArResolvedPath& resolvedPath,
      WriteMode writeMode) const override;

    std::string _CreateIdentifier(const std::string& assetPath,
                                  const PXR_NS::ArResolvedPath& anchorAssetPath) const override;

    std::string _CreateIdentifierForNewAsset(
      const std::string& assetPath,
      const PXR_NS::ArResolvedPath& anchorAssetPath) const override;

    bool _CanWriteAssetToPath(const PXR_NS::ArResolvedPath& resolvedPath,
                              std::string* whyNot) const override;

private:
    mutable std::unordered_map<std::string, std::vector<char>> _storage;
};
}
