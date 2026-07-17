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

#include <pxr/usd/ar/asset.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace adobe::usd::sandbox {

// Mapping of asset paths to their associated ArAsset object pointers
using AssetMap = std::unordered_map<std::string, std::shared_ptr<PXR_NS::ArAsset>>;

// Use size_t for the size type
using SandboxSizeType = size_t;

/// Basic struct to hold information about an asset for use in the table of contents in the
/// AssetReader and AssetWriter.
struct AssetInfo
{
    size_t size;
    size_t offset;
};

/// Holds data required for constructing the table of contents.
struct AssetTableOfContents
{
    // Total size of the table of contents in bytes. Note that this is only used for writing
    size_t sizeInBytes = 0;

    // List of entries, each containing a path to the asset, and information about the asset
    // that will be used to construct the table of contents.
    std::map<std::string, AssetInfo> assetPathsAndInfo;
};

} // namespace adobe::usd::sandbox
