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

#include <assetResolver/sbsarImage.h>

#include <assetPath/assetPathParser.h>
#include <assetResolver/sbsarPackageResolver.h>
#include <sbsarDebug.h>
#include <sbsarEngine/sbsarRenderThread.h>

#include <substance/engineid.h>

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/imaging/hio/types.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/packageUtils.h"
#include "pxr/usd/ar/resolver.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdlib.h>
#include <tuple>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

//! Metadata tokens used to collaborate with the Renderer.
//! Allow to share the internal pixel buffer with the renderer.
const PXR_NS::TfToken cTfToken_internalPixelBuffer("internalPixelBuffer");

// Return a tuple composed of :
// 1. the hydra format
// 2. a boolean specifying whether it is in the sRGB color space
std::tuple<PXR_NS::HioFormat, bool>
getFormatDescription(const std::string& filename, unsigned char pixelFormat, bool isSRGB)
{
    if ((pixelFormat & Substance_PF_MASK_RAWFormat) != Substance_PF_RAW) {
        TF_RUNTIME_ERROR("Unsupported Format (only raw is supported)");
        return { PXR_NS::HioFormat::HioFormatInvalid, false };
    }

    auto rawChannels = pixelFormat & Substance_PF_MASK_RAWChannels;
    auto rawPrecision = pixelFormat & Substance_PF_MASK_RAWPrecision;
    switch (rawChannels) {
        case Substance_PF_RGBA:
        case Substance_PF_RGBx:
            switch (rawPrecision) {
                case Substance_PF_16I:
                    return { PXR_NS::HioFormatUInt16Vec4, isSRGB };
                case Substance_PF_8I:
                    return { isSRGB ? PXR_NS::HioFormatUNorm8Vec4srgb : PXR_NS::HioFormatUNorm8Vec4,
                             isSRGB };
                case Substance_PF_16F:
                    return { PXR_NS::HioFormatFloat16Vec4, isSRGB };
                case Substance_PF_32F:
                    return { PXR_NS::HioFormatFloat32Vec4, isSRGB };
                default:
                    TF_RUNTIME_ERROR("Unsupported bit depth (only 16 or 8 bpp "
                                     "integer supported)");
                    return { PXR_NS::HioFormat::HioFormatInvalid, false };
            }

        case Substance_PF_RGB:
            switch (rawPrecision) {
                case Substance_PF_16I:
                    return { PXR_NS::HioFormatUInt16Vec3, isSRGB };
                case Substance_PF_8I:
                    return { isSRGB ? PXR_NS::HioFormatUNorm8Vec3srgb : PXR_NS::HioFormatUNorm8Vec3,
                             isSRGB };
                case Substance_PF_16F:
                    return { PXR_NS::HioFormatFloat16Vec3, isSRGB };
                case Substance_PF_32F:
                    return { PXR_NS::HioFormatFloat32Vec3, isSRGB };
                default:
                    TF_RUNTIME_ERROR("Unsupported bit depth (only 16 or 8 bpp "
                                     "integer supported)");
                    return { PXR_NS::HioFormat::HioFormatInvalid, false };
            }
        case Substance_PF_L:
            switch (rawPrecision) {
                case Substance_PF_16I:
                    return { PXR_NS::HioFormatUInt16, isSRGB };
                case Substance_PF_8I:
                    return { isSRGB ? PXR_NS::HioFormatUNorm8srgb : PXR_NS::HioFormatUNorm8,
                             isSRGB };
                case Substance_PF_16F:
                    return { PXR_NS::HioFormatFloat16, isSRGB };
                case Substance_PF_32F:
                    return { PXR_NS::HioFormatFloat32, isSRGB };
                default:
                    TF_RUNTIME_ERROR("Unsupported bit depth (only 16 or 8 bpp "
                                     "integer supported)");
                    return { PXR_NS::HioFormat::HioFormatInvalid, false };
            }
        default:
            TF_RUNTIME_ERROR("Unsupported color format");
            return { PXR_NS::HioFormat::HioFormatInvalid, false };
    }
}
}

uint32_t
SbsarImage::getBytePerPixel(unsigned char pixelFormat)
{
    auto rawChannels = pixelFormat & Substance_PF_MASK_RAWChannels;
    auto rawPrecision = pixelFormat & Substance_PF_MASK_RAWPrecision;

    uint32_t channelNumber = 0;
    switch (rawChannels) {
        case Substance_PF_RGBA:
        case Substance_PF_RGBx:
            channelNumber = 4;
            break;
        case Substance_PF_RGB:
            channelNumber = 3;
            break;
        case Substance_PF_L:
            channelNumber = 1;
            break;
        default:
            TF_RUNTIME_ERROR("Unsupported color format");
    }
    uint32_t bytePerChannel = 0;
    switch (rawPrecision) {
        case Substance_PF_8I:
            bytePerChannel = 1;
            break;
        case Substance_PF_16I:
        case Substance_PF_16F:
            bytePerChannel = 2;
            break;
        case Substance_PF_32F:
            bytePerChannel = 4;
            break;
        default:
            TF_RUNTIME_ERROR("Unsupported bit precision");
    }
    return channelNumber * bytePerChannel;
}

SbsarImage::SbsarImage()
  : mFilename()
{
}

SbsarImage::~SbsarImage() {}

const std::string&
SbsarImage::GetFilename() const
{
    return mFilename;
}

int
SbsarImage::GetWidth() const
{
    return mSbsarAsset->getSubstanceTexture().level0Width;
}

int
SbsarImage::GetHeight() const
{
    return mSbsarAsset->getSubstanceTexture().level0Height;
}

PXR_NS::HioFormat
SbsarImage::GetFormat() const
{
#ifdef FIX_STORM_16BIT
    if (mFormat == PXR_NS::HioFormatUInt16Vec4)
        return PXR_NS::HioFormatUNorm8Vec4;
    if (mFormat == PXR_NS::HioFormatUInt16Vec3)
        return PXR_NS::HioFormatUNorm8Vec3;
    if (mFormat == PXR_NS::HioFormatUInt16)
        return PXR_NS::HioFormatUNorm8;
#endif

    return mFormat;
}

int
SbsarImage::GetBytesPerPixel() const
{
#ifdef FIX_STORM_16BIT
    if (mFormat == PXR_NS::HioFormatUInt16Vec4 || mFormat == PXR_NS::HioFormatUInt16Vec3 ||
        mFormat == PXR_NS::HioFormatUInt16)
        return mBytePerPixel / 2;
#endif
    return mBytePerPixel;
}

int
SbsarImage::GetNumMipLevels() const
{
    return 1;
}

bool
SbsarImage::IsColorSpaceSRGB() const
{
    return mIsColorSpaceSRGB;
}

bool
SbsarImage::GetMetadata(const PXR_NS::TfToken& key, PXR_NS::VtValue* value) const
{
    if (key == cTfToken_internalPixelBuffer) {
        *value = VtValue(_GetBuffer());
        return true;
    }
    return false;
}

bool
SbsarImage::GetSamplerMetadata(PXR_NS::HioAddressDimension /*dim*/,
                               PXR_NS::HioAddressMode* /*param*/) const
{
    return false;
}

bool
SbsarImage::Read(const StorageSpec& storage)
{
    if (storage.width != GetWidth() || storage.height != GetHeight()) {
        TF_RUNTIME_ERROR("storage size does not match image size");
        return false;
    }

// Storm does not seems to support 16 bits integer inputs
#ifdef FIX_STORM_16BIT
    if (mFormat == PXR_NS::HioFormatUInt16Vec4 || mFormat == PXR_NS::HioFormatUInt16Vec3 ||
        mFormat == PXR_NS::HioFormatUInt16) {
        uint8_t* dstData = reinterpret_cast<uint8_t*>(storage.data);
        const uint16_t* srcData = reinterpret_cast<const uint16_t*>(_GetBuffer());
        const int channel_nb = mBytePerPixel / 2;
        for (int i = 0; i < storage.height; ++i) {
            int i_prim = storage.flipped ? storage.height - 1 - i : i;
            for (int j = 0; j < storage.width * channel_nb; ++j) {

                float f = static_cast<float>(srcData[j + i_prim * storage.width * channel_nb]) /
                          std::numeric_limits<uint16_t>::max();
                dstData[j + i * storage.width * channel_nb] =
                  static_cast<uint8_t>(std::round(std::numeric_limits<uint8_t>::max() * f));
            }
        }
        return true;
    }
#endif

    assert(storage.format == GetFormat());
    if (storage.format != GetFormat()) {
        TF_RUNTIME_ERROR("format does not match");
        return false;
    }

    if (storage.flipped) {
        // copy line by line in a reverse order
        uint8_t* dstData = reinterpret_cast<uint8_t*>(storage.data);
        const uint8_t* srcData = reinterpret_cast<const uint8_t*>(_GetBuffer());
        for (int i = 0; i < storage.height; ++i) {
            memcpy(dstData + i * storage.width * mBytePerPixel,
                   srcData + (storage.height - 1 - i) * storage.width * mBytePerPixel,
                   storage.width * mBytePerPixel);
        }
    } else {
        memcpy(storage.data, _GetBuffer(), storage.height * storage.width * mBytePerPixel);
    }

    return true;
}

bool
SbsarImage::ReadCropped(int const /*cropTop*/,
                        int const /*cropBottom*/,
                        int const /*cropLeft*/,
                        int const /*cropRight*/,
                        const StorageSpec& /*storage*/)
{
    TF_RUNTIME_ERROR("SbsarImage::ReadCropped not implemented");
    // NOT implemented
    return false;
}

bool
SbsarImage::Write(const StorageSpec& /*storage*/, const PXR_NS::VtDictionary& /*metadata*/)
{
    TF_RUNTIME_ERROR("SbsarImage::Write not implemented");
    // NOT implemented
    return false;
}

bool
SbsarImage::_OpenForReading(const std::string& filename,
                            int /*subimage*/,
                            int /*mip*/,
                            SourceColorSpace sourceColorSpace,
                            bool /*suppressErrors*/)
{
    std::shared_ptr<ArAsset> asset =
      PXR_NS::ArGetResolver().OpenAsset(PXR_NS::ArResolvedPath(filename));
    if (!asset) {
        TF_RUNTIME_ERROR("Fail to retrieve asset %s", filename.c_str());
        return false;
    }

    mSbsarAsset = std::dynamic_pointer_cast<adobe::usd::sbsar::SbsarAsset>(asset);
    if (!mSbsarAsset) {
        TF_RUNTIME_ERROR("Fail to cast file %s to SbsarAsset", filename.c_str());
        return false;
    }

    // Store the file name
    mFilename = filename;

    unsigned char pixelFormat = _GetPixelFormat();
    const bool isSRGB = [&]() -> bool {
        switch (sourceColorSpace) {
            case HioImage::SourceColorSpace::Auto:
                return (pixelFormat & Substance_PF_sRGB) != 0;
            case HioImage::SourceColorSpace::SRGB:
                return true;
            case HioImage::SourceColorSpace::Raw:
                return false;
            default:
                TF_RUNTIME_ERROR("Unsupported color space");
                return false;
        };
    }();
    // Retrieve format, srgb, and byte per pixel values
    std::tie(mFormat, mIsColorSpaceSRGB) =
      getFormatDescription(filename, _GetPixelFormat(), isSRGB);
    mBytePerPixel = getBytePerPixel(_GetPixelFormat());
    return true;
}

bool
SbsarImage::_OpenForWriting(const std::string& /*filename*/)
{
    TF_RUNTIME_ERROR("SbsarImage::_OpenForWriting not implemented");
    // NOT implemented
    return false;
}

const char*
SbsarImage::_GetBuffer() const
{
    return reinterpret_cast<const char*>(mSbsarAsset->getSubstanceTexture().buffer);
}

unsigned char
SbsarImage::_GetPixelFormat() const
{
    return mSbsarAsset->getSubstanceTexture().pixelFormat;
}

TF_REGISTRY_FUNCTION(TfType)
{
    // TODO namespace
    using Image = SbsarImage;
    TfType t = TfType::Define<Image, TfType::Bases<Image::Base>>();
    t.SetFactory<HioImageFactory<Image>>();
}
