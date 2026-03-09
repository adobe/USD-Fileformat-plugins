/*
Copyright 2025 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include <fileformatutils/layerReadMaterial.h>
#include <fileformatutils/layerReadMaterialUtils.h>
#include <fileformatutils/layerWriteShared.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd {

// The following helpers read special inputs as attributes. These are used to read values that are
// not native shader inputs on some of the surface shaders.

// Natively supported on ASM, but not OpenPBR
// Note used by UsdPreviewSurface networks
float
_readNormalScale(const UsdShadeShader& surface)
{
    float value = 1.0f;
    surface.GetPrim().GetAttribute(AsmTokens->normalScale).Get(&value);
    return value;
}

// Natively supported on UsdPreviewSurface, but not ASM and OpenPBR
bool
_readUseSpecularWorkflow(const UsdShadeShader& surface)
{
    bool value = false;
    surface.GetPrim().GetAttribute(UsdPreviewSurfaceTokens->useSpecularWorkflow).Get(&value);
    return value;
}

// Natively supported on UsdPreviewSurface, but not ASM and OpenPBR
float
_readOpacityThreshold(const UsdShadeShader& surface)
{
    float value = 0.0f;
    surface.GetPrim().GetAttribute(UsdPreviewSurfaceTokens->opacityThreshold).Get(&value);
    return value;
}

// Custom attribute not natively supported by any surface
// Note used by UsdPreviewSurface networks
bool
_readClearcoatModelsTransmissionTint(const UsdShadeShader& surface)
{
    bool value = false;
    surface.GetPrim().GetAttribute(AdobeTokens->clearcoatModelsTransmissionTint).Get(&value);
    return value;
}

// Custom attribute not natively supported by any surface
// Note used by UsdPreviewSurface networks
bool
_readUnlit(const UsdShadeShader& surface)
{
    bool value = false;
    surface.GetPrim().GetAttribute(AdobeTokens->unlit).Get(&value);
    return value;
}

// -------------------------------------------------------------------------------------------------
// UsdPreviewSurface & ASM network node handlers
//
// Note, an ASM network uses the same nodes as UsdPreviewSurface that ship with USD. We expect that
// each input on the surface can either use a constant value or a simple linear chain of nodes to
// read a texture value:
//
// TexCoords (UsdPrimvarReader_float2)
//   |
//   V
// TexCoordXform (UsdTransform2d) [OPTIONAL]
//   |
//   V
// TexRead (UsdUVTexture) (* include scale & bias and channel selection)
//   |
//   V
// UsdPreviewSurface (UsdPreviewSurface) or ASM Surface (AdobeStandardMaterial_4_0)
//
// * Note that these surfaces will automatically apply the normal map transform for normal inputs.
//
// -------------------------------------------------------------------------------------------------

bool
handleUsdPrimvarReader(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    TfToken texCoordPrimvar;
    std::string texCoordPrimvarStr;
    getShaderInputValue(shader, AdobeTokens->varname, texCoordPrimvarStr);

    // Supports both string and token type values for the varname
    // string is the correct type, but token was added to support slightly
    // incorrect assets.
    if (!texCoordPrimvarStr.empty()) {
        texCoordPrimvar = TfToken(texCoordPrimvarStr);
    } else {
        getShaderInputValue(shader, AdobeTokens->varname, texCoordPrimvar);
    }
    int uvIndex = getSTPrimvarTokenIndex(texCoordPrimvar);
    if (uvIndex >= 0) {
        ctx.input.uvIndex = uvIndex;
    } else {
        TF_WARN("Texture reader %s is reading primvar %s. Only 'st' or 'st1'..'stN' is supported "
                "(Input %s)",
                shader.GetPrim().GetPath().GetText(),
                texCoordPrimvar.GetText(),
                ctx.surfaceInputName.GetText());
    }

    return true;
}

bool
handleUsdTransform2d(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    getShaderInputValue(shader, AdobeTokens->rotation, ctx.input.uvRotation);
    getShaderInputValue(shader, AdobeTokens->scale, ctx.input.uvScale);
    getShaderInputValue(shader, AdobeTokens->translation, ctx.input.uvTranslation);

    return followConnectedInput(ctx, shader, AdobeTokens->in);
}

// Handle texture-related shader inputs such as file paths and wrapping modes.
bool
handleUsdUVTexture(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    SdfAssetPath assetPath;
    if (getShaderInputValue(shader, AdobeTokens->file, assetPath)) {
        ctx.input.image = readImage(ctx.readLayerContext, assetPath);
    }
    getShaderInputValue(shader, AdobeTokens->wrapS, ctx.input.wrapS);
    getShaderInputValue(shader, AdobeTokens->wrapT, ctx.input.wrapT);
    getShaderInputValue(shader, AdobeTokens->minFilter, ctx.input.minFilter);
    getShaderInputValue(shader, AdobeTokens->magFilter, ctx.input.magFilter);
    getShaderInputValue(shader, AdobeTokens->scale, ctx.input.scale);
    getShaderInputValue(shader, AdobeTokens->bias, ctx.input.bias);
    getShaderInputValue(shader, AdobeTokens->sourceColorSpace, ctx.input.colorspace);

    // Default to 0th UVs unless overridden in handlePrimvarReader
    ctx.input.uvIndex = 0;

    // The name of the output on the texture reader determines which channel(s) of the texture we
    // read.
    ctx.input.channel = shaderOutput.GetBaseName();

    return followConnectedInput(ctx, shader, AdobeTokens->st);
}

// These handlers are used both for UsdPreviewSurface networks and also ASM networks
const ShaderHandlerMappings usdPreviewSurfaceHandlers = {
    { { AdobeTokens->UsdUVTexture }, handleUsdUVTexture },
    { { AdobeTokens->UsdTransform2d }, handleUsdTransform2d, kOptional },
    { { AdobeTokens->UsdPrimvarReader_float2 }, handleUsdPrimvarReader }
};

// -------------------------------------------------------------------------------------------------

bool
readUsdPreviewSurfaceMaterial(ReadLayerContext& ctx,
                              OpenPbrMaterial& material,
                              const UsdShadeShader& surface)
{
    TfToken shaderId;
    surface.GetShaderId(&shaderId);
    if (shaderId != AdobeTokens->UsdPreviewSurface) {
        return false;
    }

    int useSpecularWorkflow = 0;
    getShaderInputValue(surface, UsdPreviewSurfaceTokens->useSpecularWorkflow, useSpecularWorkflow);
    material.useSpecularWorkflow = useSpecularWorkflow != 0;
    getShaderInputValue(
      surface, UsdPreviewSurfaceTokens->opacityThreshold, material.opacityThreshold);

    bool success = true;
    auto input = [&ctx, &surface, &success](const TfToken& inputName, Input& input) {
        InputContext inputContext = { ctx, inputName, usdPreviewSurfaceHandlers, input };
        success &= readSurfaceInput(inputContext, surface);
    };
    input(UsdPreviewSurfaceTokens->diffuseColor, material.base_color);
    input(UsdPreviewSurfaceTokens->emissiveColor, material.emission_color);
    input(UsdPreviewSurfaceTokens->specularColor, material.specular_color);
    input(UsdPreviewSurfaceTokens->normal, material.geometry_normal);
    input(UsdPreviewSurfaceTokens->metallic, material.base_metalness);
    input(UsdPreviewSurfaceTokens->roughness, material.specular_roughness);
    input(UsdPreviewSurfaceTokens->clearcoat, material.coat_weight);
    input(UsdPreviewSurfaceTokens->clearcoatRoughness, material.coat_roughness);
    input(UsdPreviewSurfaceTokens->opacity, material.geometry_opacity);
    input(UsdPreviewSurfaceTokens->displacement, material.displacement);
    input(UsdPreviewSurfaceTokens->occlusion, material.occlusion);
    input(UsdPreviewSurfaceTokens->ior, material.specular_ior);

    return success;
}

bool
readASMMaterial(ReadLayerContext& ctx, OpenPbrMaterial& material, const UsdShadeShader& surface)
{
    TfToken shaderId;
    surface.GetShaderId(&shaderId);
    if (shaderId != AdobeTokens->adobeStandardMaterial) {
        return false;
    }

    // Note, we currently only support fixed values for normalScale. No texture support.
    bool scatter = false;
    getShaderInputValue(surface, AsmTokens->scatter, scatter);
    getShaderInputValue(surface, AsmTokens->normalScale, material.normalScale);

    bool success = true;
    auto input = [&ctx, &surface, &success](const TfToken& inputName, Input& input) {
        InputContext inputContext = { ctx, inputName, usdPreviewSurfaceHandlers, input };
        success &= readSurfaceInput(inputContext, surface);
    };
    input(AsmTokens->baseColor, material.base_color);
    input(AsmTokens->roughness, material.specular_roughness);
    input(AsmTokens->metallic, material.base_metalness);
    input(AsmTokens->opacity, material.geometry_opacity);
    input(AsmTokens->specularLevel, material.specular_weight);
    input(AsmTokens->specularEdgeColor, material.specular_color);
    input(AsmTokens->normal, material.geometry_normal);
    input(AsmTokens->height, material.displacement);
    input(AsmTokens->anisotropyLevel, material.specular_roughness_anisotropy);
    input(AsmTokens->anisotropyAngle, material.anisotropyAngle);
    input(AsmTokens->emissiveIntensity, material.emission_luminance);
    input(AsmTokens->emissive, material.emission_color);
    input(AsmTokens->sheenOpacity, material.fuzz_weight);
    input(AsmTokens->sheenColor, material.fuzz_color);
    input(AsmTokens->sheenRoughness, material.fuzz_roughness);
    input(AsmTokens->translucency, material.transmission_weight);
    input(AsmTokens->IOR, material.specular_ior);
    input(AsmTokens->absorptionColor, material.transmission_color);
    input(AsmTokens->absorptionDistance, material.transmission_depth);
    if (scatter) {
        material.subsurface_weight = Input{ VtValue(1.0f) };
        input(AsmTokens->scatteringColor, material.subsurface_color);
        input(AsmTokens->scatteringDistance, material.subsurface_radius);
        input(AsmTokens->scatteringDistanceScale, material.subsurface_radius_scale);
    }
    input(AsmTokens->coatOpacity, material.coat_weight);
    input(AsmTokens->coatColor, material.coat_color);
    input(AsmTokens->coatRoughness, material.coat_roughness);
    input(AsmTokens->coatIOR, material.coat_ior);
    input(AsmTokens->coatNormal, material.geometry_coat_normal);

    // Non-OpenPBR inputs
    input(AsmTokens->coatSpecularLevel, material.coatSpecularLevel);
    input(AsmTokens->ambientOcclusion, material.occlusion);
    input(AsmTokens->volumeThickness, material.volumeThickness);

    material.useSpecularWorkflow = _readUseSpecularWorkflow(surface);
    material.opacityThreshold = _readOpacityThreshold(surface);
    material.clearcoatModelsTransmissionTint = _readClearcoatModelsTransmissionTint(surface);
    material.isUnlit = _readUnlit(surface);

    return success;
}

// -------------------------------------------------------------------------------------------------
// OpenPBR/MaterialX network node handlers
//
// Note: that we use a MaterialX network with its nodes and have an OpenPBR surface node.
// We only support a specific node subset and topology that we parse here. We expect that each input
// on the surface can either use a constant value or a simple linear chain of nodes to read a
// texture value:
//
// TexCoords (ND_texcoord_vector2)
//   |
//   V
// TexCoordXform (ND_place2d_vector2) [OPTIONAL]
//   |
//   V
// TexRead (ND_image_vector4, ND_image_color3, ND_image_vector3, ND_UsdUVTexture_23)
//   |
//   V
// Convert (ND_convert_color3_vector3) (* only with ND_UsdUVTexture_23 and ND_normalmap) [OPTIONAL*]
//   |
//   V
// Normalmap (ND_normalmap) (* only with geometry_normal and geometry_coat_normal) [OPTIONAL*]
//   |
//   V
// ChannelSelect (ND_separate4_vector4) (* only float input w/ ND_image_vector4) [OPTIONAL*]
//   |
//   V
// Scale (ND_multiply_float, ND_multiply_color3, ND_multiply_vector3) [OPTIONAL]
//   |
//   V
// Bias (ND_add_float, ND_add_color3, ND_add_vector3) [OPTIONAL]
//   |
//   V
// AmbientOcclusionBaseColor (ND_mix_color3) [OPTIONAL]
//   |
//   V
// OpenPBR Surface (ND_open_pbr_surface_surfaceshader)
//
// -------------------------------------------------------------------------------------------------

bool
handleTexcoordVector2(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    TfToken shaderId;
    shader.GetShaderId(&shaderId);
    if (shaderId == MtlXTokens->ND_texcoord_vector2) {
        // Note, we have no information on how to map this index to a name of a texcoord primvar
        ctx.input.uvIndex = 0;
        getShaderInputValue(shader, AdobeTokens->index, ctx.input.uvIndex);

        return true;
    } else if (shaderId == MtlXTokens->ND_geompropvalue_vector2) {
        // try to map the geomprop to a uv index
        int index = 0;
        TfToken geomprop;
        std::string geompropStr;
        if (getShaderInputValue(shader, MtlXTokens->geomprop, geompropStr)) {
            index = getSTPrimvarTokenIndex(TfToken(geompropStr));
        } else if (getShaderInputValue(shader, MtlXTokens->geomprop, geomprop)) {
            index = getSTPrimvarTokenIndex(geomprop);
        }
        ctx.input.uvIndex = index;
        return true;
    }
    return false;
}

bool
handlePlace2dVector2(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    getShaderInputValue(shader, AdobeTokens->rotate, ctx.input.uvRotation);
    GfVec2f scale(1.0f);
    getShaderInputValue(shader, AdobeTokens->scale, scale);
    // For the place2d node, the scale is not a multiplier, but the overall scale and so we need to
    // invert the value
    ctx.input.uvScale[0] = scale[0] != 0.0f ? 1.0f / scale[0] : 0.0f;
    ctx.input.uvScale[1] = scale[1] != 0.0f ? 1.0f / scale[1] : 0.0f;
    getShaderInputValue(shader, AdobeTokens->offset, ctx.input.uvTranslation);

    return followConnectedInput(ctx, shader, AdobeTokens->texcoord);
}

bool
handleImage(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    SdfAssetPath assetPath;
    if (getShaderInputValue(shader, AdobeTokens->file, assetPath)) {
        ctx.input.image = readImage(ctx.readLayerContext, assetPath);

        UsdShadeInput fileInput = shader.GetInput(AdobeTokens->file);
        if (fileInput) {
            TfToken mtlxColorSpace = fileInput.GetAttr().GetColorSpace();
            ctx.input.colorspace =
              mtlxColorSpace == MtlXTokens->srgb_texture ? AdobeTokens->sRGB : AdobeTokens->raw;
        }
    }

    TfToken shaderId;
    shader.GetShaderId(&shaderId);
    if (shaderId == MtlXTokens->ND_UsdUVTexture_23) {
        // The name of the output on the texture reader determines which channel(s) of the texture
        // we read.
        ctx.input.channel = shaderOutput.GetBaseName();

    } else if (shaderId == MtlXTokens->ND_image_color3 ||
               shaderId == MtlXTokens->ND_image_vector3) {
        ctx.input.channel = AdobeTokens->rgb;
    } else {
        // Note, if a single float channel is read via a ND_image_vector4 node, which is then
        // followed by a ND_separate4_vector4 node to extract one out of 4 RGBA channels, then the
        // handler for that node will set input.channel to the appropriate value
    }

    if (shaderId == MtlXTokens->ND_UsdUVTexture_23) {
        GfVec4f defaultValue;
        if (getShaderInputValue(shader, AdobeTokens->fallback, defaultValue)) {
            if (ctx.input.channel == AdobeTokens->r) {
                ctx.input.value = defaultValue[0];
            } else if (ctx.input.channel == AdobeTokens->g) {
                ctx.input.value = defaultValue[1];
            } else if (ctx.input.channel == AdobeTokens->b) {
                ctx.input.value = defaultValue[2];
            } else if (ctx.input.channel == AdobeTokens->a) {
                ctx.input.value = defaultValue[3];
            } else if (ctx.input.channel == AdobeTokens->rgb) {
                ctx.input.value = GfVec3f(defaultValue[0], defaultValue[1], defaultValue[2]);
            } else {
                TF_WARN("ND_UsdUVTexture_23 node at %s has a default value, but the channel is not "
                        "valid: '%s' (input %s)",
                        shader.GetPath().GetText(),
                        ctx.input.channel.GetText(),
                        ctx.surfaceInputName.GetText());
            }
        }
    } else if (shaderId == MtlXTokens->ND_image_color3 ||
               shaderId == MtlXTokens->ND_image_vector3) {
        GfVec3f defaultValue;
        if (getShaderInputValue(shader, AdobeTokens->defaultValue, defaultValue)) {
            ctx.input.value = defaultValue;
        }
    } else if (shaderId == MtlXTokens->ND_image_vector4) {
        GfVec4f defaultValue;
        if (getShaderInputValue(shader, AdobeTokens->defaultValue, defaultValue)) {
            // The ND_image_vector4 node is used to read a texture as RGBA and then extract a single
            // channel. So the default value has to extract the same thing.
            // Note, that the channel should have been set by the handleSeparate4Vector4()
            // function.
            if (ctx.input.channel == AdobeTokens->r) {
                ctx.input.value = defaultValue[0];
            } else if (ctx.input.channel == AdobeTokens->g) {
                ctx.input.value = defaultValue[1];
            } else if (ctx.input.channel == AdobeTokens->b) {
                ctx.input.value = defaultValue[2];
            } else if (ctx.input.channel == AdobeTokens->a) {
                ctx.input.value = defaultValue[3];
            } else {
                TF_WARN("ND_image_vector4 node at %s has a default value, but the channel is not "
                        "valid: '%s' (input %s)",
                        shader.GetPath().GetText(),
                        ctx.input.channel.GetText(),
                        ctx.surfaceInputName.GetText());
            }
        }
    }

    auto fromMaterialXAddressMode = [](const std::string& addressMode) -> TfToken {
        if (addressMode.empty()) {
            return TfToken();
        } else if (addressMode == "periodic") {
            // "periodic" maps to "repeat", which is also the default. So we suppress it
            return TfToken();
        } else if (addressMode == "clamp") {
            return AdobeTokens->clamp;
        } else if (addressMode == "mirror") {
            return AdobeTokens->mirror;
        } else if (addressMode == "constant") {
            return AdobeTokens->black;
        } else {
            TF_WARN("Unknown addressmode '%s'", addressMode.c_str());
            return TfToken();
        }
    };

    if (shaderId == MtlXTokens->ND_UsdUVTexture_23) {
        // The inputs on the node are called wrapS and wrapT, but are using string values like the
        // uaddressmode and vaddressmode on other MaterialX texture nodes.
        std::string wrapS, wrapT;
        getShaderInputValue(shader, AdobeTokens->wrapS, wrapS);
        getShaderInputValue(shader, AdobeTokens->wrapT, wrapT);
        ctx.input.wrapS = fromMaterialXAddressMode(wrapS);
        ctx.input.wrapT = fromMaterialXAddressMode(wrapT);
        getShaderInputValue(shader, AdobeTokens->scale, ctx.input.scale);
        getShaderInputValue(shader, AdobeTokens->bias, ctx.input.bias);

        if (ctx.surfaceInputName == OpenPbrTokens->geometry_normal ||
            ctx.surfaceInputName == OpenPbrTokens->geometry_coat_normal) {
            // In MaterialX, the ND_normalmap node, which is downstream of the ND_UsdUVTexture_23
            // will decode the normal from the raw texture value, assuming the OpenGL convention,
            // using a scale and bias of 2 (kOpenGLNormalTexScale) and -1 (kOpenGLNormalTexBias).
            // That means it would be redundant to have this on the ND_UsdUVTexture_23 node.
            //
            // We still want to have these values in our Input struct, especially if we're trying to
            // differentiate it from a DirectX encoded normalmap and/or a normal strength
            // multiplier. So we apply the affine transform to the usually neutral scale and bias
            // value to recover the expected decoding values.
            //
            // Note that this mirrors the process in the OpenPBR writing code.
            ctx.input.scale = GfCompMult(kOpenGLNormalTexScale, ctx.input.scale);
            ctx.input.bias =
              GfCompMult(kOpenGLNormalTexScale, ctx.input.bias) + kOpenGLNormalTexBias;
        }

        return followConnectedInput(ctx, shader, AdobeTokens->st);
    } else {
        std::string uaddressmode, vaddressmode;
        getShaderInputValue(shader, AdobeTokens->uaddressmode, uaddressmode);
        getShaderInputValue(shader, AdobeTokens->vaddressmode, vaddressmode);
        ctx.input.wrapS = fromMaterialXAddressMode(uaddressmode);
        ctx.input.wrapT = fromMaterialXAddressMode(vaddressmode);

        return followConnectedInput(ctx, shader, AdobeTokens->texcoord);
    }
}

bool
handleConvertColorToVector(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    return followConnectedInput(ctx, shader, AdobeTokens->in);
}

bool
handleNormalMap(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    return followConnectedInput(ctx, shader, AdobeTokens->in);
}

bool
handleAmbientOcclusionBaseColorMap(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    // create a new input context that reads the 'bg' input of the ND_mix_color3 node
    InputContext inputContext = {
        ctx.readLayerContext, AdobeTokens->bg, ctx.handlerMappings, ctx.input, ctx.handlerIndex
    };
    return readSurfaceInput(inputContext, shader);
}

bool
handleAmbientOcclusionMap(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    // create a new input context that reads the 'fg' input of the ND_mix_color3 node
    InputContext inputContext = {
        ctx.readLayerContext, AdobeTokens->fg, ctx.handlerMappings, ctx.input, ctx.handlerIndex
    };
    return readSurfaceInput(inputContext, shader);
}

bool
handleConvertFloatToColor(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    // create a new input context that reads the 'in' input of the ND_convert_float_color3 node
    InputContext inputContext = {
        ctx.readLayerContext, AdobeTokens->in, ctx.handlerMappings, ctx.input, ctx.handlerIndex
    };
    return readSurfaceInput(inputContext, shader);
}

bool
handleSeparate4Vector4(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    auto outputNameToChannelToken = [](const TfToken& outputName) -> TfToken {
        if (outputName == AdobeTokens->outx) {
            return AdobeTokens->r;
        } else if (outputName == AdobeTokens->outy) {
            return AdobeTokens->g;
        } else if (outputName == AdobeTokens->outz) {
            return AdobeTokens->b;
        } else if (outputName == AdobeTokens->outw) {
            return AdobeTokens->a;
        }

        TF_WARN("Unknown output name '%s'", outputName.GetText());

        return AdobeTokens->r;
    };

    // Note, this node goes together with a ND_image_vector4. The handleImage() function will
    // not set the channel in that case, so that the channel choice here takes effect.
    ctx.input.channel = outputNameToChannelToken(shaderOutput.GetBaseName());

    return followConnectedInput(ctx, shader, AdobeTokens->in);
}

bool
handleMultiply(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    TfToken shaderId;
    shader.GetShaderId(&shaderId);
    if (shaderId == MtlXTokens->ND_multiply_float) {
        float scale = ctx.input.scale[0];
        getShaderInputValue(shader, AdobeTokens->in1, scale);
        ctx.input.scale[0] = scale;
    } else if (shaderId == MtlXTokens->ND_multiply_color3 ||
               shaderId == MtlXTokens->ND_multiply_vector3) {
        GfVec3f scale(ctx.input.scale[0], ctx.input.scale[1], ctx.input.scale[2]);
        getShaderInputValue(shader, AdobeTokens->in1, scale);
        ctx.input.scale[0] = scale[0];
        ctx.input.scale[1] = scale[1];
        ctx.input.scale[2] = scale[2];
    } else {
        TF_CODING_ERROR("handleMultiply called for unexpected node of type %s", shaderId.GetText());
        return false;
    }

    return followConnectedInput(ctx, shader, AdobeTokens->in2);
}

bool
handleAdd(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());

    TfToken shaderId;
    shader.GetShaderId(&shaderId);
    if (shaderId == MtlXTokens->ND_add_float) {
        float bias = ctx.input.bias[0];
        getShaderInputValue(shader, AdobeTokens->in1, bias);
        ctx.input.bias[0] = bias;
    } else if (shaderId == MtlXTokens->ND_add_color3 || shaderId == MtlXTokens->ND_add_vector3) {
        GfVec3f bias(ctx.input.bias[0], ctx.input.bias[1], ctx.input.bias[2]);
        getShaderInputValue(shader, AdobeTokens->in1, bias);
        ctx.input.bias[0] = bias[0];
        ctx.input.bias[1] = bias[1];
        ctx.input.bias[2] = bias[2];
    } else {
        TF_CODING_ERROR("handleAdd called for unexpected node of type %s", shaderId.GetText());
        return false;
    }

    return followConnectedInput(ctx, shader, AdobeTokens->in2);
}

// Note, we currently can't express that a normal input should have a ND_normalmap, but must not
// have a ND_separate4_vector4 node. Or that a float input needs a ND_separate4_vector4 followed by
// a ND_image_vector4 or just a ND_ND_UsdUVTexture_23 node. These could be modeled by different
// handler mappings and then switching depending on the surface input.

// clang-format off
const ShaderHandlerMappings materialXHandlers = {
    { { MtlXTokens->ND_normalmap },
      handleNormalMap,
      kOptional },
    { { MtlXTokens->ND_mix_color3 },
      handleAmbientOcclusionBaseColorMap,
      kOptional },
    { { MtlXTokens->ND_convert_color3_vector3 },
      handleConvertColorToVector,
      kOptional },
    { { MtlXTokens->ND_add_float, MtlXTokens->ND_add_color3, MtlXTokens->ND_add_vector3 },
      handleAdd,
      kOptional },
    { { MtlXTokens->ND_multiply_float, MtlXTokens->ND_multiply_color3, MtlXTokens->ND_multiply_vector3 },
      handleMultiply,
      kOptional },
    { { MtlXTokens->ND_separate4_vector4 },
      handleSeparate4Vector4,
      kOptional },
    { { MtlXTokens->ND_UsdUVTexture_23, MtlXTokens->ND_image_vector4, MtlXTokens->ND_image_color3,
        MtlXTokens->ND_image_vector3 },
      handleImage },
    { { MtlXTokens->ND_place2d_vector2 },
      handlePlace2dVector2, kOptional },
    { { MtlXTokens->ND_texcoord_vector2, MtlXTokens->ND_geompropvalue_vector2 },
      handleTexcoordVector2 }
};

// This is a custom handler sequence to handle ambient occlusion input connections to base color
const ShaderHandlerMappings materialXAmbientOcclusionHandlers = {
    { { MtlXTokens->ND_mix_color3 },
      handleAmbientOcclusionMap },
    { { MtlXTokens->ND_convert_float_color3 },
      handleConvertFloatToColor },
    { { MtlXTokens->ND_UsdUVTexture_23, MtlXTokens->ND_image_vector4, MtlXTokens->ND_image_color3,
        MtlXTokens->ND_image_vector3 },
      handleImage },
    { { MtlXTokens->ND_place2d_vector2 },
      handlePlace2dVector2, kOptional },
    { { MtlXTokens->ND_texcoord_vector2, MtlXTokens->ND_geompropvalue_vector2 },
      handleTexcoordVector2 }
};
// clang-format on

// -------------------------------------------------------------------------------------------------

bool
readOpenPbrMaterial(ReadLayerContext& ctx, OpenPbrMaterial& material, const UsdShadeShader& surface)
{
    TfToken shaderId;
    surface.GetShaderId(&shaderId);
    if (shaderId != MtlXTokens->ND_open_pbr_surface_surfaceshader) {
        return false;
    }

    bool success = true;
    auto input = [&ctx, &surface, &success](
                   const TfToken& inputName, Input& input, const ShaderHandlerMappings& handlers) {
        InputContext inputContext = { ctx, inputName, handlers, input };
        bool result = readSurfaceInput(inputContext, surface);
        if (!result) {
            TF_WARN("Failed to read input %s on OpenPBR surface %s",
                    inputName.GetText(),
                    surface.GetPath().GetText());
            success = false;
        }
    };

#define INPUT(x) input(OpenPbrTokens->x, material.x, materialXHandlers);
    INPUT(base_weight)
    INPUT(base_color)
    INPUT(base_diffuse_roughness)
    INPUT(base_metalness)
    INPUT(specular_weight)
    INPUT(specular_color)
    INPUT(specular_roughness)
    INPUT(specular_ior)
    INPUT(specular_roughness_anisotropy)
    INPUT(transmission_weight)
    INPUT(transmission_color)
    INPUT(transmission_depth)
    INPUT(transmission_scatter)
    INPUT(transmission_scatter_anisotropy)
    INPUT(transmission_dispersion_scale)
    INPUT(transmission_dispersion_abbe_number)
    INPUT(subsurface_weight)
    INPUT(subsurface_color)
    INPUT(subsurface_radius)
    INPUT(subsurface_radius_scale)
    INPUT(subsurface_scatter_anisotropy)
    INPUT(fuzz_weight)
    INPUT(fuzz_color)
    INPUT(fuzz_roughness)
    INPUT(coat_weight)
    INPUT(coat_color)
    INPUT(coat_roughness)
    INPUT(coat_roughness_anisotropy)
    INPUT(coat_ior)
    INPUT(coat_darkening)
    INPUT(thin_film_weight)
    INPUT(thin_film_thickness)
    INPUT(thin_film_ior)
    INPUT(emission_luminance)
    INPUT(emission_color)
    INPUT(geometry_opacity)
    INPUT(geometry_thin_walled)
    INPUT(geometry_normal)
    INPUT(geometry_coat_normal)
    INPUT(geometry_tangent)
    INPUT(geometry_coat_tangent)
#undef INPUT

    // handle collecting the ambient occlusion input by following the base_color input but using a
    // differnt set of handlers to control the shade path traversal
    input(OpenPbrTokens->base_color, material.occlusion, materialXAmbientOcclusionHandlers);

    // Non-OpenPBR inputs
    input(UsdPreviewSurfaceTokens->displacement, material.displacement, materialXHandlers);
    input(AsmTokens->anisotropyAngle, material.anisotropyAngle, materialXHandlers);
    input(AsmTokens->coatSpecularLevel, material.coatSpecularLevel, materialXHandlers);
    input(AsmTokens->volumeThickness, material.volumeThickness, materialXHandlers);
    material.normalScale = _readNormalScale(surface);
    material.useSpecularWorkflow = _readUseSpecularWorkflow(surface);
    material.opacityThreshold = _readOpacityThreshold(surface);
    material.clearcoatModelsTransmissionTint = _readClearcoatModelsTransmissionTint(surface);
    material.isUnlit = _readUnlit(surface);

    return success;
}

// -------------------------------------------------------------------------------------------------

bool
readMaterial(ReadLayerContext& ctx, const UsdPrim& prim)
{
    OpenPbrMaterial material;
    material.name = prim.GetPath().GetName();
    material.displayName = prim.GetDisplayName();

    UsdShadeMaterial usdMaterial(prim);

    // We have a priority order of surface types:
    // 1. OpenPBR/MaterialX (mtlx)
    // 2. ASM (adobe)
    // 3. UsdPreviewSurface (the universal fallback)
    UsdShadeShader surface =
      usdMaterial.ComputeSurfaceSource({ MtlXTokens->mtlx, AdobeTokens->adobe });
    if (!surface) {
        TF_WARN("No surface shader for material %s", prim.GetPath().GetText());
        return false;
    }

    bool success = readOpenPbrMaterial(ctx, material, surface);
    if (!success) {
        success = readASMMaterial(ctx, material, surface);
        if (!success) {
            success = readUsdPreviewSurfaceMaterial(ctx, material, surface);
        }
    }

    // Question: when the reading fails, should the material be removed from the UsdData?
    // Currently we keep a partially parsed Material in there.
    auto [materialIndex, outputMaterial] = ctx.usd->addMaterial();
    ctx.materials[prim.GetPath().GetString()] = materialIndex;
    outputMaterial = mapOpenPbrMaterialStructToMaterialStruct(material);

    printMaterial("layer::read", prim.GetPath(), outputMaterial, ctx.debugTag);
    return success;
}

}