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

#include <sandbox/protocol/assetWriter.h>

#include <sandbox/debugCodes.h>
#include <sandbox/resolver/inMemoryWritableAsset.h>

#include <ipc/sharedMemory.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/fileUtils.h>

namespace adobe::usd::sandbox {

using namespace PXR_NS;

AssetWriter::AssetWriter(ipc::SharedMemory& shm, const AssetMap& arAssets)
  : _shm(shm)
  , _assetsToWrite(arAssets)
{
    _SetupTableOfContents();
}

size_t
AssetWriter::GetSize() const
{
    // Return the total size needed for storing the table of contents and the assets
    return _requiredSize;
}

bool
AssetWriter::WriteAssetsToSharedMemory()
{
    size_t bytesWritten = 0;

    if (!_WriteTableOfContents(bytesWritten)) {
        TF_WARN("ERROR: Failed to write table of contents to shared memory");
        return false;
    }

    // Write assets

    for (const auto& [path, asset] : _assetsToWrite) {

        // This check is not necessary for the function to work, but is useful for verifying that
        // the assets are at the correct offsets. Otherwise, there will be issues if an asset is
        // not where the table of contents says it should be
        if (_tableOfContents.assetPathsAndInfo[path].offset != bytesWritten) {
            TF_CODING_ERROR(
              "Asset \"%s\" is at the wrong offset (expected=%lu, actual=%zu)! The asset is not "
              "being written the the location specified in the table of contents!",
              path.c_str(),
              _tableOfContents.assetPathsAndInfo[path].offset,
              bytesWritten);
            return false;
        }

        // Write the asset data to shared memory. The path and size are not written here because
        // they were already written in the table of contents that points here
        if (!_WriteBuffer(asset->GetSize(), asset->GetBuffer().get(), bytesWritten)) {
            TF_RUNTIME_ERROR("ERROR: Failed to write asset data for \"%s\" to shared memory",
                             path.c_str());
            // We don't return false so that we continue to write the rest of the assets even if
            // one or more textures are missing. If the USDC data is missing, a later step in the
            // process will fail.
        }
    }
    return true;
}

// Private functions

void
AssetWriter::_SetupTableOfContents()
{
    // First, we calculate the size of the table of contents, so we can properly calculate the
    // asset offsets after the table of contents.
    size_t tableOfContentsSize = 0;

    // Number of assets to be written at the start of the table of contents
    _IncrementBySizeType(tableOfContentsSize);

    for (const auto& [path, asset] : _assetsToWrite) {
        // Simulate writing the asset path, size and offset to shared memory, to calculate the size
        // of the table of contents
        _IncrementBySizeAndString(path, tableOfContentsSize); // Asset path
        _IncrementBySizeType(tableOfContentsSize);            // Asset size
        _IncrementBySizeType(tableOfContentsSize);            // Asset offset
    }

    // Now that we know how much space the table of contents will take, we can set all the offsets
    // properly
    _tableOfContents.sizeInBytes = tableOfContentsSize;

    size_t currentOffset = tableOfContentsSize;
    for (const auto& [path, asset] : _assetsToWrite) {

        size_t assetSize = asset->GetSize();
        _tableOfContents.assetPathsAndInfo[path] = { assetSize, currentOffset };
        // Increment the offset to where the next asset will be written
        currentOffset += assetSize;
    }

    // Set the total size needed for storing the table of contents and the assets
    _requiredSize = currentOffset;
}

bool
AssetWriter::_WriteTableOfContents(size_t& bytesWritten)
{
    if (_tableOfContents.sizeInBytes == 0) {
        // If initialized, the table of contents will at least store the number of assets. If the
        // size is 0, it hasn't been initialized.
        TF_CODING_ERROR("ERROR: Table of contents data is not set. It must be constructed before "
                        "it can be written to shared memory");
        return false;
    }

    // Write the number of assets to the shared memory
    if (!_WriteSizeType(_tableOfContents.assetPathsAndInfo.size(), bytesWritten)) {
        TF_WARN("ERROR: Failed to write number of assets to shared memory");
        return false;
    }

    for (const auto& [path, assetInfo] : _tableOfContents.assetPathsAndInfo) {
        size_t assetSize = assetInfo.size;
        size_t assetOffset = assetInfo.offset;
        if (!_WriteSizeAndString(path, bytesWritten)) {
            TF_WARN("ERROR: Failed to write asset path \"%s\" to shared memory table of contents",
                    path.c_str());
            return false;
        }
        if (!_WriteSizeType(assetSize, bytesWritten)) {
            TF_WARN("ERROR: Failed to write asset size (%zu) for asset \"%s\" to shared memory "
                    "table of contents",
                    assetSize,
                    path.c_str());
            return false;
        }
        if (!_WriteSizeType(assetOffset, bytesWritten)) {
            TF_WARN("ERROR: Failed to write asset location (%zu) for asset \"%s\" to shared memory "
                    "table of contents",
                    assetOffset,
                    path.c_str());
            return false;
        }
    }

    return true;
}

bool
AssetWriter::_WriteRaw(const void* buffer, size_t size, size_t offset)
{
    return _shm.Write(buffer, size, offset);
}

bool
AssetWriter::_WriteSizeType(SandboxSizeType value, size_t& bytesWritten)
{
    size_t dataSize = sizeof(SandboxSizeType);
    if (!_WriteRaw(&value, dataSize, bytesWritten)) {
        return false;
    }
    bytesWritten += dataSize;
    return true;
}

bool
AssetWriter::_WriteSizeAndString(const std::string& string, size_t& bytesWritten)
{
    size_t dataSize = string.size();
    if (!_WriteSizeType(dataSize, bytesWritten)) {
        return false;
    }

    if (!_WriteRaw(string.data(), dataSize, bytesWritten)) {
        return false;
    }
    bytesWritten += dataSize;
    return true;
}

bool
AssetWriter::_WriteBuffer(size_t size, const void* buffer, size_t& bytesWritten)
{
    // Warning: this function does not write the size of the buffer! The amount of data written
    // must be tracked separately.
    if (!_WriteRaw(buffer, size, bytesWritten)) {
        return false;
    }

    bytesWritten += size;
    return true;
}

void
AssetWriter::_IncrementBySizeType(size_t& bytesWritten)
{
    bytesWritten += sizeof(SandboxSizeType);
}

void
AssetWriter::_IncrementBySizeAndString(const std::string& string, size_t& bytesWritten)
{
    bytesWritten += sizeof(SandboxSizeType) + string.size();
}

} // namespace adobe::usd::sandbox