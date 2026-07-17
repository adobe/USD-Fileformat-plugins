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

/// \file material.h
///
/// Set of material utilities.
///

#include "fileformatutils/api.h"
#include "images.h"
#include "usdData.h"

namespace adobe::usd {
/// \ingroup utils_materials
/// \brief Translates textures and values for material import/export.
///
/// The InputTranslator works on Input<T> objects,
/// which is a helper struct that stores data associated to UsdShadeInput objects,
/// for example the diffuseColor UsdShadeInput from the UsdPreviewSurface.
/// The InputTranslator helps with a number of things:
/// * convert phong to PBR Inputs<T>
/// * convert bump to normal Input<T>
/// * convert transparency to opacity Input<T>
/// * define an Input<T> with certain textures packed together
/// * each Input<T> might be sourced by a texture or by values, this class translates accordingly
/// * cache any generated textures, and route new textures accordingly in the output Inputs<T>
/// * actually generating image data is optional.
class USDFFUTILS_API InputTranslator
{
public:
    /// @param[in] exportImages: Whether to actually generate image data.
    /// @param[in] inputImages: Input images
    InputTranslator(bool exportImages,
                    std::vector<ImageAsset>& inputImages,
                    const std::string& debugTag);

    ~InputTranslator();

    /// Generates an output value that is the same as the input value.
    bool translateDirect(const Input& in, Input& out, bool intermediate = false);

    /// Generates an output value that is the same as the input value but extracts a single channel
    bool translateToSingle(const std::string& name,
                           const Input& in,
                           Input& out,
                           bool intermediate = false);

    /// Generates an output value that is the same as the input value but extracts a single channel
    bool translateToSingleAffine(const std::string& name,
                                 const Input& in,
                                 float scale,
                                 float bias,
                                 Input& out,
                                 bool intermediate = false);

    /// Generates an output value equal to the input value multiplied by a factor.
    bool translateFactor(const Input& in,
                         const Input& factor,
                         Input& out,
                         bool intermediate = false);

    /// Generates an output value equal to the per-channel product of two inputs.
    /// If \p linearize is true, image samples are converted from sRGB to linear before
    /// multiplication and the result is converted back to sRGB. Has no effect on constant
    /// (non-image) inputs, which are always treated as linear.
    bool translateProduct(const std::string& name,
                          const Input& in,
                          const Input& factor,
                          Input& out,
                          bool intermediate = false,
                          bool linearize = false);

    /// Generates an output value equal to the per-channel maximum of two inputs.
    bool translateMax(const std::string& name,
                      const Input& in0,
                      const Input& in1,
                      Input& out,
                      bool intermediate = false);

    /// Generates an output value equal to the linear interpolation between two inputs using a
    /// single-channel mask. out = in0 * (1 - mask) + in1 * mask
    /// If \p linearize is true, image samples from in0 and in1 are converted from sRGB to linear
    /// before interpolation and the result is converted back to sRGB. The mask is never linearized
    /// since it is always a scalar weight. Has no effect on constant (non-image) inputs, which are
    /// always treated as linear.
    bool translateLerp(const std::string& name,
                       const Input& in0,
                       const Input& in1,
                       const Input& mask,
                       Input& out,
                       bool intermediate = false,
                       bool linearize = false);

    /// Generates an output value equal to the scaled and biased input value.
    bool translateAffine(const std::string& name,
                         const Input& in,
                         float scale,
                         float bias,
                         Input& out,
                         bool intermediate = false);

    bool extractChannel(const std::string& name,
                        const Input& in,
                        int channelIndex,
                        float scale,
                        float bias,
                        Input& out,
                        bool intermediate = false);

    /// Generates PBR output values based on phong input values.
    bool translatePhong2PBR(const Input& diffuseIn,
                            const Input& specularIn,
                            const Input& glosinessIn,
                            Input& diffuseOut,
                            Input& metallicOut,
                            Input& roughnessOut);

    /// Computes only the roughness component of a Phong-to-PBR conversion, leaving metallic
    /// untouched. Use this when the caller provides an explicit metallic/reflectionFactor value
    /// and only needs shininess converted to PBR roughness.
    bool translatePhong2Roughness(const Input& specularIn,
                                  const Input& shininessIn,
                                  Input& roughnessOut);

    /// Generates a normal output value that is the same as the normal input value if present,
    /// or base on a bump input value otherwise.
    bool translateNormals(const Input& bumpIn, const Input& normalsIn, Input& normalsOut);

    /// Generates a transparency output value based on an opacity input value.
    bool translateTransparency2Opacity(const Input& transparency, Input& opacity);

    /// Generates an opacity output value based on a transparency input value.
    bool translateOpacity2Transparency(const Input& opacity, Input& transparency);

    /// Generates an ambient output value based on an occlusion input value.
    bool translateAmbient2Occlusion(const Input& ambient, Input& occlusion);

    /// Generates an output value by converting a multiscatter albedo input to a single-scatter
    /// albedo input.
    bool translateMultiscatterToSingleScatter(const std::string& name,
                                              const Input& in,
                                              float anisotropy,
                                              Input& out,
                                              bool intermediate = false);

    /// Generates an output value by converting a single-scatter albedo input to a multiscatter
    /// albedo input.
    bool translateSingleScatterToMultiscatter(const std::string& name,
                                              const Input& in,
                                              float anisotropy,
                                              Input& out,
                                              bool intermediate = false);

    /// Generates an output value that is a mix from 4 input values. If those values are from a
    /// single image in the same order, name is not used, and instead the result will be identical
    /// to calling translateDirect.
    bool translateMix(const std::string& name,
                      const PXR_NS::TfToken& colorspace,
                      const Input& in0,
                      const Input& in1,
                      const Input& in2,
                      const Input& in3,
                      Input& out);

    /// Returns an output value taking 1 channel from a 3-channel input value.
    /// Useful in combination with translateMix.
    Input split3f(const Input& input, int channel);

    /// Compute the range of pixel values for an input
    /// If the input is a constant value, that value is returned
    std::pair<PXR_NS::GfVec4f, PXR_NS::GfVec4f> computeRange(const Input& input);

    /// Get the i-th output image.
    const ImageAsset& getImage(int i) const;

    /// Get the output images.
    std::vector<ImageAsset>& getImages();

    /// Get the name of an image source
    std::string getImageSourceName(int index) const;

    // First term is false if image couldn't be decoded
    std::pair<bool, Image&> getDecodedImage(int index);

    int addImage(Image&& image,
                 const std::string& assetName,
                 const std::string& assetUri,
                 ImageFormat format,
                 bool intermediate = false);

private:
    std::string mDebugTag;
    bool mExportImages;
    std::unordered_map<std::string, int> mCache;
    std::vector<ImageAsset> mImagesSrc;
    std::vector<Image> mDecodedImages;
    std::vector<bool> mDecodedMap;
    std::vector<ImageAsset> mImagesDst;

    int addImage(ImageAsset&& image);

    // Translate an input value directly to an output value. Helper function can be reused by
    // different translate functions regardless of which Input objects those take
    void translateDirectInternal(int imageIdx, Input& out);

    /// Describes one input slot for _applyImageOp.
    struct _ImageOpSlot
    {
        const Input* input = nullptr; ///< Input to sample or evaluate as a constant.
        int channels = 0;             ///< Number of output channels to fill for this slot.
        bool linearize = false;       ///< If true, convert sRGB→linear after sampling.
    };

    /// Common implementation for multi-input image operations (product, max, lerp, …).
    /// Handles cache lookup, image decode, UV transform, pixel loop, and output setup.
    /// @p pixelFn is called per pixel as pixelFn(bufs, outPixel, outChannels), where bufs[i]
    /// points to the slot.channels float values sampled or evaluated for slot i.
    template<typename PixelFn>
    bool _applyImageOp(const std::string& name,
                       const std::string& opTag,
                       const std::string& extraKeySuffix,
                       std::initializer_list<_ImageOpSlot> slots,
                       int outChannels,
                       bool intermediate,
                       Input& out,
                       PixelFn pixelFn);
};

// Map channel index to USD channel token
USDFFUTILS_API const PXR_NS::TfToken&
channel2Token(int channel);

// Map USD channel token to channel index
USDFFUTILS_API int
token2Channel(const PXR_NS::TfToken& token);

// Van de Hulst approximation for converting between single-scattering albedo (as used by
// OpenPBR's transmission_scatter / subsurface_color) and multiple-scattering albedo (as used
// by KHR_materials_volume_scatter's multiscatterColorFactor).
//
// The forward approximation is:
//   s  = sqrt((1 - alpha) / (1 - alpha * g))
//   C  = (1 - s)(1 - 0.139 * s) / (1 + 1.17 * s)
// where alpha is the single-scatter albedo, g is the anisotropy, and C is the
// multiple-scatter (diffuse) albedo.  Analytically inverting this for alpha yields the
// polynomial form in multiscatterToSingleScatter().
//
// Reference:
//   Christopher Kulla and Alejandro Conty Estevez, "Revisiting Physically Based Shading
//   at Imageworks", ACM SIGGRAPH 2017 Course: Physically Based Shading in Theory and
//   Practice (2017).
//   https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
//
// The underlying approximation originates from:
//   H. C. van de Hulst, "Multiple Light Scattering", Academic Press (1980).
USDFFUTILS_API float
singleScatterToMultiscatter(float singleScatter, float anisotropy);

USDFFUTILS_API PXR_NS::GfVec3f
singleScatterToMultiscatter(const PXR_NS::GfVec3f& singleScatter, float anisotropy);

USDFFUTILS_API float
multiscatterToSingleScatter(float multiscatter, float anisotropy);

USDFFUTILS_API PXR_NS::GfVec3f
multiscatterToSingleScatter(const PXR_NS::GfVec3f& multiscatter, float anisotropy);
}
