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
#include <sbsarDebug.h>
#include <sbsarEngine/sbsarInputImageCache.h>
#include <sbsarEngine/sbsarRenderThread.h>

#include <pxr/base/tf/diagnosticLite.h>
#include <pxr/imaging/hio/image.h>

#include <mutex>
#include <unordered_map>

using namespace SubstanceAir;
PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sbsar {
namespace {
//! Data stucture to store input image in the cache.
struct InputImageCacheData
{
    //! Input image.
    InputImage::SPtr image;
    //! Creation time of the image, or last access time.
    std::chrono::time_point<std::chrono::steady_clock> lastAccessTime;
    //! Image size in bytes.
    std::size_t size;
    explicit InputImageCacheData()
      : lastAccessTime(std::chrono::steady_clock::now())
    {
    }

    void updateLastAccessTime() { lastAccessTime = std::chrono::steady_clock::now(); }
};

struct InputImageCache
{
    std::unordered_map<std::size_t, InputImageCacheData> cache;
    //! Total size of the cache in bytes.
    std::size_t size = 0;
};

//! Convert HioFormat to SubstancePixelFormat.
//! Some HioFormat are not supported by Substance engine.
unsigned char
toSubstancePixelFormat(HioFormat format)
{
    switch (format) {
        case HioFormatUNorm8:
            return Substance_PF_8I | Substance_PF_L;
        case HioFormatUNorm8Vec3:
            return Substance_PF_8I | Substance_PF_RGB;
        case HioFormatUNorm8Vec4:
            return Substance_PF_8I | Substance_PF_RGBA;
        case HioFormatFloat16:
            return Substance_PF_16F | Substance_PF_L;
        case HioFormatFloat16Vec3:
            return Substance_PF_16F | Substance_PF_RGB;
        case HioFormatFloat16Vec4:
            return Substance_PF_16F | Substance_PF_RGBA;
        case HioFormatFloat32:
            return Substance_PF_32F | Substance_PF_L;
        case HioFormatFloat32Vec3:
            return Substance_PF_32F | Substance_PF_RGB;
        case HioFormatFloat32Vec4:
            return Substance_PF_32F | Substance_PF_RGBA;
        case HioFormatInt16:
            return Substance_PF_16I | Substance_PF_L;
        case HioFormatInt16Vec3:
            return Substance_PF_16I | Substance_PF_RGB;
        case HioFormatInt16Vec4:
            return Substance_PF_16I | Substance_PF_RGBA;
        case HioFormatUNorm8srgb:
            return Substance_PF_8I | Substance_PF_L | Substance_PF_sRGB;
        case HioFormatUNorm8Vec3srgb:
            return Substance_PF_8I | Substance_PF_RGB | Substance_PF_sRGB;
        case HioFormatUNorm8Vec4srgb:
            return Substance_PF_8I | Substance_PF_RGBA | Substance_PF_sRGB;
        case HioFormatSNorm8:
        case HioFormatSNorm8Vec3:
        case HioFormatSNorm8Vec4:
        case HioFormatDouble64:
        case HioFormatDouble64Vec3:
        case HioFormatDouble64Vec4:
        case HioFormatUInt16:
        case HioFormatUInt16Vec3:
        case HioFormatUInt16Vec4:
        case HioFormatUInt32:
        case HioFormatUInt32Vec3:
        case HioFormatUInt32Vec4:
        case HioFormatInt32:
        case HioFormatInt32Vec3:
        case HioFormatInt32Vec4:
        case HioFormatUNorm8Vec2:
        case HioFormatSNorm8Vec2:
        case HioFormatFloat16Vec2:
        case HioFormatFloat32Vec2:
        case HioFormatDouble64Vec2:
        case HioFormatUInt16Vec2:
        case HioFormatInt16Vec2:
        case HioFormatUInt32Vec2:
        case HioFormatInt32Vec2:
        case HioFormatUNorm8Vec2srgb:
        case HioFormatBC6FloatVec3:
        case HioFormatBC6UFloatVec3:
        case HioFormatBC7UNorm8Vec4:
        case HioFormatBC7UNorm8Vec4srgb:
        case HioFormatBC1UNorm8Vec4:
        case HioFormatBC3UNorm8Vec4:
            TF_RUNTIME_ERROR("SbsarRender: Unsupported HioFormat %i", format);
            return 0;
        case HioFormatCount:
        case HioFormatInvalid:
            TF_RUNTIME_ERROR("SbsarRender: Invalid HioFormat");
            return 0;
    }
    TF_RUNTIME_ERROR("SbsarRender: Invalid HioFormat");
    return 0;
}

//! Convert HioImage to InputImage.
std::pair<InputImage::SPtr, size_t>
toInputImage(const HioImageSharedPtr& img)
{
    if (img == nullptr) {
        TF_RUNTIME_ERROR("SbsarRender: Failed to convert HioImage to InputImage");
        return { nullptr, 0 };
    }
    unsigned char substanceFormat = toSubstancePixelFormat(img->GetFormat());
    SubstanceTexture texture = { nullptr, // Buffer will be set later
                                 static_cast<unsigned short>(img->GetWidth()),
                                 static_cast<unsigned short>(img->GetHeight()),
                                 substanceFormat,
                                 Substance_ChanOrder_RGBA,
                                 1 };
    InputImage::SPtr inputImage = InputImage::create(texture);
    InputImage::ScopedAccess access(inputImage);
    // Fill image
    HioImage::StorageSpec storage;
    storage.width = img->GetWidth();
    storage.height = img->GetHeight();
    storage.format = img->GetFormat();
    storage.depth = 0;
    storage.flipped = false;
    storage.data = access->buffer;
    img->Read(storage);

    return { inputImage, access.getSize() };
}

//! Clean the cache by removing the oldest 10% of the images.
void
_cleanCache(InputImageCache& inputImageCache)
{
    TF_DEBUG(SBSAR_RENDER).Msg("AssetCache: Cleaning cache\n");
    // Sort input image by creation time and delete the oldest 10%.
    std::chrono::time_point<std::chrono::steady_clock> oldtestTimeToRemove;
    std::map<std::chrono::time_point<std::chrono::steady_clock>, std::size_t> timeSizeMap;
    for (auto& [hash, imageData] : inputImageCache.cache) {
        InputImage::ScopedAccess access(imageData.image);
        timeSizeMap[imageData.lastAccessTime] = access.getSize();
    }
    std::size_t currentSize = 0;
    std::size_t toDeleteSize = inputImageCache.size / 10;
    for (auto& [time, size] : timeSizeMap) {
        if (currentSize >= toDeleteSize)
            break;
        currentSize += size;
        oldtestTimeToRemove = time;
    }

    std::size_t nbImageDeleted = 0;
    for (auto it = inputImageCache.cache.begin(); it != inputImageCache.cache.end();) {
        if (it->second.lastAccessTime <= oldtestTimeToRemove) {
            inputImageCache.size -= it->second.size;
            ++nbImageDeleted;
            it = inputImageCache.cache.erase(it);
        } else
            ++it;
    }

    getCacheStats().inputImageDeleted += nbImageDeleted;
    TF_DEBUG(SBSAR_RENDER)
      .Msg("InputImageCache: end of cleaning cache, Image deleted: %zu, for %zu memory save\n",
           nbImageDeleted,
           currentSize);
}

//! Load and add an image in the cache.
std::size_t
_loadAndAddInputImageData(InputImageCache& inputImageCache, const std::string& resolvedAssetPath)
{
    if (resolvedAssetPath.empty())
        return 0;

    std::size_t hash = std::hash<std::string>{}(resolvedAssetPath);
    auto it = inputImageCache.cache.find(hash);
    if (it != inputImageCache.cache.end())
        // Already in the cache.
        return it->first;

    HioImageSharedPtr image = HioImage::OpenForReading(resolvedAssetPath);
    if (!image) {
        TF_RUNTIME_ERROR("Failed to load image: %s", resolvedAssetPath.c_str());
        return 0;
    }

    InputImageCacheData data;
    auto [inputImage, size] = toInputImage(image);
    if (inputImage == nullptr)
        return 0;
    data.image = inputImage;
    data.size = size;
    inputImageCache.cache[hash] = data;
    inputImageCache.size += size;
    ++getCacheStats().inputImageCreated;

    if (inputImageCache.size > getCacheSize().getMaxInputImageCacheSize())
        _cleanCache(inputImageCache);

    return hash;
}

//! Get an image from the cache if it exist.
InputImage::SPtr
_getInputImageCacheData(InputImageCache& inputImageCache, std::size_t hash)
{
    auto it = inputImageCache.cache.find(hash);
    if (it == inputImageCache.cache.end()) {
        TF_RUNTIME_ERROR("Image not found in cache");
        return nullptr;
    }
    it->second.updateLastAccessTime();
    return it->second.image;
}
//! Static structure to store the input image cache.
struct GlobalInputImageCache
{
    std::mutex mutex;
    InputImageCache inputImageCache;
};

GlobalInputImageCache&
_getGlobalInputImageCache()
{
    static GlobalInputImageCache globalInputImageCache;
    return globalInputImageCache;
}

} // end anonymous namespace

std::size_t
addImageToInputImageCache(const std::string& resolvedAssetPath)
{
    GlobalInputImageCache& globalInputImageCache = _getGlobalInputImageCache();
    std::lock_guard guard(globalInputImageCache.mutex);
    return _loadAndAddInputImageData(globalInputImageCache.inputImageCache, resolvedAssetPath);
}

InputImage::SPtr
getImageFromInputImageCache(std::size_t hash)
{
    GlobalInputImageCache& globalInputImageCache = _getGlobalInputImageCache();
    std::lock_guard guard(globalInputImageCache.mutex);
    return _getInputImageCacheData(globalInputImageCache.inputImageCache, hash);
}

void
clearInputImageCache()
{
    GlobalInputImageCache& globalInputImageCache = _getGlobalInputImageCache();
    std::lock_guard guard(globalInputImageCache.mutex);
    globalInputImageCache.inputImageCache.cache.clear();
    globalInputImageCache.inputImageCache.size = 0;
}

}
