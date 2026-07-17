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

#include <sandbox/protocol/assetReader.h>

#include <sandbox/debugCodes.h>
#include <sandbox/resolver/inMemoryWritableAsset.h>

#include <ipc/sharedMemory.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/fileUtils.h>

namespace adobe::usd::sandbox {

using namespace PXR_NS;

AssetReader::AssetReader(ipc::SharedMemory& shm)
  : _shm(shm)
{
    if (!_ReadTableOfContentsIntoCache()) {
        TF_WARN("ERROR: Failed to read table of contents from shared memory\n");
    }
}

std::shared_ptr<PXR_NS::ArAsset>
AssetReader::ReadAssetFromSharedMemory(const std::string& path)
{
    TF_DEBUG_MSG(
      FILE_FORMAT_SANDBOXPROXY, "Reading asset from binary data for path: %s\n", path.c_str());

    size_t assetSize, assetOffset;
    if (!_ReadAssetSizeAndOffset(path, assetSize, assetOffset)) {
        TF_WARN("ERROR: Failed to read size and offset from binary data for path: %s",
                path.c_str());
        return nullptr;
    }

    // Reject sizes that don't fit in shared memory before attempting to allocate the buffer
    const size_t shmSize = _shm.GetSize();
    if (assetOffset > shmSize || assetSize > shmSize - assetOffset) {
        TF_WARN("ERROR: Asset \"%s\" with size %zu at offset %zu exceeds shared memory size %zu",
                path.c_str(),
                assetSize,
                assetOffset,
                shmSize);
        return nullptr;
    }

    // ArInMemoryAsset::FromBuffer requires a shared_ptr<const char>, but the char is a buffer.
    // Normally, the shared_ptr would use the char delete, which will cause memory issues if used
    // on a char[]. Instead, we explicitly register the char[] delete
    std::shared_ptr<char> buffer(new char[assetSize], std::default_delete<char[]>());

    // Read the asset data from shared memory into our new buffer
    if (!_ReadBuffer(assetSize, buffer.get(), assetOffset)) {
        TF_WARN("ERROR: Failed to read asset data for \"%s\" from shared memory", path.c_str());
        return nullptr;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "Successfully read asset from binary data for path: %s\n",
                 path.c_str());

    // Create the ArInMemoryAsset from our copied buffer
    return std::static_pointer_cast<ArAsset>(
      ArInMemoryAsset::FromBuffer(std::static_pointer_cast<const char>(buffer), assetSize));
}

bool
AssetReader::ProcessAssetsFromSharedMemory(
  std::function<void(const std::string& path, const std::shared_ptr<PXR_NS::ArAsset>& asset)>
    processAssetCallback)
{
    if (_tableOfContents.assetPathsAndInfo.empty()) {
        TF_WARN("Warning: No assets found while reading from shared memory\n");
        return true;
    }

    for (const auto& [path, _] : _tableOfContents.assetPathsAndInfo) {
        std::shared_ptr<ArAsset> asset = ReadAssetFromSharedMemory(path);
        if (!asset) {
            TF_WARN("ERROR: Failed to read asset from binary data for path: %s", path.c_str());
            return false;
        }
        processAssetCallback(path, asset);
    }
    return true;
}

// Private functions

bool
AssetReader::_ReadTableOfContentsIntoCache()
{
    size_t bytesRead = 0;

    if (!_tableOfContents.assetPathsAndInfo.empty()) {
        return true;
    }
    // Asset's location in the table of contents has not been cached. Reconstruct it

    // Read the number of assets to reconstruct the table of contents
    size_t numAssets;
    if (!_ReadSizeType(numAssets, bytesRead)) {
        TF_WARN("ERROR: Failed to read number of assets from shared memory");
        return false;
    }

    for (size_t i = 0; i < numAssets; ++i) {
        std::string assetPath;
        if (!_ReadAndResizeString(assetPath, bytesRead)) {
            TF_WARN("ERROR: Failed to read asset path %zu from shared memory for constructing "
                    "asset table of contents",
                    i);
            return false;
        }

        size_t assetSize;
        if (!_ReadSizeType(assetSize, bytesRead)) {
            TF_WARN("ERROR: Failed to read asset size for asset \"%s\" from shared memory "
                    "for constructing asset table of contents",
                    assetPath.c_str());
            return false;
        }

        size_t assetLocation;
        if (!_ReadSizeType(assetLocation, bytesRead)) {
            TF_WARN("ERROR: Failed to read asset location for asset \"%s\" from shared memory "
                    "for constructing asset table of contents",
                    assetPath.c_str());
            return false;
        }

        _tableOfContents.assetPathsAndInfo[assetPath] = { assetSize, assetLocation };
    }
    return true;
}

bool
AssetReader::_ReadAssetSizeAndOffset(const std::string& path,
                                     size_t& assetSize,
                                     size_t& assetOffset)
{
    // Read the asset's size and location from the table of contents
    const auto it = _tableOfContents.assetPathsAndInfo.find(path);
    if (it == _tableOfContents.assetPathsAndInfo.end()) {
        TF_WARN("ERROR: Asset \"%s\" not found in shared memory", path.c_str());
        return false;
    }
    assetSize = it->second.size;
    assetOffset = it->second.offset;

    return true;
}

bool
AssetReader::_ReadRaw(size_t offset, void* buffer, size_t size)
{
    return _shm.Read(offset, buffer, size);
}

bool
AssetReader::_ReadSizeType(SandboxSizeType& value, size_t& bytesRead)
{
    if (_ReadRaw(bytesRead, &value, sizeof(SandboxSizeType))) {
        bytesRead += sizeof(SandboxSizeType);
        return true;
    } else {
        return false;
    }
}

bool
AssetReader::_ReadAndResizeString(std::string& string, size_t& bytesRead)
{
    size_t dataSize;
    if (!_ReadSizeType(dataSize, bytesRead)) {
        return false;
    }

    // Reject lengths that don't fit in shared memory before attempting to allocate the buffer
    const size_t shmSize = _shm.GetSize();
    if (bytesRead > shmSize || dataSize > shmSize - bytesRead) {
        TF_WARN("ERROR: Declared string size %zu at offset %zu exceeds shared memory size %zu",
                dataSize,
                bytesRead,
                shmSize);
        return false;
    }

    string.resize(dataSize);
    if (_ReadRaw(bytesRead, string.data(), dataSize)) {
        bytesRead += dataSize;
        return true;
    } else {
        return false;
    }
}

bool
AssetReader::_ReadBuffer(size_t& size, void* buffer, size_t& bytesRead)
{
    if (_ReadRaw(bytesRead, buffer, size)) {
        bytesRead += size;
        return true;
    }
    return false;
}

} // namespace adobe::usd::sandbox