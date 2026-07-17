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

#include <sandbox/debugCodes.h>
#include <sandbox/utilities/utilities.h>

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/sdf/layer.h>

#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/type.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/propertySpec.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <string>

namespace adobe::usd::sandbox {

PXR_NAMESPACE_USING_DIRECTIVE;

bool
ConsumeBoolArg(std::map<std::string, std::string>& args, const std::string& key)
{
    auto it = args.find(key);
    if (it == args.end()) {
        return false;
    }
    bool value = (it->second == "true");
    args.erase(it);
    return value;
}

std::filesystem::path
getExecutableDirectory()
{
    std::filesystem::path currentPath = pxr::ArchGetExecutablePath();
    return currentPath.parent_path();
}

std::string
NormalizePath(const std::string& path)
{
    std::string normalizedPath = path;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    return normalizedPath;
}

std::unordered_map<std::string, std::string>
FindAssetPaths(SdfLayerRefPtr layer)
{
    return FindAndModifyAssetPaths(layer, [](const std::string&, std::string&) { return false; });
}

std::unordered_map<std::string, std::string>
FindAndModifyAssetPaths(
  SdfLayerRefPtr layerToModify,
  const std::function<bool(const std::string&, std::string&)>& createNewAssetName)
{
    return FindAndModifyAssetPaths(layerToModify, createNewAssetName, *layerToModify);
}

std::unordered_map<std::string, std::string>
FindAndModifyAssetPaths(
  SdfLayerRefPtr layerToModify,
  const std::function<bool(const std::string&, std::string&)>& createNewAssetName,
  const SdfLayer& layerForResolvingAssets)
{
    // First element is the authored asset path for referencing the texture, while the second is
    // the resolved asset path for finding it on disk. They may be the same
    std::unordered_map<std::string, std::string> assets;

    layerToModify->Traverse(SdfPath::AbsoluteRootPath(), [&](const SdfPath& path) {
        if (!path.IsPropertyPath()) {
            return;
        }
        SdfPropertySpecHandle prop = layerToModify->GetPropertyAtPath(path);
        if (!prop || prop->GetTypeName() != SdfValueTypeNames->Asset) {
            return;
        }

        // A single asset value may appear both in the property's default and in one or more of its
        // time samples (e.g. a constant animated attribute). Cache the createNewAssetName decision
        // per distinct authored value so the callback fires once per value, while still rewriting
        // every occurrence with the (deterministic) new name.
        std::unordered_map<std::string, std::pair<bool, std::string>> decisionByAuthoredPath;

        // Extract the asset path from one authored value, run it past createNewAssetName, record it
        // in the result map, and (if the callback asked) write the new name back via setValue.
        auto handleValue = [&](const VtValue& value,
                               const std::function<void(const SdfAssetPath&)>& setValue) {
            if (value.IsEmpty() || !value.IsHolding<SdfAssetPath>()) {
                return;
            }
            std::string assetPath = value.UncheckedGet<SdfAssetPath>().GetAssetPath();
            if (assetPath.empty()) {
                return;
            }

            // Turn a (potentially) relative asset path into an absolute resolved path. Use the
            // source layer so the path is relative to the location of the original asset
            std::string normalizedPath = NormalizePath(assetPath);
            std::string resolvedAssetPath =
              layerForResolvingAssets.ComputeAbsolutePath(normalizedPath);

            auto [it, inserted] =
              decisionByAuthoredPath.try_emplace(assetPath, false, std::string());
            if (inserted) {
                it->second.first = createNewAssetName(assetPath, it->second.second);
            }
            const auto& [shouldRewrite, newAssetName] = it->second;

            std::string effectivePath = assetPath;
            if (shouldRewrite) {
                setValue(SdfAssetPath(newAssetName));
                effectivePath = newAssetName;
            }

            // Multiple nodes may refer to the same asset, so we have to ensure it's only added once
            if (assets.find(effectivePath) == assets.end()) {
                assets.insert({ effectivePath, resolvedAssetPath });
            }
        };

        // Default value.
        handleValue(prop->GetDefaultValue(),
                    [&](const SdfAssetPath& v) { prop->SetDefaultValue(VtValue(v)); });

        // Time-sampled values: a worker can hide a reference in an animated asset attribute, which
        // a default-only scrub would miss. Scoped to asset-typed attributes (the type check above),
        // so non-asset and non-animated attributes pay nothing here.
        for (double time : layerToModify->ListTimeSamplesForPath(path)) {
            VtValue sampleValue;
            if (layerToModify->QueryTimeSample(path, time, &sampleValue)) {
                handleValue(sampleValue, [&](const SdfAssetPath& v) {
                    layerToModify->SetTimeSample(path, time, VtValue(v));
                });
            }
        }
    });
    return assets;
}

std::unordered_map<std::string, std::shared_ptr<PXR_NS::ArAsset>>
GetArAssets(const std::unordered_map<std::string, std::string>& assets)
{
    // Pair of asset path and asset data
    std::unordered_map<std::string, std::shared_ptr<PXR_NS::ArAsset>> arAssets;
    arAssets.reserve(assets.size());

    for (const auto& [authoredPath, resolvedPath] : assets) {
        if (authoredPath == resolvedPath) {
            TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "Found asset: %s\n", authoredPath.c_str());
        } else {
            TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                         "Found asset: %s (full path: %s)\n",
                         authoredPath.c_str(),
                         resolvedPath.c_str());
        }

        ArResolvedPath resolvedAssetPath = ArResolvedPath(resolvedPath);
        std::shared_ptr<ArAsset> assetData = ArGetResolver().OpenAsset(resolvedAssetPath);

        if (assetData) {
            arAssets[authoredPath] = assetData;
        } else {
            TF_WARN("Could not resolve asset for path %s\n", resolvedPath.c_str());
        }
    }
    return arAssets;
}

std::string
BuildNewPluginPath(const std::string& currentPxrPluginPath,
                   const std::string& proxyPluginPath,
                   const std::string& unsafePluginRoot)
{
#if SANDBOX_IS_WINDOWS
    const std::string separator = ";";
#else
    const std::string separator = ":";
#endif

    if (currentPxrPluginPath.empty()) {
        return unsafePluginRoot;
    }

    // The current plugin couldn't be located, so it cannot be properly filtered out of
    // PXR_PLUGINPATH_NAME. For that reason, to ensure the sandbox doesn't find the proxy plugin,
    // the environment variable must be completely cleared (and replaced with the sandboxed plugin
    // location).
    if (proxyPluginPath.empty()) {
        TF_WARN("(HOST) No proxy plugin location found, PXR_PLUGINPATH_NAME environment variable "
                "cannot be filtered. The sandboxed process will be launched with a "
                "PXR_PLUGINPATH_NAME completely cleared and reset.");
        return unsafePluginRoot;
    }

    std::error_code ec;
    const std::filesystem::path canonicalProxyPath =
      std::filesystem::weakly_canonical(proxyPluginPath, ec);
    if (ec) {
        TF_WARN("(HOST) Could not canonicalize proxy plugin path '%s': %s. "
                "PXR_PLUGINPATH_NAME cannot be filtered. The sandboxed process will be launched "
                "with a PXR_PLUGINPATH_NAME completely cleared and reset.",
                proxyPluginPath.c_str(),
                ec.message().c_str());
        return unsafePluginRoot;
    }

    std::vector<std::string> paths = TfStringSplit(currentPxrPluginPath, separator);
    bool found = false;
    for (auto& p : paths) {
        std::error_code entryEc;
        // Error codes occur rarely and in unusual circumstances (non-existant paths are handled
        // properly). If one occurs, it likely isn't the path used for finding the proxy plugin.
        if (std::filesystem::weakly_canonical(p, entryEc) == canonicalProxyPath && !entryEc) {
            p = unsafePluginRoot;
            found = true;
        }
    }

    if (!found) {
        paths.insert(paths.begin(), unsafePluginRoot);
    }

    return TfStringJoin(paths, separator.c_str());
}

}
