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

#include <string>

// Forward declaration — full definition is in <ipc/sharedMemory.h>, included only by the .cpp.
namespace adobe::usd::ipc {
class SharedMemory;
}

namespace adobe::usd::sandbox {

/**
 * The AssetWriter allows for writing ArAssets to shared memory. It will serialize the assets in
 * one process so they can be read by a separate AssetReader in another process with access to the
 * shared memory.
 */
class USDSANDBOX_API AssetWriter
{
public:
    /**
     * Constructor for the AssetWriter.
     *
     * @param shm The SharedMemory region to write to. Must have been created/connected and must
     *            remain valid for the lifetime of the AssetWriter.
     * @param arAssets The assets to write to the shared memory. This should be a map of entries,
     *                 each containing a path to reference the asset by, and the ArAsset pointer
     *                 extracted from the USD layer.
     */
    AssetWriter(ipc::SharedMemory& shm, const AssetMap& arAssets);

    /**
     * Get the total size in bytes needed for storing the assets and the table of contents in
     * the shared memory. The shared memory should be sized to at least this before writing.
     *
     * @return The total size in bytes needed for storing the assets and the table of contents in
     *         the shared memory.
     */
    size_t GetSize() const;

    /**
     * Write assets to the binary data.
     *
     * This function assumes that there is the required space in the shared memory for the assets
     * and the table of contents. GetSize returns the total size in bytes required, so shared memory
     * should be resized to at least this size.
     *
     * @return True if the assets were written successfully, false otherwise
     */
    bool WriteAssetsToSharedMemory();

private:
    // SharedMemory region provided at construction time. Always valid.
    ipc::SharedMemory& _shm;

    // Store the mapping of asset paths to their offsets in shared memory. The table is constructed
    // and stored here before being written to the shared memory.
    AssetTableOfContents _tableOfContents;

    // Store the assets to write to the shared memory.
    AssetMap _assetsToWrite;

    // The total size in bytes needed for storing the table of contents and the assets. Set in
    // _SetupTableOfContents(). Shared memory must be at least this size before writing.
    size_t _requiredSize = 0;

    /*
     * Construct a table of contents that maps asset paths to their offsets in shared memory.
     * The table of contents is formatted as follows:
     * 1. Number of assets in table (size_t)
     * 2. For each asset:
     *    a. Path string (size_t + string)
     *    b. Asset size (size_t)
     *    c. Asset offset in shared memory (size_t)
     *
     * This function does not write the table of contents to the shared memory, but rather sets it
     * up internally so it can be easily written in the future. This is because the shared memory
     * may need to be resized based on the size of the table of contents and the assets, before it
     * can be written.
     *
     * Requires: _assetsToWrite has already been set
     *
     * This function will set the total size needed for storing the table of contents and the
     * assets.
     */
    void _SetupTableOfContents();

    /*
     * Write the pre-calculated table of contents to the binary data.
     *
     * Requires: _tableOfContents has already been set with the _SetupTableOfContents function
     *
     * bytesWritten: the number of bytes written so far. This will be modified to reflect the
     *               new offset after the table of contents is written. If no data has been
     *               written yet, this should be 0.
     *
     * Returns true if the table of contents was written successfully, false otherwise.
     */
    bool _WriteTableOfContents(size_t& bytesWritten);

    /*
     * These functions are used to write data to the shared memory buffer.
     *
     * The first parameter(s) is/are the value(s) to be written.
     *
     * The last parameter is the number of bytes written so far, or in other words, the offset of
     * the next byte to be written. It will be modified by the function to reflect the new offset.
     *
     * If these functions are updated, then the following Increment functions should be updated as
     * well. They help calculate the size of data that will be written, without actually writing to
     * shared memory. This is needed for calculating the size of the table of contents.
     *
     * The return value is true if the data was written successfully, false otherwise.
     */

    bool _WriteSizeType(SandboxSizeType value, size_t& bytesWritten);
    bool _WriteSizeAndString(const std::string& string, size_t& bytesWritten);
    // Warning: this function does not write the size of the buffer, it must be tracked separately
    bool _WriteBuffer(size_t size, const void* buffer, size_t& bytesWritten);

    // Helper: writes buffer to _shm at the given offset.
    bool _WriteRaw(const void* buffer, size_t size, size_t offset);

    // Helper functions for writing the table of contents. These are used to calculate how many
    // bytes would be written by the corresponding write function
    static void _IncrementBySizeType(size_t& bytesWritten);
    static void _IncrementBySizeAndString(const std::string& string, size_t& bytesWritten);
};

} // namespace adobe::usd::sandbox
