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
#include <pxr/usd/ar/asset.h>
#include <substance/framework/renderresult.h>

namespace adobe::usd::sbsar {
//! Asset representing a substance texture.
//! If GetBuffer() is called, the buffer will be copied from the RenderResultImage.
class SbsarAsset final : public PXR_NS::ArAsset
{
  public:
    struct AssetHeader
    {
        unsigned short level0Width;
        unsigned short level0Height;
        unsigned char pixelFormat;
        unsigned char channelsOrder;
        unsigned char mipmapCount;
    };

    explicit SbsarAsset(const std::shared_ptr<SubstanceAir::RenderResultImage>& renderResultImage);

    const SubstanceTexture& getSubstanceTexture() const;

    size_t GetSize() const override;
    //! This function makes a copy of the buffer from the RenderResultImage.
    //! Prefere use getSubstanceTexture() to access to the texture data.
    std::shared_ptr<const char> GetBuffer() const override;
    size_t Read(void* buffer, size_t count, size_t offset) const override;
    std::pair<FILE*, size_t> GetFileUnsafe() const override;

  private:
    std::shared_ptr<SubstanceAir::RenderResultImage> mRenderResultImage;
    //! Buffer containing the header + image data in a continuous buffer.
    //! It is mutable because in GetBuffer(), the first call will copy the buffer in
    //! mRenderResultImage to mBuffer.
    mutable std::shared_ptr<const char> mBuffer;
    //! Buffer size in bytes
    size_t mBufferSize;
};
}
