/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#pragma once
#include <api.h>
#include <string>
#include <substance/framework/inputimage.h>
namespace adobe::usd::sbsar {

//! \brief Load and cache image from a file.
//! This function is safe to call from any thread and will load the image if it isn't in the
//! cache yet. The cache size is controled by CacheSize. When the cache is full, 10 % of the oldest
//! image in cache are removed.
//! \param resolvedAssetPath The complete path to the asset that should be opened and converted.
//! \return Hash of the image in the cache. This hash can be used to retrieve the image.
USDSBSAR_API std::size_t
addImageToInputImageCache(const std::string& resolvedAssetPath);

//! \brief Get an image from the cache.
//! This function is safe to call from any thread an
//! Warning: If the image is not in the cache, this function will return a nullptr.
//! \param hash The hash of the image in the cache.
//! \return The image in the cache or nullptr if the image is not in the cache.
USDSBSAR_API SubstanceAir::InputImage::SPtr
getImageFromInputImageCache(std::size_t hash);

//! \brief Erase all the cache.
void
clearInputImageCache();
}
