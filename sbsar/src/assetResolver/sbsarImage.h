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

#include "api.h"

#include <substance/framework/framework.h>

#include <pxr/imaging/hio/image.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE
class ArAsset;
PXR_NAMESPACE_CLOSE_SCOPE

//! \brief This class are in charge of handling ".sbsarimage". It provide the
//! interface to read the underlying texture store in an ArAsset.
//!
//! \note SbsarImage are in charge of handling .sbsarimage
//!
//! \see plugInfo.json.in to see how .sbsarImage and SbsarImage are link
//! together \see SBSARPackageResolver to see where .sbsarImage are created

class SbsarImage final : public PXR_NS::HioImage
{
  public:
    struct ImageHeader
    {
        unsigned short level0Width;
        unsigned short level0Height;
        unsigned char pixelFormat;
        unsigned char channelsOrder;
        unsigned char mipmapCount;
        bool isSRGB;
    };
    static uint32_t getBytePerPixel(unsigned char pixelFormat);

    using Base = HioImage;
    USDSBSAR_API
    SbsarImage();
    USDSBSAR_API
    ~SbsarImage() override;

    //! @{ HioImage overrides
    const std::string& GetFilename() const override;
    int GetWidth() const override;
    int GetHeight() const override;
    PXR_NS::HioFormat GetFormat() const override;
    int GetBytesPerPixel() const override;
    int GetNumMipLevels() const override;
    bool IsColorSpaceSRGB() const override;
    bool GetMetadata(const PXR_NS::TfToken& key, PXR_NS::VtValue* value) const override;
    bool GetSamplerMetadata(PXR_NS::HioAddressDimension dim,
                            PXR_NS::HioAddressMode* param) const override;
    bool Read(const StorageSpec& storage) override;
    bool ReadCropped(int const cropTop,
                     int const cropBottom,
                     int const cropLeft,
                     int const cropRight,
                     const StorageSpec& storage) override;
    bool Write(const StorageSpec& storage, const PXR_NS::VtDictionary& metadata) override;

  protected:
    virtual bool _OpenForReading(std::string const& filename,
                                 int subimage,
                                 int mip,
                                 SourceColorSpace sourceColorSpace,
                                 bool suppressErrors) override;

    virtual bool _OpenForWriting(std::string const& filename) override;
    //! @} HioImage overrides
  private:
    const ImageHeader& GetHeader() const;
    const char* GetBuffer() const;

    std::string mFilename;
    size_t mAssetSize;
    std::shared_ptr<const char> mAssetBuffer;
    bool mIsColorSpaceSRGB;
    PXR_NS::HioFormat mFormat;
    int mBytePerPixel;
};
