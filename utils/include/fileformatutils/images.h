/*
Copyright 2023 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#pragma once

#include "usdData.h"
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdShade/material.h>
#include <string>
#include <vector>

namespace adobe::usd {

/// \ingroup utils_materials
/// \brief Handles image data and stores it as R8G8B8A8 integer data.
/// It uses malloc/free to be compatible with stb.
/// Might be able to replace by OpenImageIO, which USD also depends on.
///
class USDFFUTILS_API Image
{
  public:
    int width;
    int height;
    int channels;
    std::vector<float> pixels;

    /// No allocation of image data is done yet at construction.
    Image();

    /// Frees the pixel image data.
    ~Image();

    /// Returns whether the image is empty
    bool isEmpty() const { return width == 0 || height == 0 || channels == 0; }

    /// Allocates memory for the pixel image data with dimensions \p width x \p height x \p
    /// channels.
    bool allocate(int width, int height, int channels);

    /// Reads image data from an ImageAsset (which holds an encoded image like jpg, pgn, bmp, ...).
    bool read(const ImageAsset& imageAsset, int forceChannels = -1);

    /// Writes image data to an ImageAsset (The encoding type needs to have been specified on the
    /// asset)
    bool write(ImageAsset& imageAsset) const;

    /// Converts an image to a png
    static bool convertImageToPng(const ImageAsset& srcImageAsset, ImageAsset& dstImageAsset);

    /// Copys channel \p channelSrc from \p src to own channel \p channelDst
    bool copyChannel(const Image& src, int channelSrc, int channelDst);

    /// Maps the scale/bias transform to a \p channelSrc from \p src to own channel \p channelDst
    bool transformChannel(const Image& src,
                          int channelSrc,
                          float scale,
                          float bias,
                          int channelDst);

    /// Set RGBA values to the image if it has storage allocated.
    void set(float r, float g, float b, float a);

    /// Get the min and max values for the pixels in the image
    /// If min is larger than max for a channel, the channel did not exist
    std::pair<PXR_NS::GfVec4f, PXR_NS::GfVec4f> computeRange() const;
};

/// \ingroup utils_materials
/// \brief Multiplies 2 images element-wise
USDFFUTILS_API bool
imageMult(const Image& in, const Image& factor, Image& out);

/// \ingroup utils_materials
/// \brief Affine transforms an image
USDFFUTILS_API bool
imageTransformAffine(const Image& in, float scale, float bias, Image& out);

/// \ingroup utils_materials
/// \brief Apply scale/bias transform to a single channel of source image and store in single
/// channel output image
USDFFUTILS_API bool
imageExtractChannel(const Image& in, int channelSrc, float scale, float bias, Image& out);

/// \ingroup utils_materials
/// \brief Writes the ImageAsset object to file. Used for debugging.
USDFFUTILS_API void
imageWrite(const ImageAsset& image, const std::string& filename, bool overwrite = false);

/// \ingroup utils_materials
/// \brief Assigns a PXR_NS::VtArray to a std::vector.
/// Makes debugging VtArray contents easier, since VtArrays are not inspectable in debugger, but
/// vector is.
template<typename T>
void
assign(std::vector<T>& v, PXR_NS::VtArray<T> u)
{
    memcpy(v.data(), u.data(), u.size());
}

/// \ingroup utils_materials
/// \brief Converts srgb component to linear
USDFFUTILS_API float
srgbToLinear(float s);

/// \ingroup utils_materials
/// \brief Converts linear component to srgb
USDFFUTILS_API float
linearToSRGB(float s);

/// \ingroup utils_materials
/// \brief Is the resolved asset path a supported image file
bool USDFFUTILS_API
isImageFileSupported(const std::string& resolvedAssetPath);

/// \ingroup utils_materials
/// \brief Is the uri a sbsar image
bool USDFFUTILS_API
isUriSbsarImage(const std::string& uri);

/// \ingroup utils_materials
/// \brief Get the sbsar usage from the parameters string
std::string USDFFUTILS_API
getSbsarUsageFromParameters(const std::string& parametersStr);

/// \ingroup utils_materials
/// \brief Get the sbsar image extension
std::string USDFFUTILS_API
getSbsarImageExtension(const std::string& resolvedAssetPath);

/// \ingroup utils_materials
/// \brief Extract the file path from the asset path
std::string USDFFUTILS_API
extractFilePathFromAssetPath(const std::string& assetPath);

/// \ingroup utils_materials
/// \brief Transcodes an image asset to memory
bool USDFFUTILS_API
transcodeImageAssetToMemory(const std::string& resolvedAssetPath,
                            const std::string& filename,
                            std::vector<uint8_t>& outputPixelData);

}
