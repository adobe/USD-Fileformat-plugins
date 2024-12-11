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

    /// Generates a normal output value that is the same as the normal input value if present,
    /// or base on a bump input value otherwise.
    bool translateNormals(const Input& bumpIn, const Input& normalsIn, Input& normalsOut);

    /// Generates a transparency output value based on an opacity input value.
    bool translateTransparency2Opacity(const Input& transparency, Input& opacity);

    /// Generates an opacity output value based on a transparency input value.
    bool translateOpacity2Transparency(const Input& opacity, Input& transparency);

    /// Generates an ambient output value based on an occlusion input value.
    bool translateAmbient2Occlusion(const Input& ambient, Input& occlusion);

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
};

// Map channel index to USD channel token
USDFFUTILS_API const PXR_NS::TfToken&
channel2Token(int channel);

// Map USD channel token to channel index
USDFFUTILS_API int
token2Channel(const PXR_NS::TfToken& token);

}