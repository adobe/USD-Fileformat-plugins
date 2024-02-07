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
#include "layerWriteMaterialX.h"

#include "common.h"
#include "debugCodes.h"
#include "sdfMaterialUtils.h"
#include "sdfUtils.h"

#include <pxr/usd/usdShade/tokens.h>

using namespace PXR_NS;

namespace adobe::usd {

SdfPath
_createMaterialXUvReader(SdfAbstractData* sdfData,
                         const SdfPath& materialPath,
                         const SdfPath& parentPath)
{
    // XXX The MaterialX texcoord reader function has an index to specify which set of UV
    // coordinates to read, but it does not have the ability to specify a primvar by name. So we
    // currently default to the first set, but there is something to be figured out about how to
    // connect a named primvar to a UV coordinate index in MaterialX.
    // Maybe ND_geompropvalue_vector2 with geomprop="st" will do the trick. Note, that the shared
    // stPrimvarNameAttrName input attribute is of type Token, but `geomprop` is of type String
    return createShader(
      sdfData, parentPath, AdobeTokens->texCoordReader, MtlXTokens->ND_texcoord_vector2, "out");
}

// If a texture coordinate transform is needed for the given input a transform will be created and
// the result output path will be returned. Otherwise it will forward the default ST reader result
// path.
SdfPath
_createMaterialXUvTransform(SdfAbstractData* sdfData,
                            const SdfPath& parentPath,
                            const std::string& name,
                            const Input& input,
                            const SdfPath& uvReaderResultPath)
{
    if (input.transformRotation.IsEmpty() && input.transformScale.IsEmpty() &&
        input.transformTranslation.IsEmpty()) {
        return uvReaderResultPath;
    }

    // For the place2d node, the scale is not a multiplier, but the overall scale and so we need to
    // invert the value
    VtValue scale;
    if (input.transformScale.IsHolding<GfVec2f>()) {
        GfVec2f s = input.transformScale.UncheckedGet<GfVec2f>();
        s[0] = s[0] != 0.0f ? 1.0f / s[0] : 0.0f;
        s[1] = s[1] != 0.0f ? 1.0f / s[1] : 0.0f;
        scale = s;
    }

    // Create UV transform by applying scale, rotation and transform, in that order
    // This matches what the UsdTransform2d node does
    return createShader(sdfData,
                        parentPath,
                        TfToken(name + "_uv_transform"),
                        MtlXTokens->ND_place2d_vector2,
                        "out",
                        { { "scale", scale },
                          { "rotate", input.transformRotation },
                          { "offset", input.transformTranslation } },
                        { { "texcoord", uvReaderResultPath } });
    ;
}

std::string
_toMaterialXAddressMode(const TfToken& wrapMode)
{
    if (wrapMode == AdobeTokens->repeat) {
        return "periodic";
    } else if (wrapMode == AdobeTokens->clamp) {
        return "clamp";
    } else if (wrapMode == AdobeTokens->mirror) {
        return "mirror";
    } else if (wrapMode == AdobeTokens->black) {
        return "constant";
    } else {
        TF_WARN("Unknown wrapMode '%s'", wrapMode.GetText());
        return "periodic";
    }
}

SdfPath
_createScaleAndBiasNodes(SdfAbstractData* sdfData,
                         const SdfPath& parentPath,
                         const std::string& baseName,
                         const SdfPath& textureInput,
                         int numChannels,
                         bool isColor,
                         const GfVec4f& scale4,
                         const GfVec4f& bias4)
{
    TfToken scaleShaderType, biasShaderType;
    VtValue scale, bias;
    if (numChannels == 1) {
        float s = scale4[0];
        if (s != 1.0f) {
            scale = s;
            scaleShaderType = MtlXTokens->ND_multiply_float;
        }
        float b = bias4[0];
        if (b != 0.0f) {
            bias = b;
            biasShaderType = MtlXTokens->ND_add_float;
        }
    } else if (numChannels == 3) {
        GfVec3f s = GfVec3f(scale4[0], scale4[1], scale4[2]);
        if (s != GfVec3f(1.0f)) {
            scale = s;
            scaleShaderType =
              isColor ? MtlXTokens->ND_multiply_color3 : MtlXTokens->ND_multiply_vector3;
        }
        GfVec3f b = GfVec3f(bias4[0], bias4[1], bias4[2]);
        if (b != GfVec3f(0.0f)) {
            bias = b;
            biasShaderType = isColor ? MtlXTokens->ND_add_color3 : MtlXTokens->ND_add_vector3;
        }
    }

    SdfPath textureOutput = textureInput;
    if (!scale.IsEmpty()) {
        textureOutput = createShader(sdfData,
                                     parentPath,
                                     TfToken(baseName + "_scale"),
                                     scaleShaderType,
                                     "out",
                                     { { "in1", scale } },
                                     { { "in2", textureOutput } });
    }
    if (!bias.IsEmpty()) {
        textureOutput = createShader(sdfData,
                                     parentPath,
                                     TfToken(baseName + "_bias"),
                                     biasShaderType,
                                     "out",
                                     { { "in1", bias } },
                                     { { "in2", textureOutput } });
    }

    return textureOutput;
}

SdfPath
_createMaterialXTextureReader(SdfAbstractData* sdfData,
                              const SdfPath& parentPath,
                              const TfToken& name,
                              const Input& input,
                              const SdfPath& uvResultPath,
                              const std::string& texturePath,
                              bool isNormalMap,
                              bool convertToColor)
{
    int numChannels = input.numChannels();
    TfToken shaderType;
    VtValue defaultValue;
    if (numChannels == 1) {
        // If we want to extract a single channel we read the RGBA version of the texture in linear
        // color space.
        shaderType = MtlXTokens->ND_image_vector4;
        if (input.value.IsHolding<float>()) {
            // We're always using a RGBA texture reader (ND_image_vector4), so the fallback value
            // has to match, even if we only care about a single channel.
            float f = input.value.UncheckedGet<float>();
            defaultValue = GfVec4f(f);
        }
    } else if (numChannels == 3) {
        // We differentiate between two types of texture readers depending on the type of input on
        // the surface shader. A mismatch in types will lead to errors.
        if (name == OpenPbrTokens->geometry_normal || name == OpenPbrTokens->geometry_coat_normal ||
            name == OpenPbrTokens->geometry_tangent) {
            shaderType = MtlXTokens->ND_image_vector3;
        } else {
            shaderType = MtlXTokens->ND_image_color3;
        }
        if (input.value.IsHolding<GfVec3f>()) {
            defaultValue = input.value;
        }
    } else {
        TF_CODING_ERROR(
          "Unsupported texture type for %d channels on input %s", numChannels, name.GetText());
        return SdfPath();
    }

    // In MaterialX, each input attribute on a node can have an associated color space. We
    // explicitly mark the "file" input with a color space if we know that we got a sRGB texture.
    // Note, this will become the "colorSpace" metadata on the input attribute.
    InputColorSpaces inputColorSpaces;
    if (input.colorspace == AdobeTokens->sRGB) {
        inputColorSpaces["file"] = MtlXTokens->srgb_texture;
    }

    // Note, we're setting the texture path directly on this texture reader, which means the
    // path is duplicated on each texture reader of the same texture for each of the different
    // sub networks. This is currently needed since some software is not correctly following
    // connections to resolve input values.
    // Once that has improved in the ecosystem we could author the asset path once as an
    // attribute on the material and connect all corresponding texture readers to that attribute
    // value.
    SdfPath textureOutput =
      createShader(sdfData,
                   parentPath,
                   name,
                   shaderType,
                   "out",
                   { { "file", SdfAssetPath(texturePath) },
                     { "default", defaultValue },
                     { "uaddressmode", _toMaterialXAddressMode(input.wrapS) },
                     { "vaddressmode", _toMaterialXAddressMode(input.wrapT) } },
                   { { "texcoord", uvResultPath } },
                   inputColorSpaces);

    // Extract the single channel from the 4 channel reader
    if (numChannels == 1) {
        std::string out = input.channel == AdobeTokens->r   ? "outx"
                          : input.channel == AdobeTokens->g ? "outy"
                          : input.channel == AdobeTokens->b ? "outz"
                                                            : "outw";
        textureOutput = createShader(sdfData,
                                     parentPath,
                                     TfToken(name.GetString() + "_to_float"),
                                     MtlXTokens->ND_separate4_vector4,
                                     out,
                                     {},
                                     { { "in", textureOutput } });
    }

    if (isNormalMap) {
        // The texture reader for a normal map reads a texture map in tangent space, which needs to
        // be transformed into world space. Route normal map through a normal map node
        // Note, we skip the usual scale and bias of 2 and -1 for the normal map data and send the
        // data directly into the normalmap node.
        textureOutput = createShader(sdfData,
                                     parentPath,
                                     TfToken(name.GetString() + "_to_world_space"),
                                     MtlXTokens->ND_normalmap,
                                     "out",
                                     {},
                                     { { "in", textureOutput } });
    } else {
        if (!input.scale.IsEmpty() || !input.bias.IsEmpty()) {
            GfVec4f scale4 = input.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
            GfVec4f bias4 = input.bias.GetWithDefault<GfVec4f>(GfVec4f(0.0f));
            bool isColor = shaderType == MtlXTokens->ND_image_color3;
            textureOutput = _createScaleAndBiasNodes(sdfData,
                                                     parentPath,
                                                     name.GetString(),
                                                     textureOutput,
                                                     numChannels,
                                                     isColor,
                                                     scale4,
                                                     bias4);
        }
    }

    if (convertToColor && numChannels == 1) {
        textureOutput = createShader(sdfData,
                                     parentPath,
                                     TfToken(name.GetString() + "_to_color"),
                                     MtlXTokens->ND_convert_float_color3,
                                     "out",
                                     {},
                                     { { "in", textureOutput } });
    }

    return textureOutput;
}

void
_setupMaterialXInput(WriteSdfContext& ctx,
                     const SdfPath& materialPath,
                     const SdfPath& parentPath,
                     const TfToken& name,
                     const Input& input,
                     SdfPath& uvReaderResultPath,
                     InputValues& inputValues,
                     InputConnections& inputConnections)
{
    if (input.image >= 0) {
        if (input.isZeroTexture()) {
            inputValues.emplace_back(name.GetString(), getTextureZeroVtValue(input.channel));
        } else {
            if ((size_t)input.image >= ctx.usdData->images.size()) {
                TF_CODING_ERROR("Image index %d for %s is larger than images array %zu",
                                input.image,
                                name.GetText(),
                                ctx.usdData->images.size());
                return;
            }
            std::string texturePath =
              createTexturePath(ctx.srcAssetFilename, ctx.usdData->images[input.image].uri);

            // Create the ST reader on demand when we create the first textured input
            if (uvReaderResultPath.IsEmpty()) {
                uvReaderResultPath =
                  _createMaterialXUvReader(ctx.sdfData, materialPath, parentPath);
            }

            // This creates a ST transform node if needed, otherwise the default ST result path
            // will be returned.
            SdfPath stResultPath = _createMaterialXUvTransform(
              ctx.sdfData, parentPath, name.GetString(), input, uvReaderResultPath);

            bool isNormalMap =
              name == OpenPbrTokens->geometry_normal || name == OpenPbrTokens->geometry_coat_normal;
            // geometry_opacity expects a color, but our input opacity is a float input
            bool convertToColor = name == OpenPbrTokens->geometry_opacity;
            SdfPath texResultPath = _createMaterialXTextureReader(ctx.sdfData,
                                                                  parentPath,
                                                                  name,
                                                                  input,
                                                                  stResultPath,
                                                                  texturePath,
                                                                  isNormalMap,
                                                                  convertToColor);

            inputConnections.emplace_back(name.GetString(), texResultPath);
        }
    } else if (!input.value.IsEmpty()) {
        // Set constant value on the surface shader directly
        if (name == OpenPbrTokens->geometry_opacity) {
            // geometry_opacity expects a color, but our input opacity is a float input
            if (input.value.IsHolding<float>()) {
                float opacity = input.value.UncheckedGet<float>();
                inputValues.emplace_back(name.GetString(), VtValue(GfVec3f(opacity)));
            } else {
                TF_WARN("Expect float value for constant opacity. Got type %s",
                        input.value.GetTypeName().c_str());
            }
        } else {
            inputValues.emplace_back(name.GetString(), input.value);
        }
    }
}

void
writeMaterialX(WriteSdfContext& ctx, const SdfPath& materialPath, const Material& material)
{
    SdfPath p;

    // This will create a NodeGraph parent prim for all the shading nodes in this network
    SdfPath parentPath =
      createPrimSpec(ctx.sdfData, materialPath, MtlXTokens->MaterialX, UsdShadeTokens->NodeGraph);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "layer::write MaterialX network %s\n", parentPath.GetText());

    SdfPath uvReaderResultPath;
    InputValues inputValues;
    InputConnections inputConnections;
    auto writeInput = [&](const TfToken& name, const Input& input) {
        _setupMaterialXInput(ctx,
                             materialPath,
                             parentPath,
                             name,
                             input,
                             uvReaderResultPath,
                             inputValues,
                             inputConnections);
    };

    // OpenPBR spec:
    // https://github.com/AcademySoftwareFoundation/OpenPBR/blob/main/reference/open_pbr_surface.mtlx

    // Currently unused inputs
    // Input useSpecularWorkflow;
    // Input clearcoatSpecular;
    // Input displacement;
    // Input opacityThreshold;
    // Input occlusion;
    // Input thickness;

    // base
    // base_weight (no source info)
    writeInput(OpenPbrTokens->base_color, material.diffuseColor);
    // XXX we're not setting base_roughness? Should we when metallic != 0?
    // "Roughness of the diffuse reflection. Higher values cause the surface to appear flatter."
    // writeInput(OpenPbrTokens->base_roughness, material.roughness);
    writeInput(OpenPbrTokens->base_metalness, material.metallic);

    // specular
    writeInput(OpenPbrTokens->specular_weight, material.specularLevel);
    writeInput(OpenPbrTokens->specular_color, material.specularColor);
    writeInput(OpenPbrTokens->specular_roughness, material.roughness);
    writeInput(OpenPbrTokens->specular_ior, material.ior);
    // specular_ior_level (no source info)
    writeInput(OpenPbrTokens->specular_anisotropy, material.anisotropyLevel);
    // XXX it's unclear if the angle we got for the ASM model works with the OpenPBR rotation
    writeInput(OpenPbrTokens->specular_rotation, material.anisotropyAngle);

    // transmission
    writeInput(OpenPbrTokens->transmission_weight, material.transmission);
    writeInput(OpenPbrTokens->transmission_color, material.absorptionColor);
    writeInput(OpenPbrTokens->transmission_depth, material.absorptionDistance);
    // transmission_scatter (no source info)
    // transmission_scatter_anisotropy (no source info)
    // transmission_dispersion (no source info)

    // subsurface
    if (!material.scatteringColor.isEmpty() || !material.scatteringDistance.isEmpty()) {
        // XXX We currently turn the subsurface fully on if the asset has a scattering color or
        // distance specified
        inputValues.emplace_back("subsurface_weight", 1.0f);
    }
    writeInput(OpenPbrTokens->subsurface_color, material.scatteringColor);
    writeInput(OpenPbrTokens->subsurface_radius, material.scatteringDistance);
    // subsurface_radius_scale (no source info) (maps to ASM scatteringDistanceScale)
    // subsurface_anisotropy (no source info)

    // fuzz
    if (!material.sheenColor.isEmpty()) {
        // XXX We currently turn the fuzz fully on if the asset has a sheen color specified
        inputValues.emplace_back("fuzz_weight", 1.0f);
    }
    writeInput(OpenPbrTokens->fuzz_color, material.sheenColor);
    writeInput(OpenPbrTokens->fuzz_roughness, material.sheenRoughness);

    // coat
    // XXX How does clearcoatSpecular fit into this lobe? coat_ior_level?
    writeInput(OpenPbrTokens->coat_weight, material.clearcoat);
    writeInput(OpenPbrTokens->coat_color, material.clearcoatColor);
    writeInput(OpenPbrTokens->coat_roughness, material.clearcoatRoughness);
    // coat_anisotropy (no source info)
    // coat_rotation (no source info)
    writeInput(OpenPbrTokens->coat_ior, material.clearcoatIor);
    // coat_ior_level (no source info)

    // thin_film
    // thin_film_thickness (no source info)
    // thin_film_ior (no source info)

    // emission
    if (!material.emissiveColor.isEmpty()) {
        // The luminance is currently part of of the `scale` or `value` of the
        // emissiveColor input
        inputValues.emplace_back("emission_luminance", 1.0f);
    }
    writeInput(OpenPbrTokens->emission_color, material.emissiveColor);

    // geometry
    writeInput(OpenPbrTokens->geometry_opacity, material.opacity);
    // geometry_thin_walled (no source info)
    writeInput(OpenPbrTokens->geometry_normal, material.normal);
    writeInput(OpenPbrTokens->geometry_coat_normal, material.clearcoatNormal);
    // geometry_tangent (no source info)

    // Create OpenPBR surface shader
    SdfPath outputPath = createShader(ctx.sdfData,
                                      parentPath,
                                      MtlXTokens->MaterialX,
                                      MtlXTokens->ND_open_pbr_surface_surfaceshader,
                                      "out",
                                      inputValues,
                                      inputConnections);
    createShaderOutput(
      ctx.sdfData, materialPath, "mtlx:surface", SdfValueTypeNames->Token, outputPath);
}
}
