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

#include <sandbox/api.h>
#include <sandbox/platformConfig.h>

// Forward declarations for SdfLayer types — avoids pulling sdf/layer.h (and its boost/python
// transitive dependency) into every consumer that only needs lightweight utilities.
#include <pxr/usd/sdf/declareHandles.h>

// Forward-declare ArAsset so that GetArAssets() can reference std::shared_ptr<PXR_NS::ArAsset>
// without requiring ar/asset.h in this header.
PXR_NAMESPACE_OPEN_SCOPE
class ArAsset;
PXR_NAMESPACE_CLOSE_SCOPE

#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// TODO: Capitalize functions, add comments, etc...

namespace adobe::usd::sandbox {

/// The URI used to identify the in-memory USD data.
inline const std::string inMemoryURI = "InMemory://myfile.usdc";

/// Get the directory that the running executable is in
USDSANDBOX_API
std::filesystem::path
getExecutableDirectory();

/**
 * Read a boolean file format argument and remove it from the map. Consumed (host-only) arguments
 * are erased so they are not forwarded to the sandboxed worker. Only the exact value "true" yields
 * true; any other value, or an absent key, yields false.
 *
 * @param args The file format arguments map, modified in place (the key is erased if present).
 * @param key The argument key to consume.
 *
 * @return The parsed boolean value.
 */
USDSANDBOX_API bool
ConsumeBoolArg(std::map<std::string, std::string>& args, const std::string& key);

// Template function declaration and definition
template<typename T>
std::string
toString(const T& value)
{
    // TODO: to lower overhead, replace with std::to_string()
    std::stringstream ss;
    ss << value;
    return ss.str();
}

/**
 * Normalize a path to use forward slashes. All backslashes are replaced with forward slashes.
 *
 * @param path The path to normalize
 *
 * @return The normalized path
 */
std::string
NormalizePath(const std::string& path);

/**
 * Find all asset properties on a SdfLayer containing packaged paths
 *
 * Both the default value and every time-sampled (animated) value of each asset-typed property are
 * visited, so a reference hidden in an animated asset attribute is not missed.
 *
 * The resulting map will contain both the authored asset path and the resolved asset path. The
 * former is used for referencing the texture and is the string as found in the USD data. The
 * latter may be processed to be a more accurate path for finding the actual asset, becoming an
 * absolute path with normalized forward slashes. This will be the case if, for instance, the
 * original path referenced in the USDC is authored with Windows backslashes, but the current
 * device is Mac or Linux and wouldn't be able to read that path.
 *
 * @param layer The layer to find asset properties on
 *
 * @return An unordered map of asset paths found on the layer
 */
USDSANDBOX_API
std::unordered_map<std::string, std::string>
FindAssetPaths(PXR_NS::SdfLayerRefPtr layer);

/**
 * Find all asset properties on a SdfLayer containing packaged paths and modify them in the USDC
 * data based on the given createNewAssetName function.
 *
 * The resulting map will contain both the authored asset path and the resolved asset path. The
 * former is used for referencing the texture and is the string as found in the USD data. The
 * latter may be processed to be a more accurate path for finding the actual asset, becoming an
 * absolute path with normalized forward slashes. This will be the case if, for instance, the
 * original path referenced in the USDC is authored with Windows backslashes, but the current
 * device is Mac or Linux and wouldn't be able to read that path.
 *
 * @param layerToModify The layer to find and modify asset properties on.
 *
 * @param createNewAssetName A function that calculates a new asset name based on the old one, and
 *                           returns whether that new name should be set in the USD data
 *
 * bool createNewAssetName(const std::string& authoredPath, std::string& newName):
 *   - The first parameter, authoredPath, is the authored path to the referenced asset.
 *   - The second, newName, is a reference to a string that should be updated to the new name for
 *     the asset in the USD data.
 * The function returns a boolean that indicates whether the USD data should be updated with
 * newName, the second parameter (passed by reference).
 *
 * @return An unordered map of asset paths found on the layer. If the USD data is changed by
 *         createNewAssetName(), the first element of each map entry will be altered accordingly.
 *         The second element will be calculated with getResolvedAssetPath(). These elements will
 *         usually be the same.
 */
USDSANDBOX_API
std::unordered_map<std::string, std::string>
FindAndModifyAssetPaths(
  PXR_NS::SdfLayerRefPtr layerToModify,
  const std::function<bool(const std::string&, std::string&)>& createNewAssetName);

/**
 * For a function overview and descriptions of the first two parameters and return value, see
 * documentation for
 * std::unordered_map<std::string, std::string>
 * FindAndModifyAssetPaths(
 *   PXR_NS::SdfLayerRefPtr layerToModify,
 *   std::function<bool(const std::string&, std::string&)> createNewAssetName
 * )
 *
 * Normally, layerToModify is used to construct the resolved, absolute paths required to find the
 * referenced assets. In some scenarios, though, a new anonymous layer must be created to be
 * modified if the original is const. In these cases, it may not be able to be used to properly
 * resolve referenced assets, since it doesn't have the context of an existing layer (such as a
 * location on disk that referenced assets are relative to).
 *
 * In this scenario, assets must be resolved relative to a separate layer. This function provides
 * an additional SdfLayer parameter which will be used to generate a resolved asset path instead
 * of the original layerToModify.
 *
 * This resolved asset path will be the second entry in each pair in the returned map, and is used
 * for finding the referenced asset.
 *
 * @param layerForResolvingAssets The SdfLayer used for resolving referenced asset paths. This is
 * not an SdfLayerRefPtr but rather an SdfLayer, so that this function can be used within
 * SdfFileFormat::Read(), which provides an SdfLayer.
 *
 */
USDSANDBOX_API
std::unordered_map<std::string, std::string>
FindAndModifyAssetPaths(
  PXR_NS::SdfLayerRefPtr layerToModify,
  const std::function<bool(const std::string&, std::string&)>& createNewAssetName,
  const PXR_NS::SdfLayer& layerForResolvingAssets);

/**
 * Resolve the given asset paths into ArAssets that can be written to shared memory
 *
 * @param assets The (unordered) map of asset paths to write to the shared memory. The asset data
 *               will be loaded using the file format resolver with the given path. The first path
 *               in each pair should be the string as authored in the USD data, whereas the second
 *               should be a normalized absolute path where the asset can be found.
 *
 * @return A map of entries, each containing the asset's authored path and the resolved ArAsset
 *         pointer
 */
USDSANDBOX_API
std::unordered_map<std::string, std::shared_ptr<PXR_NS::ArAsset>>
GetArAssets(const std::unordered_map<std::string, std::string>& assets);

/**
 * Builds a new PXR_PLUGINPATH_NAME value by replacing the proxy plugin's path entry with the
 * unsafe plugin root, preserving all other entries in the path list. This ensures the sandboxed
 * process won't discover the proxy plugin (causing recursion or a crash) while still finding the
 * actual sandboxed plugins and any other USD plugins.
 *
 * If proxyPluginPath is not found in the current path list, unsafePluginRoot is prepended so
 * the sandboxed plugins are still discoverable.
 *
 * If proxyPluginPath is empty or cannot be canonicalized, the entire path list cannot be safely
 * filtered since the proxy location is unknown. Instead, unsafePluginRoot is returned alone
 * (clearing all other entries).
 *
 * @param currentPxrPluginPath The current value of PXR_PLUGINPATH_NAME
 * @param proxyPluginPath      The path entry to remove (the proxy plugin directory)
 * @param unsafePluginRoot     The replacement path (the sandboxed plugins directory)
 *
 * @return A new string to be used for PXR_PLUGINPATH_NAME
 */
USDSANDBOX_API
std::string
BuildNewPluginPath(const std::string& currentPxrPluginPath,
                   const std::string& proxyPluginPath,
                   const std::string& unsafePluginRoot);
}
