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

#include <assetResolver/sbsarAsset.h>
#include <assetResolver/sbsarImage.h>

#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sbsar {
namespace {

uint32_t
_computePixelBufferSize(const SubstanceTexture& texture)
{
    size_t bytePerPixel = SbsarImage::getBytePerPixel(texture.pixelFormat);
    return texture.level0Height * texture.level0Width * bytePerPixel;
}

std::shared_ptr<const char>
copyBuffer(const SubstanceAir::RenderResultImage& img)
{
    auto tex = img.getTexture();
    size_t data_size = _computePixelBufferSize(tex);
    size_t buffer_size = sizeof(SbsarAsset::AssetHeader) + data_size;
    auto buffer = std::shared_ptr<char>(new char[buffer_size], std::default_delete<char[]>());
    auto* header = reinterpret_cast<SbsarAsset::AssetHeader*>(buffer.get());
    char* data = buffer.get() + sizeof(SbsarAsset::AssetHeader);
    header->level0Width = tex.level0Width;
    header->level0Height = tex.level0Height;
    header->pixelFormat = tex.pixelFormat;
    header->channelsOrder = Substance_ChanOrder_RGBA;
    header->mipmapCount = tex.mipmapCount;

    memcpy(data, tex.buffer, data_size);
    return buffer;
}
}

SbsarAsset::SbsarAsset(const std::shared_ptr<SubstanceAir::RenderResultImage>& renderResultImage)
  : mRenderResultImage(renderResultImage)
{
    size_t data_size = _computePixelBufferSize(mRenderResultImage->getTexture());
    mBufferSize = sizeof(SbsarAsset::AssetHeader) + data_size;
}

const SubstanceTexture&
SbsarAsset::getSubstanceTexture() const
{
    return mRenderResultImage->getTexture();
}

size_t
SbsarAsset::GetSize() const
{
    return mBufferSize;
}

std::shared_ptr<const char>
SbsarAsset::GetBuffer() const
{
    if (!mBuffer) {
        mBuffer = copyBuffer(*mRenderResultImage);
    }
    return mBuffer;
}

std::pair<FILE*, size_t>
SbsarAsset::GetFileUnsafe() const
{
    TF_RUNTIME_ERROR("SbsarAsset::GetFileUnsafe not implemented");
    return { nullptr, 0 };
}

size_t
SbsarAsset::Read(void* buffer, size_t count, size_t offset) const
{
    TF_RUNTIME_ERROR("SbsarAsset::Read not implemented");
    return 0;
}

}
