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

#include <sandbox/resolver/inMemoryResolver.h>

#include <sandbox/resolver/inMemoryWritableAsset.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/type.h>
#include <pxr/usd/ar/defineResolver.h>
#include <pxr/usd/ar/inMemoryAsset.h>

using namespace PXR_NS;

namespace adobe::usd::sandbox {

AR_DEFINE_RESOLVER(adobe::usd::sandbox::InMemoryResolver, ArResolver);

InMemoryResolver::InMemoryResolver() {}

InMemoryResolver::~InMemoryResolver() {}

bool
InMemoryResolver::SetData(const std::string& uri, const std::vector<char>& data)
{
    if (!TfStringStartsWith(uri, "InMemory://")) {
        TF_WARN("InMemoryResolver: URI '%s' does not start with 'InMemory://'", uri.c_str());
        return false;
    }
    _storage[uri] = data;
    return true;
}

bool
InMemoryResolver::GetData(const std::string& uri, std::vector<char>& outData) const
{
    auto it = _storage.find(uri);
    if (it != _storage.end()) {
        outData = it->second;
        return true;
    }
    return false;
}

void
InMemoryResolver::_BeginCacheScope(VtValue* cacheScopeData)
{}
void
InMemoryResolver::_EndCacheScope(VtValue* cacheScopeData)
{}

ArResolvedPath
InMemoryResolver::_Resolve(const std::string& path) const
{
    if (TfStringStartsWith(path, "InMemory://")) {
        return ArResolvedPath(path);
    }
    return ArResolvedPath();
}

ArResolvedPath
InMemoryResolver::_ResolveForNewAsset(const std::string& assetPath) const
{
    if (TfStringStartsWith(assetPath, "InMemory://")) {
        return ArResolvedPath(assetPath);
    }
    return ArResolvedPath();
}

std::shared_ptr<ArAsset>
InMemoryResolver::_OpenAsset(const PXR_NS::ArResolvedPath& resolvedPath) const
{
    const std::string& uri = resolvedPath.GetPathString();

    if (!TfStringStartsWith(uri, "InMemory://")) {
        TF_WARN("Invalid uri: not handled by this resolver");
        return nullptr;
    }

    auto it = _storage.find(uri);
    if (it == _storage.end()) {
        TF_WARN("InMemoryResolver: Asset not found: %s", uri.c_str());
        return nullptr;
    }

    // TODO: Verify that this is correct or replace it when refactoring how this class stores data
    return ArInMemoryAsset::FromBuffer(
      std::shared_ptr<const char>(it->second.data(), [](const char*) {}), it->second.size());
}

std::shared_ptr<ArWritableAsset>
InMemoryResolver::_OpenAssetForWrite(const ArResolvedPath& resolvedPath, WriteMode writeMode) const
{
    const std::string& uri = resolvedPath.GetPathString();

    if (!TfStringStartsWith(uri, "InMemory://")) {
        TF_WARN("Invalid uri: not handled by this resolver");
        return nullptr;
    }

    auto it = _storage.find(uri);
    if (it == _storage.end()) {
        if (writeMode == WriteMode::Replace || writeMode == WriteMode::Update) {
            // Create a new buffer for writing
            _storage[uri] = std::vector<char>();
            it = _storage.find(uri);
        } else {
            TF_WARN("InMemoryResolver: Unsupported write mode for URI '%s'", uri.c_str());
            return nullptr;
        }
    } else {
        if (writeMode == WriteMode::Replace) {
            // Clear existing data
            it->second.clear();
        }
    }
    return std::make_shared<InMemoryWritableAsset>(it->second);
}

std::string
InMemoryResolver::_CreateIdentifier(const std::string& assetPath,
                                    const ArResolvedPath& anchorAssetPath) const
{
    return assetPath;
}

std::string
InMemoryResolver::_CreateIdentifierForNewAsset(const std::string& assetPath,
                                               const ArResolvedPath& anchorAssetPath) const
{
    return assetPath;
}

bool
InMemoryResolver::_CanWriteAssetToPath(const ArResolvedPath& resolvedPath,
                                       std::string* whyNot) const
{
    const std::string& uri = resolvedPath.GetPathString();
    if (!TfStringStartsWith(uri, "InMemory://")) {
        if (whyNot) {
            *whyNot = "URI scheme not supported by InMemoryResolver.";
        }
        return false;
    }
    return true;
}

}