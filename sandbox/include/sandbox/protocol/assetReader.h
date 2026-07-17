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
#include <sandbox/protocol/assetSerializerUtil.h>

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/inMemoryAsset.h>

#include <functional>
#include <string>

// Forward declaration — full definition is in <ipc/sharedMemory.h>, included only by the .cpp.
namespace adobe::usd::ipc {
class SharedMemory;
}

namespace adobe::usd::sandbox {

/**
 * The AssetReader allows for reading ArAssets from shared memory. It will deserialize the assets
 * in one process after they have been written to shared memory by a separate AssetWriter in
 * another process.
 */
class USDSANDBOX_API AssetReader
{
public:
    /**
     * Constructor for the AssetReader.
     *
     * @param shm The SharedMemory region to read from. Must have been created/connected and must
     *            remain valid for the lifetime of the AssetReader.
     */
    AssetReader(ipc::SharedMemory& shm);

    /**
     * Read an asset with a given path from the binary data
     *
     * @param path The path of the asset to read
     *
     * @return The ArAsset for the asset, or nullptr if the asset was not found
     */
    std::shared_ptr<PXR_NS::ArAsset> ReadAssetFromSharedMemory(const std::string& path);

    /**
     * Extract all assets from the shared memory and process them with the given callback.
     *
     * @param processAssetCallback A callback function that will be called for each asset. The
     *                             callback will be called with the path of the asset and the asset
     *                             itself.
     *
     * @return True if the assets were extracted and processed successfully, false otherwise
     */
    bool ProcessAssetsFromSharedMemory(
      std::function<void(const std::string& path, const std::shared_ptr<PXR_NS::ArAsset>& asset)>
        processAssetCallback);

private:
    // SharedMemory region provided at construction time. Always valid.
    ipc::SharedMemory& _shm;

    // Store the mapping of asset paths to their offsets in shared memory. The table is read from
    // the shared memory and cached for easy access.
    AssetTableOfContents _tableOfContents;

    /*
     * Read the table of contents into the assetsOffsetsCache, so that an asset's location in
     * shared memory can be found and stored with the asset's path. Then, when reading an asset,
     * its location can simply be looked up in the cache and read directly.
     * The table of contents is formatted as follows:
     * 1. Number of assets in table (size_t)
     * 2. For each asset:
     *    a. Path string (size_t + string)
     *    b. Asset size (size_t)
     *    c. Asset offset in shared memory (size_t)
     *
     * Note: This function assumes that the table of contents has been written to shared memory
     * starting from the beginning of the memory block! It will start reading with an offset of 0!
     *
     * Returns true if the table of contents was read successfully, false otherwise.
     */
    bool _ReadTableOfContentsIntoCache();

    /*
     * Read the size and offset of an asset from the binary data. This helper function allows for
     * reading an asset from shared memory into a buffer, or into a stream.
     *
     * path: the path of the asset to read
     * assetSize: a variable that will be set to the size of the asset.
     * assetOffset: a variable that will be set to the offset of the asset in the binary data.
     *
     * Returns true if the size and offset were read successfully, false otherwise
     */
    bool _ReadAssetSizeAndOffset(const std::string& path, size_t& assetSize, size_t& assetOffset);

    /*
     * These functions are used to read data from the shared memory buffer.
     *
     * The first parameter(s) is the value(s) to be read.
     *
     * The last parameter is the number of bytes read so far, or in other words, the offset of the
     * next byte to be read. It will be modified by the function to reflect the new offset.
     *
     * The return value is true if the data was read successfully, false otherwise.
     */

    bool _ReadSizeType(SandboxSizeType& value, size_t& bytesRead);
    bool _ReadAndResizeString(std::string& string, size_t& bytesRead);
    bool _ReadBuffer(size_t& size, void* buffer, size_t& bytesRead);

    // Helper: reads from _shm at the given offset into buffer.
    bool _ReadRaw(size_t offset, void* buffer, size_t size);
};

} // namespace adobe::usd::sandbox
