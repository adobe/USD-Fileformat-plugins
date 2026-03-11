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
#include <fileformatutils/layerWriteOpenPBR.h>

#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>
#include <fileformatutils/sdfMaterialUtils.h>
#include <fileformatutils/sdfUtils.h>

#include <pxr/usd/usdShade/tokens.h>

using namespace PXR_NS;

namespace adobe::usd {

const std::string stPrimvarNameAttrName = "stPrimvarName";

SdfPath
_createMaterialXUvReader(SdfAbstractData* sdfData, const SdfPath& parentPath, int uvIndex)
{
    // Map the index to a unique name for the texture uv coordinate reader and map the index to
    // one of st, st1, st2, ... for use as the geomprop value.
    return createShader(sdfData,
                        parentPath,
                        getSTTexCoordReaderToken(uvIndex),
                        MtlXTokens->ND_geompropvalue_vector2,
                        "out",
                        { { "geomprop", getSTPrimvarAttrToken(uvIndex).GetString() } },
                        {});
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
    if (input.hasDefaultTransform()) {
        return uvReaderResultPath;
    }

    // For the place2d node, the scale is not a multiplier, but the overall scale and so we need to
    // invert the value
    GfVec2f scale = input.uvScale;
    scale[0] = scale[0] != 0.0f ? 1.0f / scale[0] : 0.0f;
    scale[1] = scale[1] != 0.0f ? 1.0f / scale[1] : 0.0f;

    // Create UV transform by applying scale, rotation and transform, in that order
    // This matches what the UsdTransform2d node does
    return createShader(
      sdfData,
      parentPath,
      TfToken(name + "_uv_transform"),
      MtlXTokens->ND_place2d_vector2,
      "out",
      { { "scale", scale }, { "rotate", input.uvRotation }, { "offset", input.uvTranslation } },
      { { "texcoord", uvReaderResultPath } });
}

std::string
_toMaterialXAddressMode(const TfToken& wrapMode)
{
    if (wrapMode.IsEmpty()) {
        return "periodic";
    } else if (wrapMode == AdobeTokens->repeat) {
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
createMaterialXTextureReader(SdfAbstractData* sdfData,
                             const SdfPath& parentPath,
                             const TfToken& name,
                             const Input& input,
                             const SdfPath& uvResultPath,
                             const SdfPath& textureConnection)
{
    // Normal and tangent map textures need a bit of special processing in MaterialX
    const bool isNormalMap =
      name == OpenPbrTokens->geometry_normal || name == OpenPbrTokens->geometry_coat_normal;
    const bool isTangentMap =
      name == OpenPbrTokens->geometry_tangent || name == OpenPbrTokens->geometry_coat_tangent;

    // Most texture inputs are for color and single float inputs on the OpenPBR surface shader
    // and these inputs need to support channel selection from packed RGBA texture and also
    // scale & bias support to adjust the read texel values. That is why we're using a
    // ND_UsdUVTexture_23 shader, which was designed to emulate the UsdUVTexture node that we've
    // been using before.
    TfToken outputName = input.channel;
    static const GfVec4f defaultFallback(0.0f, 0.0f, 0.0f, 1.0f);
    GfVec4f fallback = defaultFallback;
    if (outputName == AdobeTokens->r) {
        if (input.value.IsHolding<float>()) {
            fallback[0] = input.value.UncheckedGet<float>();
        }
    } else if (outputName == AdobeTokens->g) {
        if (input.value.IsHolding<float>()) {
            fallback[1] = input.value.UncheckedGet<float>();
        }
    } else if (outputName == AdobeTokens->b) {
        if (input.value.IsHolding<float>()) {
            fallback[2] = input.value.UncheckedGet<float>();
        }
    } else if (outputName == AdobeTokens->a) {
        if (input.value.IsHolding<float>()) {
            fallback[3] = input.value.UncheckedGet<float>();
        }
    } else if (outputName == AdobeTokens->rgb) {
        if (input.value.IsHolding<GfVec3f>()) {
            const GfVec3f& vec3 = input.value.UncheckedGet<GfVec3f>();
            fallback = GfVec4f(vec3[0], vec3[1], vec3[2], 1.0f);
        }
    } else {
        TF_CODING_ERROR("Unsupported texture type for channel %s on input %s",
                        outputName.GetText(),
                        name.GetText());
        return SdfPath();
    }

    // In MaterialX, each input attribute on a node can have an associated color space. We
    // explicitly mark the "file" input with a color space if we know that we got a sRGB
    // texture. Note, this will become the "colorSpace" metadata on the input attribute.
    InputColorSpaces inputColorSpaces;
    if (input.colorspace == AdobeTokens->sRGB) {
        inputColorSpaces["file"] = MtlXTokens->srgb_texture;
    }

    // The inputs on the node are called wrapS and wrapT, but are using string values like the
    // uaddressmode and vaddressmode on other MaterialX texture nodes.
    InputValues inputValues = { { "wrapS", _toMaterialXAddressMode(input.wrapS) },
                                { "wrapT", _toMaterialXAddressMode(input.wrapT) } };
    if (fallback != defaultFallback) {
        inputValues.emplace_back("fallback", fallback);
    }

    GfVec4f scale = input.scale;
    GfVec4f bias = input.bias;
    if (isNormalMap) {
        // In MaterialX, the ND_normalmap node, which is downstream of the ND_UsdUVTexture_23 will
        // decode the normal from the raw texture value, assuming the OpenGL convention, using a
        // scale and bias of 2 (kOpenGLNormalTexScale) and -1 (kOpenGLNormalTexBias). That means it
        // would be redundant to have this on the ND_UsdUVTexture_23 node.
        //
        // We have these decoding scale and bias values in our Input struct, especially if we're
        // trying to differentiate it from a DirectX encoded normalmap and/or a normal strength
        // multiplier. So we apply the inverse affine transform using the OpenGL decoding values,
        // which yields a scale of 1 and a bias of 0, if it was indeed the OpenGL convention. In the
        // case of something else it will yield a transformation to something that can be decoding
        // with the OpenGL convention. Thus we can represent DirectX encoding and multipliers.
        //
        // Note that this mirrors the process in the OpenPBR reading code.
        scale = GfCompDiv(scale, kOpenGLNormalTexScale);
        bias = GfCompDiv(bias - kOpenGLNormalTexBias, kOpenGLNormalTexScale);
    }
    if (scale != kDefaultTexScale) {
        inputValues.emplace_back("scale", scale);
    }
    if (bias != kDefaultTexBias) {
        inputValues.emplace_back("bias", bias);
    }

    InputConnections inputConnections = { { "st", uvResultPath }, { "file", textureConnection } };

    SdfPath textureOutput = createShader(sdfData,
                                         parentPath,
                                         name,
                                         MtlXTokens->ND_UsdUVTexture_23,
                                         outputName.GetString(),
                                         inputValues,
                                         inputConnections,
                                         inputColorSpaces);

    if (isNormalMap || isTangentMap) {
        // The rgb output of the ND_UsdUVTexture_23 is of type color3, but the ND_normalmap node
        // for normal maps and the tangent map input on the surface require vector3. So we inject a
        // simple type conversion node for correctness.
        textureOutput = createShader(sdfData,
                                     parentPath,
                                     TfToken(name.GetString() + "_as_vector"),
                                     MtlXTokens->ND_convert_color3_vector3,
                                     "out",
                                     {},
                                     { { "in", textureOutput } });
    }

    if (isNormalMap) {
        // The texture reader for a normal map reads a texture map in tangent space, which needs
        // to be transformed into world space. Route normal map through a normal map node.
        textureOutput = createShader(sdfData,
                                     parentPath,
                                     TfToken(name.GetString() + "_to_world_space"),
                                     MtlXTokens->ND_normalmap,
                                     "out",
                                     {},
                                     { { "in", textureOutput } });
    }

    return textureOutput;
}

void
_setupOpenPbrInput(WriteSdfContext& ctx,
                   const SdfPath& materialPath,
                   const SdfPath& parentPath,
                   const TfToken& name,
                   const Input& input,
                   std::unordered_map<int, SdfPath>& uvReaderResultPathMap,
                   InputValues& inputValues,
                   InputConnections& inputConnections,
                   const InputToMaterialInputTypeMap& inputRemapping,
                   MaterialInputs& materialInputs)
{
    auto remappingIt = inputRemapping.find(name);
    bool hasMapping = remappingIt != inputRemapping.cend();
    if (!hasMapping) {
        TF_CODING_ERROR("Expecting to find remapping for shader input '%s'", name.GetText());
        return;
    }

    const TfToken& materialInputName = remappingIt->second.name;
    const SdfValueTypeName& inputType = remappingIt->second.type;

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

            bool isColorTexture = inputType == SdfValueTypeNames->Color3f;
            SdfPath textureConnection = addMaterialInputTexture(ctx.sdfData,
                                                                materialPath,
                                                                materialInputName,
                                                                texturePath,
                                                                isColorTexture,
                                                                materialInputs);

            // Create the ST reader on demand when we create the first textured input
            SdfPath uvReaderResultPath;
            auto it = uvReaderResultPathMap.find(input.uvIndex);
            if (it == uvReaderResultPathMap.end()) {
                uvReaderResultPath =
                  _createMaterialXUvReader(ctx.sdfData, parentPath, input.uvIndex);
                uvReaderResultPathMap[input.uvIndex] = uvReaderResultPath;
            } else {
                uvReaderResultPath = it->second;
            }

            // This creates a ST transform node if needed, otherwise the default ST result path
            // will be returned.
            SdfPath stResultPath = _createMaterialXUvTransform(
              ctx.sdfData, parentPath, name.GetString(), input, uvReaderResultPath);

            SdfPath texResultPath = createMaterialXTextureReader(
              ctx.sdfData, parentPath, name, input, stResultPath, textureConnection);

            inputConnections.emplace_back(name.GetString(), texResultPath);
        }
    } else if (!input.value.IsEmpty()) {
        if (!materialInputName.IsEmpty()) {
            // Set constant value on material input and connect surface shader to that input
            SdfPath connection = addMaterialInputValue(
              ctx.sdfData, materialPath, materialInputName, inputType, input.value, materialInputs);
            inputConnections.emplace_back(name.GetString(), connection);
            const MinMaxVtValuePair* range =
              ShaderRegistry::getInstance().getMaterialInputRange(materialInputName);
            if (range) {
                setRangeMetadata(ctx.sdfData, connection, *range);
            }
        } else {
            // If the input name is not valid, then just set the value
            inputValues.emplace_back(name.GetString(), input.value);
        }
    }
}

void
writeOpenPBR(WriteSdfContext& ctx,
             const SdfPath& materialPath,
             const OpenPbrMaterial& material,
             MaterialInputs& materialInputs)
{
    SdfPath p;

    // This will create a NodeGraph parent prim for all the shading nodes in this network
    SdfPath parentPath =
      createPrimSpec(ctx.sdfData, materialPath, MtlXTokens->OpenPBR, UsdShadeTokens->NodeGraph);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL, "layer::write MaterialX network %s\n", parentPath.GetText());

    InputValues inputValues;
    InputConnections inputConnections;
    std::unordered_map<int, SdfPath> uvReaderResultPathMap;
    const InputToMaterialInputTypeMap& remapping =
      ShaderRegistry::getInstance().getOpenPbrInputRemapping();
    auto writeInput = [&](const TfToken& name, const Input& input) {
        if (!input.isEmpty())
            _setupOpenPbrInput(ctx,
                               materialPath,
                               parentPath,
                               name,
                               input,
                               uvReaderResultPathMap,
                               inputValues,
                               inputConnections,
                               remapping,
                               materialInputs);
    };

#define INPUT(x) writeInput(OpenPbrTokens->x, material.x)
    INPUT(base_weight); // has no UsdPreviewSurface or ASM equivalent
    INPUT(base_color);
    INPUT(base_diffuse_roughness);
    INPUT(base_metalness);
    INPUT(specular_weight);
    INPUT(specular_color);
    INPUT(specular_roughness);
    INPUT(specular_ior);
    INPUT(specular_roughness_anisotropy);
    INPUT(transmission_weight);
    INPUT(transmission_color);
    INPUT(transmission_depth);
    INPUT(transmission_scatter);
    INPUT(transmission_scatter_anisotropy);
    INPUT(transmission_dispersion_scale);
    INPUT(transmission_dispersion_abbe_number);
    INPUT(subsurface_weight);
    INPUT(subsurface_color);
    INPUT(subsurface_radius);
    INPUT(subsurface_radius_scale);
    INPUT(subsurface_scatter_anisotropy);
    INPUT(fuzz_weight);
    INPUT(fuzz_color);
    INPUT(fuzz_roughness);
    INPUT(coat_weight);
    INPUT(coat_color);
    INPUT(coat_roughness);
    INPUT(coat_roughness_anisotropy);
    INPUT(coat_ior);
    INPUT(coat_darkening);
    INPUT(thin_film_weight);
    INPUT(thin_film_thickness);
    INPUT(thin_film_ior);
    INPUT(emission_luminance);
    INPUT(emission_color);
    INPUT(geometry_opacity);
    INPUT(geometry_thin_walled);
    INPUT(geometry_normal);
    INPUT(geometry_coat_normal);
    INPUT(geometry_tangent);
    INPUT(geometry_coat_tangent);

    // We handle ambient occlusion for OpenPBR with a custom shader graph created below (if
    // necessary)
    writeInput(UsdPreviewSurfaceTokens->occlusion, material.occlusion);
#undef INPUT

    // Non-OpenPBR inputs
    // When in transcoding mode (preserveExtraMaterialInfo=true) we write these fields to not loose
    // any information when reading the USD data again and exporting to a format that supports these
    // inputs.
    // When not transcoding we do not write them, since a MaterialX / OpenPBR renderer will not use
    // these fields and might even fail to validate.
    if (ctx.options->preserveExtraMaterialInfo) {
        // TODO turn into proper displacement setup
        writeInput(UsdPreviewSurfaceTokens->displacement, material.displacement);
        writeInput(AsmTokens->anisotropyAngle, material.anisotropyAngle);
        writeInput(AsmTokens->coatSpecularLevel, material.coatSpecularLevel);
        writeInput(AsmTokens->volumeThickness, material.volumeThickness);
    }

    // first check if we have connections for both base_color and occlusion
    auto baseColorIt =
      std::find_if(inputConnections.begin(), inputConnections.end(), [&](const auto& p) {
          return p.first == OpenPbrTokens->base_color.GetString();
      });
    auto occlusionIt =
      std::find_if(inputConnections.begin(), inputConnections.end(), [&](const auto& p) {
          return p.first == UsdPreviewSurfaceTokens->occlusion.GetString();
      });

    // If there are connections to base_color and occlusion to be added to the OpenPBR shader, we
    // generate an OpenPBR subgraph that feeds the base_color and occlusion
    // inputs to an ND_mix_color3 shader node and then connect the ND_mix_color3 output to the
    // OpenPBR base_color input. The occlusion input is first converted from a float to a color3
    // with the ND_convert_float_color3 shader node.
    if (baseColorIt != inputConnections.end() && occlusionIt != inputConnections.end()) {
        // capture the input paths for the base_color and occlusion connections
        SdfPath baseColorConnection = baseColorIt->second;
        SdfPath occlusionConnection = occlusionIt->second;

        // Remove both the base_color and occlusion connections. We'll create a new base_color
        // connection below where an ND_mix_color3 node will be created to combine the base_color
        // and occlusion sources which will then be input source for the OpenPBR base_color input.
        inputConnections.erase(baseColorIt);
        occlusionIt =
          std::find_if(inputConnections.begin(), inputConnections.end(), [&](const auto& p) {
              return p.first == UsdPreviewSurfaceTokens->occlusion.GetString();
          });
        if (occlusionIt != inputConnections.end())
            inputConnections.erase(occlusionIt);

        // convert ambientOcclusion float to color3
        SdfPath occlusionColorOutput = createShader(ctx.sdfData,
                                                    parentPath,
                                                    AdobeTokens->AmbientOcclusionAsColor,
                                                    MtlXTokens->ND_convert_float_color3,
                                                    "out",
                                                    {},
                                                    { { "in", occlusionConnection } });

        // Provide base_color and occlusion color as inputs to the ND_mix_color3 node.
        // Note: We use a fixed value of 0.0 for the "mix" input which means that the "bg" input is
        // connected to the base color source and "fg" is connected to the ambient occlusion source.
        SdfPath ambientOcclusionBaseColor =
          createShader(ctx.sdfData,
                       parentPath,
                       AdobeTokens->AmbientOcclusionBaseColor,
                       MtlXTokens->ND_mix_color3,
                       "out",
                       { { "mix", 0.0f } },
                       { { "bg", baseColorConnection }, { "fg", occlusionColorOutput } });

        // add the connection from the ND_mix_color3 output to the OpenPBR base_color input
        inputConnections.emplace_back("base_color", ambientOcclusionBaseColor);
    }

    // Create OpenPBR surface shader
    SdfPath outputPath = createShader(ctx.sdfData,
                                      parentPath,
                                      MtlXTokens->OpenPBR,
                                      MtlXTokens->ND_open_pbr_surface_surfaceshader,
                                      "out",
                                      inputValues,
                                      inputConnections);
    createShaderOutput(
      ctx.sdfData, materialPath, "mtlx:surface", SdfValueTypeNames->Token, outputPath);

    SdfPath surfaceShaderPath = parentPath.AppendChild(MtlXTokens->OpenPBR);
    createExtraConstantAttribute(ctx.sdfData, material, surfaceShaderPath);

    // TODO: create displacement setup
}
}
