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
#include <sandbox/hardening/hardening.h>
#include <sandbox/protocol/assetReader.h>
#include <sandbox/protocol/assetWriter.h>
#include <sandbox/protocol/sandboxProtocol.h>
#include <sandbox/resolver/inMemoryWritableAsset.h>
// Defines inMemoryURI
#include <sandbox/utilities/utilities.h>

#include <pxr/base/plug/registry.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
// to remove
#include <pxr/base/arch/env.h>
#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/plug/plugin.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace adobe::usd::sandbox;

#define INVALID_RETURN_CODE -1

/*
 * Release shared memory and flush standard output streams before the sandboxed process exits.
 *
 * protocol: the active sandbox protocol whose shared memory segment should be cleaned up.
 */
void
CleanUpSharedResources(adobe::usd::sandbox::SandboxProtocol& protocol)
{
    protocol.CleanSharedMemory();
    fflush(stdout);
    fflush(stderr);
}

/*
 * Run the import side of the sandbox protocol: open the asset at resolvedPath, serialize it
 * as in-memory USDC together with all referenced textures, and write the result into shared
 * memory for the host process to consume.
 *
 * resolvedPath: absolute path to the source asset to import.
 * protocol: the active sandbox protocol used for IPC and shared memory.
 *
 * Returns true on success; false on any failure.
 */
bool
SandboxedImport(const std::string& resolvedPath, SandboxProtocol& protocol)
{
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Reading fileformat arguments\n");
    std::map<std::string, std::string> fileFormatArgs;
    if (!protocol.ReceiveFileFormatArgs(fileFormatArgs)) {
        TF_WARN("(SANDBOX) Failed to read pipe arguments from host.\n");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Opening layer\n");
    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(resolvedPath, fileFormatArgs);
    if (!layer) {
        TF_WARN("(SANDBOX) Failed to open document: %s\n", resolvedPath.c_str());
        return false;
    }

    if (ArGetResolver().OpenAssetForWrite(ArResolvedPath(inMemoryURI),
                                          ArResolver::WriteMode::Update) == nullptr) {
        TF_WARN("(SANDBOX) Failed to open USD writable asset for writing: %s\n",
                inMemoryURI.c_str());
        return false;
    }

    // TODO: Ensure this returns true. It currently doesn't because InMemoryWritableAsset::Close()
    // always returns false. Changing that currently would cause USD to crash because of how
    // crate files assume memory maps can be used even when there is no actual file. Before the
    // return value of this function can be used, USD must be patched
    layer->Export(inMemoryURI);

    // TODO: While we can't check that Export(...) succeeded, as a temporary workaround we can try
    // to reload the file to confirm that it was created properly. This check should be removed
    // when we can trust the return value of layer->Export() (requires USD fix).
    SdfLayerRefPtr inMemLayer = SdfLayer::FindOrOpen(inMemoryURI);
    if (!inMemLayer) {
        TF_RUNTIME_ERROR("(SANDBOX) Failed to write USD layer as USDC data for in memory asset "
                         "\"%s\". The layer may be malformed.\n",
                         inMemoryURI.c_str());
        return false;
    }

    std::unordered_map<std::string, std::string> assets = FindAssetPaths(layer);
    assets.insert({ inMemoryURI, inMemoryURI });

    AssetMap arAssets = GetArAssets(assets);
    AssetWriter assetWriter(protocol.GetSharedMemory(), arAssets);
    size_t assetsSize = assetWriter.GetSize();
    if (assetsSize == 0) {
        TF_WARN("(SANDBOX) Failed to set assets to write and get size in bytes\n");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Sending assets size\n");
    if (!protocol.SendAssetSize(assetsSize)) {
        TF_WARN("(SANDBOX) Failed to send assets size\n");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Connecting to shared memory\n");
    if (!protocol.ReceiveAndConnectSharedMemory()) {
        TF_WARN("(SANDBOX) Failed to connect to shared memory\n");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(SANDBOX) Found %zu assets in asset layer %s, writing to shared memory\n",
                 assets.size(),
                 layer->GetIdentifier().c_str());

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Writing assets to shared memory\n");
    if (!assetWriter.WriteAssetsToSharedMemory()) {
        TF_WARN("(SANDBOX) Failed to write assets for layer: %s\n", resolvedPath.c_str());
        return false;
    }
    return true;
}

/*
 * Run the export side of the sandbox protocol: read pre-loaded assets from shared memory,
 * register them with the in-memory resolver, and export the USD layer to resolvedPath.
 *
 * resolvedPath: absolute path where the exported asset should be written.
 * protocol: the active sandbox protocol used for IPC and shared memory.
 *
 * Returns true on success; false on any failure.
 */
bool
SandboxedExport(const std::string& resolvedPath, SandboxProtocol& protocol)
{
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Reading fileformat arguments\n");
    std::map<std::string, std::string> fileFormatArgs;
    if (!protocol.ReceiveFileFormatArgs(fileFormatArgs)) {
        TF_WARN("(SANDBOX) Failed to read pipe arguments from host.\n");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Connecting to shared memory\n");
    if (!protocol.ReceiveAndConnectSharedMemory()) {
        TF_WARN("(SANDBOX) Failed to connect to shared memory\n");
        return false;
    }

    AssetReader assetReader(protocol.GetSharedMemory());
    assetReader.ProcessAssetsFromSharedMemory(
      [](const std::string& path, const std::shared_ptr<PXR_NS::ArAsset>& asset) {
          auto writableAsset =
            ArGetResolver().OpenAssetForWrite(ArResolvedPath(path), ArResolver::WriteMode::Replace);
          if (writableAsset) {
              std::vector<char> buffer(asset->GetSize());
              asset->Read(buffer.data(), buffer.size(), 0);
              writableAsset->Write(buffer.data(), buffer.size(), 0);
              writableAsset->Close();
          } else {
              TF_WARN("(SANDBOX) Failed to open asset for writing: %s\n", path.c_str());
          }
      });

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(inMemoryURI);
    if (!layer) {
        TF_WARN("(SANDBOX) Failed to open document: %s\n", inMemoryURI.c_str());
        return false;
    }

    // Propagate a failed export to the host via a non-zero exit (return false).
    if (!layer->Export(resolvedPath, std::string(), fileFormatArgs)) {
        TF_WARN("(SANDBOX) Failed to export asset to %s in the sandboxed process\n",
                resolvedPath.c_str());
        return false;
    }
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Exported layer: %s\n", resolvedPath.c_str());
    return true;
}

// Entry point of the sandboxed process
int
main(int argc, char* argv[])
{
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(SANDBOX) Using %s as plugin path for sandbox server\n",
                 ArchGetEnv("PXR_PLUGINPATH_NAME").c_str());
    if (argc != 6) {
        TF_WARN("(SANDBOX) Incorrect number of arguments.\n");
        TF_WARN("Usage: SandboxedProcess.exe <ResolvedPath> <UnsafePluginPath> "
                "<ToSandboxPipeHandle> <FromSandboxPipeHandle> <IsExport>\n");
        return INVALID_RETURN_CODE;
    }

    std::string resolvedPath = argv[1];
    std::string unsafePluginRoot = argv[2];

    std::string readPipeToSandbox = argv[3];
    std::string writePipeFromSandbox = argv[4];
    SandboxProtocol protocol(readPipeToSandbox, writePipeFromSandbox);

    std::string isExportStr = argv[5];
    std::transform(isExportStr.begin(), isExportStr.end(), isExportStr.begin(), ::tolower);
    bool isExport;
    if (isExportStr == "true") {
        isExport = true;
    } else if (isExportStr == "false") {
        isExport = false;
    } else {
        TF_WARN("(SANDBOX) Invalid 5th argument: %s. Export flag must be true or false\n",
                isExportStr.c_str());
        return INVALID_RETURN_CODE;
    }

    PlugRegistry& registry = PlugRegistry::GetInstance();
    std::filesystem::path pluginRoot(unsafePluginRoot);
    if (!std::filesystem::is_directory(unsafePluginRoot)) {
        TF_WARN("(SANDBOX) Plugin path doesn't exist: %s\n", unsafePluginRoot.c_str());
        return INVALID_RETURN_CODE;
    }

    registry.RegisterPlugins(unsafePluginRoot);
    auto allPlugins = registry.GetAllPlugins();
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(SANDBOX) Registered plugins (from %s):\n",
                 unsafePluginRoot.c_str());
    for (const auto& plugin : allPlugins) {
        TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                     "  - %s at %s\n",
                     plugin->GetName().c_str(),
                     plugin->GetPath().c_str());
    }

    // Apply process restrictions AFTER plugin registration (so registration's syscalls are not
    // blocked) but BEFORE any untrusted document is opened or parsed.
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Applying process restrictions\n");
    if (!adobe::usd::sandbox::hardening::ApplyProcessRestrictions(isExport)) {
        TF_WARN("(SANDBOX): Failed to apply process restrictions.\n");
        CleanUpSharedResources(protocol);
        return INVALID_RETURN_CODE;
    }

    if (isExport) {
        TF_DEBUG_MSG(
          FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Exporting layer: %s\n", resolvedPath.c_str());
        if (!SandboxedExport(resolvedPath, protocol)) {
            CleanUpSharedResources(protocol);
            return INVALID_RETURN_CODE;
        }
    } else {
        TF_DEBUG_MSG(
          FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Importing layer: %s\n", resolvedPath.c_str());
        if (!SandboxedImport(resolvedPath, protocol)) {
            CleanUpSharedResources(protocol);
            return INVALID_RETURN_CODE;
        }
    }
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Conversion complete, cleaning up\n");
    CleanUpSharedResources(protocol);
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Sandboxed process complete\n");
    return 0;
}