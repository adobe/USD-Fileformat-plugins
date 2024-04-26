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
#include "sdfMaterialUtils.h"

#include "common.h"
#include "debugCodes.h"
#include "sdfUtils.h"

#include <pxr/usd/usdShade/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace { // anonymous namespace

using namespace adobe::usd;

using TokenToSdfValueTypeMap = std::unordered_map<TfToken, SdfValueTypeName, TfToken::HashFunctor>;

struct ShaderInfo
{
    TokenToSdfValueTypeMap inputTypes;
    TokenToSdfValueTypeMap outputTypes;

    SdfValueTypeName getInputType(const TfToken& inputName) const
    {
        const auto it = inputTypes.find(inputName);
        if (it != inputTypes.end()) {
            return it->second;
        }
        TF_WARN("Couldn't find type for input %s", inputName.GetText());
        return SdfValueTypeNames->Token;
    }

    SdfValueTypeName getOutputType(const TfToken& outputName) const
    {
        const auto it = outputTypes.find(outputName);
        if (it != outputTypes.end()) {
            return it->second;
        }
        TF_WARN("Couldn't find type for output %s", outputName.GetText());
        return SdfValueTypeNames->Token;
    }
};

// Table of shaders with input and outputs and their respective types
// This table is used to make createShader extra convenient to use.
// The data here is essentially a mini form of the shader schemas. If we're concerned about this
// staying up-to-date we could investigate gathering this information at run-time via the
// shader definition registry (Sdr) module. Unfortunate, the ASM terminal nodes are not found there.
// clang-format off
static const std::map<TfToken, ShaderInfo> shaderInfos = {
    // UsdPreviewSurface and related shaders
    { AdobeTokens->UsdUVTexture, {{
        { TfToken("inputs:file"), SdfValueTypeNames->Asset },
        { TfToken("inputs:st"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:wrapS"), SdfValueTypeNames->Token },
        { TfToken("inputs:wrapT"), SdfValueTypeNames->Token },
        { TfToken("inputs:fallback"), SdfValueTypeNames->Float4 },
        { TfToken("inputs:scale"), SdfValueTypeNames->Float4 },
        { TfToken("inputs:bias"), SdfValueTypeNames->Float4 },
        { TfToken("inputs:sourceColorSpace"), SdfValueTypeNames->Token }
    }, {
        { TfToken("outputs:r"), SdfValueTypeNames->Float },
        { TfToken("outputs:g"), SdfValueTypeNames->Float },
        { TfToken("outputs:b"), SdfValueTypeNames->Float },
        { TfToken("outputs:a"), SdfValueTypeNames->Float },
        { TfToken("outputs:rgb"), SdfValueTypeNames->Float3 }
    }}},
    { AdobeTokens->UsdTransform2d, {{
        { TfToken("inputs:in"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:rotation"), SdfValueTypeNames->Float },
        { TfToken("inputs:scale"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:translation"), SdfValueTypeNames->Float2 }
    }, {
        { TfToken("outputs:result"), SdfValueTypeNames->Float2 }
    }}},
    { AdobeTokens->UsdPrimvarReader_float2, {{
        { TfToken("inputs:varname"), SdfValueTypeNames->String },
        { TfToken("inputs:fallback"), SdfValueTypeNames->Float2 }
    }, {
        { TfToken("outputs:result"), SdfValueTypeNames->Float2 }
    }}},
    { AdobeTokens->UsdPreviewSurface, {{
        { TfToken("inputs:diffuseColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:emissiveColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:useSpecularWorkflow"), SdfValueTypeNames->Int },
        { TfToken("inputs:specularColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:metallic"), SdfValueTypeNames->Float },
        { TfToken("inputs:roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:clearcoat"), SdfValueTypeNames->Float },
        { TfToken("inputs:clearcoatRoughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:opacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:opacityThreshold"), SdfValueTypeNames->Float },
        { TfToken("inputs:ior"), SdfValueTypeNames->Float },
        { TfToken("inputs:normal"), SdfValueTypeNames->Normal3f },
        { TfToken("inputs:displacement"), SdfValueTypeNames->Float },
        { TfToken("inputs:occlusion"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:surface"), SdfValueTypeNames->Token },
        { TfToken("outputs:displacement"), SdfValueTypeNames->Token }
    }}},
    // MaterialX nodes
    { MtlXTokens->ND_texcoord_vector2, {{
        { TfToken("inputs:index"), SdfValueTypeNames->Int },
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float2 }
    }}},
    { MtlXTokens->ND_rotate2d_vector2, {{
        { TfToken("inputs:in"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:amount"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float2 }
    }}},
    { MtlXTokens->ND_multiply_vector2, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:in2"), SdfValueTypeNames->Float2 }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float2 }
    }}},
    { MtlXTokens->ND_add_vector2, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:in2"), SdfValueTypeNames->Float2 }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float2 }
    }}},
    { MtlXTokens->ND_place2d_vector2, {{
        { TfToken("inputs:texcoord"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:pivot"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:scale"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:rotate"), SdfValueTypeNames->Float },
        { TfToken("inputs:offset"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:operationorder"), SdfValueTypeNames->Int }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float2 }
    }}},
    { MtlXTokens->ND_separate4_vector4, {{
        { TfToken("inputs:in"), SdfValueTypeNames->Float4 },
    }, {
        { TfToken("outputs:outx"), SdfValueTypeNames->Float },
        { TfToken("outputs:outy"), SdfValueTypeNames->Float },
        { TfToken("outputs:outz"), SdfValueTypeNames->Float },
        { TfToken("outputs:outw"), SdfValueTypeNames->Float }
    }}},
    { MtlXTokens->ND_convert_float_color3, {{
        { TfToken("inputs:in"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Color3f }
    }}},
    { MtlXTokens->ND_multiply_float, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Float },
        { TfToken("inputs:in2"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float }
    }}},
    { MtlXTokens->ND_multiply_color3, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:in2"), SdfValueTypeNames->Color3f }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Color3f }
    }}},
    { MtlXTokens->ND_multiply_vector3, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:in2"), SdfValueTypeNames->Float3 }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float3 }
    }}},
    { MtlXTokens->ND_add_float, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Float },
        { TfToken("inputs:in2"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float }
    }}},
    { MtlXTokens->ND_add_color3, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:in2"), SdfValueTypeNames->Color3f }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Color3f }
    }}},
    { MtlXTokens->ND_add_vector3, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:in2"), SdfValueTypeNames->Float3 }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float3 }
    }}},
    { MtlXTokens->ND_image_vector4, {{
        { TfToken("inputs:texcoord"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:file"), SdfValueTypeNames->Asset },
        { TfToken("inputs:default"), SdfValueTypeNames->Float4 },
        { TfToken("inputs:uaddressmode"), SdfValueTypeNames->String },
        { TfToken("inputs:vaddressmode"), SdfValueTypeNames->String }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float4 }
    }}},
    { MtlXTokens->ND_image_color3, {{
        { TfToken("inputs:texcoord"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:file"), SdfValueTypeNames->Asset },
        { TfToken("inputs:default"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:uaddressmode"), SdfValueTypeNames->String },
        { TfToken("inputs:vaddressmode"), SdfValueTypeNames->String }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Color3f }
    }}},
    { MtlXTokens->ND_image_vector3, {{
        { TfToken("inputs:texcoord"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:file"), SdfValueTypeNames->Asset },
        { TfToken("inputs:default"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:uaddressmode"), SdfValueTypeNames->String },
        { TfToken("inputs:vaddressmode"), SdfValueTypeNames->String }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float3 }
    }}},
    { MtlXTokens->ND_image_float, {{
        { TfToken("inputs:texcoord"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:file"), SdfValueTypeNames->Asset },
        { TfToken("inputs:default"), SdfValueTypeNames->Float },
        { TfToken("inputs:uaddressmode"), SdfValueTypeNames->String },
        { TfToken("inputs:vaddressmode"), SdfValueTypeNames->String }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float }
    }}},
    { MtlXTokens->ND_normalmap, {{
        { TfToken("inputs:in"), SdfValueTypeNames->Float3 }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float3 }
    }}},
    // Note, the ND_adobe_standard_material will be retired soon in favor of the OpenPBR node
    { MtlXTokens->ND_adobe_standard_material, {{
        { TfToken("inputs:base_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:ambient_occlusion"), SdfValueTypeNames->Float },
        { TfToken("inputs:roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:metallic"), SdfValueTypeNames->Float },
        { TfToken("inputs:normal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:opacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:emission_color"), SdfValueTypeNames->Color3f }
    }, {
        { TfToken("outputs:surface"), SdfValueTypeNames->Token }
    }}},
    { MtlXTokens->ND_open_pbr_surface_surfaceshader, {{
        { TfToken("inputs:base_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:base_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:base_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:base_metalness"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:specular_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_ior"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_ior_level"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_rotation"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:transmission_depth"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_scatter"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:transmission_scatter_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_dispersion"), SdfValueTypeNames->Float },
        { TfToken("inputs:subsurface_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:subsurface_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:subsurface_radius"), SdfValueTypeNames->Float },
        { TfToken("inputs:subsurface_radius_scale"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:subsurface_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:fuzz_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:fuzz_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:fuzz_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:coat_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_rotation"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_ior"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_ior_level"), SdfValueTypeNames->Float },
        { TfToken("inputs:thin_film_thickness"), SdfValueTypeNames->Float },
        { TfToken("inputs:thin_film_ior"), SdfValueTypeNames->Float },
        { TfToken("inputs:emission_luminance"), SdfValueTypeNames->Float },
        { TfToken("inputs:emission_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:geometry_opacity"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:geometry_thin_walled"), SdfValueTypeNames->Bool },
        { TfToken("inputs:geometry_normal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:geometry_coat_normal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:geometry_tangent"), SdfValueTypeNames->Float3 },
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Token }
    }}},
    // Adobe Standard Material surface node
    { AdobeTokens->adobeStandardMaterial, {{
        { TfToken("inputs:baseColor"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:metallic"), SdfValueTypeNames->Float },
        { TfToken("inputs:opacity"), SdfValueTypeNames->Float },
        // XXX ASM doesn't actually have an opacityThreshold, which is a UsdPreviewSurface concept
        // But we use it to carry the information about the threshold for transcoding uses
        { TfToken("inputs:opacityThreshold"), SdfValueTypeNames->Float },
        { TfToken("inputs:specularLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:specularEdgeColor"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:normal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:normalScale"), SdfValueTypeNames->Float },
        { TfToken("inputs:combineNormalAndHeight"), SdfValueTypeNames->Bool },
        { TfToken("inputs:height"), SdfValueTypeNames->Float },
        { TfToken("inputs:heightScale"), SdfValueTypeNames->Float },
        { TfToken("inputs:heightLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:anisotropyLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:anisotropyAngle"), SdfValueTypeNames->Float },
        { TfToken("inputs:emissiveIntensity"), SdfValueTypeNames->Float },
        { TfToken("inputs:emissive"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:sheenOpacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:sheenColor"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:sheenRoughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:translucency"), SdfValueTypeNames->Float },
        { TfToken("inputs:IOR"), SdfValueTypeNames->Float },
        { TfToken("inputs:dispersion"), SdfValueTypeNames->Float },
        { TfToken("inputs:absorptionColor"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:absorptionDistance"), SdfValueTypeNames->Float },
        { TfToken("inputs:scatter"), SdfValueTypeNames->Bool },
        { TfToken("inputs:scatteringColor"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:scatteringDistance"), SdfValueTypeNames->Float },
        { TfToken("inputs:scatteringDistanceScale"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:scatteringRedShift"), SdfValueTypeNames->Float },
        { TfToken("inputs:scatteringRayleigh"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatOpacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatColor"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:coatRoughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatIOR"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatSpecularLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatNormal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:coatNormalScale"), SdfValueTypeNames->Float },
        { TfToken("inputs:ambientOcclusion"), SdfValueTypeNames->Float },
        { TfToken("inputs:volumeThickness"), SdfValueTypeNames->Float },
        { TfToken("inputs:volumeThicknessScale"), SdfValueTypeNames->Float },
    }, {
        { TfToken("outputs:surface"), SdfValueTypeNames->Token }
    }}}
};

using InputRangeMap = std::unordered_map<TfToken, MinMaxVtValuePair, TfToken::HashFunctor>;

// Min/max ranges for material level inputs
static const InputRangeMap inputRanges = {
    { AdobeTokens->absorptionDistance, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->anisotropyLevel, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->clearcoat, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->clearcoatIor, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->clearcoatRoughness, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->displacement, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->ior, { VtValue(1.0), VtValue(3.0) } },
    { AdobeTokens->metallic, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->occlusion, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->opacity, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->opacityThreshold, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->roughness, { VtValue(0.0), VtValue(1.0) } },
    // { AdobeTokens->scatteringDistance, { VtValue(0.0), VtValue(1.0) } },  // not sure this should have limits
    { AdobeTokens->sheenRoughness, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->specularLevel, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->transmission, { VtValue(0.0), VtValue(1.0) } },
    { AdobeTokens->useSpecularWorkflow, { VtValue(0), VtValue(1) } },
    // { AdobeTokens->volumeThickness, { VtValue(0.0), VtValue(1.0) } },  // not sure this should have limits
};

// mapping of UsdPreviewSurface input name to material input name (and type)
static const InputToMaterialInputTypeMap usdPreviewSurfaceInputRemapping = {
    { AdobeTokens->clearcoat, { AdobeTokens->clearcoat, SdfValueTypeNames->Float } },
    { AdobeTokens->clearcoatRoughness, { AdobeTokens->clearcoatRoughness, SdfValueTypeNames->Float } },
    { AdobeTokens->diffuseColor, { AdobeTokens->diffuseColor, SdfValueTypeNames->Color3f } },
    { AdobeTokens->displacement, { AdobeTokens->displacement, SdfValueTypeNames->Float } },
    { AdobeTokens->emissiveColor, { AdobeTokens->emissiveColor, SdfValueTypeNames->Color3f } },
    { AdobeTokens->ior, { AdobeTokens->ior, SdfValueTypeNames->Float } },
    { AdobeTokens->metallic, { AdobeTokens->metallic, SdfValueTypeNames->Float } },
    { AdobeTokens->normal, { AdobeTokens->normal, SdfValueTypeNames->Normal3f } },
    { AdobeTokens->occlusion, { AdobeTokens->occlusion, SdfValueTypeNames->Float } },
    { AdobeTokens->opacity, { AdobeTokens->opacity, SdfValueTypeNames->Float } },
    { AdobeTokens->opacityThreshold, { AdobeTokens->opacityThreshold, SdfValueTypeNames->Float } },
    { AdobeTokens->roughness, { AdobeTokens->roughness, SdfValueTypeNames->Float } },
    { AdobeTokens->specularColor, { AdobeTokens->specularColor, SdfValueTypeNames->Color3f } },
    { AdobeTokens->useSpecularWorkflow, { AdobeTokens->useSpecularWorkflow, SdfValueTypeNames->Int } }
};

// mapping of ASM input name to material input name (and type)
static const InputToMaterialInputTypeMap asmInputRemapping = {
    { AdobeTokens->IOR, { AdobeTokens->ior, SdfValueTypeNames->Float } },
    { AdobeTokens->absorptionColor, { AdobeTokens->absorptionColor, SdfValueTypeNames->Float3 } },
    { AdobeTokens->absorptionDistance, { AdobeTokens->absorptionDistance, SdfValueTypeNames->Float } },
    { AdobeTokens->ambientOcclusion, { AdobeTokens->occlusion, SdfValueTypeNames->Float } },
    { AdobeTokens->anisotropyAngle, { AdobeTokens->anisotropyAngle, SdfValueTypeNames->Float } },
    { AdobeTokens->anisotropyLevel, { AdobeTokens->anisotropyLevel, SdfValueTypeNames->Float } },
    { AdobeTokens->baseColor, { AdobeTokens->diffuseColor, SdfValueTypeNames->Float3 } },
    { AdobeTokens->coatColor, { AdobeTokens->clearcoatColor, SdfValueTypeNames->Float3 } },
    { AdobeTokens->coatIOR, { AdobeTokens->clearcoatIor, SdfValueTypeNames->Float } },
    { AdobeTokens->coatNormal, { AdobeTokens->clearcoatNormal, SdfValueTypeNames->Float3 } },
    { AdobeTokens->coatOpacity, { AdobeTokens->clearcoat, SdfValueTypeNames->Float } }, // **
    { AdobeTokens->coatRoughness, { AdobeTokens->clearcoatRoughness, SdfValueTypeNames->Float } },
    { AdobeTokens->coatSpecularLevel, { AdobeTokens->clearcoatSpecular, SdfValueTypeNames->Float } },
    { AdobeTokens->emissive, { AdobeTokens->emissiveColor, SdfValueTypeNames->Float3 } },
    { AdobeTokens->height, { AdobeTokens->displacement, SdfValueTypeNames->Float } },
    { AdobeTokens->metallic, { AdobeTokens->metallic, SdfValueTypeNames->Float } },
    { AdobeTokens->normal, { AdobeTokens->normal, SdfValueTypeNames->Float3 } },
    { AdobeTokens->opacity, { AdobeTokens->opacity, SdfValueTypeNames->Float } },
    { AdobeTokens->opacityThreshold, { AdobeTokens->opacityThreshold, SdfValueTypeNames->Float } },
    { AdobeTokens->roughness, { AdobeTokens->roughness, SdfValueTypeNames->Float } },
    { AdobeTokens->scatteringColor, { AdobeTokens->scatteringColor, SdfValueTypeNames->Float3 } },
    { AdobeTokens->scatteringDistance, { AdobeTokens->scatteringDistance, SdfValueTypeNames->Float } },
    { AdobeTokens->sheenColor, { AdobeTokens->sheenColor, SdfValueTypeNames->Float3 } },
    { AdobeTokens->sheenRoughness, { AdobeTokens->sheenRoughness, SdfValueTypeNames->Float } },
    { AdobeTokens->specularEdgeColor, { AdobeTokens->specularColor, SdfValueTypeNames->Float3 } },
    { AdobeTokens->specularLevel, { AdobeTokens->specularLevel, SdfValueTypeNames->Float } },
    { AdobeTokens->translucency, { AdobeTokens->transmission, SdfValueTypeNames->Float } },
    { AdobeTokens->volumeThickness, { AdobeTokens->volumeThickness, SdfValueTypeNames->Float } },
};

// mapping of MaterialX input name to material input name (and type)
static const InputToMaterialInputTypeMap materialXInputRemapping = {
    { OpenPbrTokens->base_color, { AdobeTokens->diffuseColor, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->base_metalness, { AdobeTokens->metallic, SdfValueTypeNames->Float } },
    { OpenPbrTokens->coat_color, { AdobeTokens->clearcoatColor, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->coat_ior, { AdobeTokens->clearcoatIor, SdfValueTypeNames->Float } },
    { OpenPbrTokens->coat_roughness, { AdobeTokens->clearcoatRoughness, SdfValueTypeNames->Float } },
    { OpenPbrTokens->coat_weight, { AdobeTokens->clearcoat, SdfValueTypeNames->Float } },
    { OpenPbrTokens->emission_color, { AdobeTokens->emissiveColor, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->fuzz_color, { AdobeTokens->sheenColor, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->fuzz_roughness, { AdobeTokens->sheenRoughness, SdfValueTypeNames->Float } },
    { OpenPbrTokens->geometry_coat_normal, { AdobeTokens->clearcoatNormal, SdfValueTypeNames->Float3 } },
    { OpenPbrTokens->geometry_normal, { AdobeTokens->normal, SdfValueTypeNames->Float3 } },
    { OpenPbrTokens->geometry_opacity, { AdobeTokens->opacity, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->specular_anisotropy, { AdobeTokens->anisotropyLevel, SdfValueTypeNames->Float } },
    { OpenPbrTokens->specular_color, { AdobeTokens->specularColor, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->specular_ior, { AdobeTokens->ior, SdfValueTypeNames->Float } },
    { OpenPbrTokens->specular_rotation, { AdobeTokens->anisotropyAngle, SdfValueTypeNames->Float } },
    { OpenPbrTokens->specular_roughness, { AdobeTokens->roughness, SdfValueTypeNames->Float } },
    { OpenPbrTokens->specular_weight, { AdobeTokens->specularLevel, SdfValueTypeNames->Float } },
    { OpenPbrTokens->subsurface_color, { AdobeTokens->scatteringColor, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->subsurface_radius, { AdobeTokens->scatteringDistance, SdfValueTypeNames->Float } },
    { OpenPbrTokens->transmission_color, { AdobeTokens->absorptionColor, SdfValueTypeNames->Color3f } },
    { OpenPbrTokens->transmission_depth, { AdobeTokens->absorptionDistance, SdfValueTypeNames->Float } },
    { OpenPbrTokens->transmission_weight, { AdobeTokens->transmission, SdfValueTypeNames->Float } },
};

// clang-format on

void
_setShaderType(SdfAbstractData* data, const SdfPath& shaderPath, const TfToken& shaderType)
{
    SdfPath p = createAttributeSpec(
      data, shaderPath, UsdShadeTokens->infoId, SdfValueTypeNames->Token, SdfVariabilityUniform);
    setAttributeDefaultValue(data, p, shaderType);
}

SdfPath
_createShaderAttr(SdfAbstractData* data,
                  const SdfPath& shaderPath,
                  const TfToken& attrName,
                  const SdfValueTypeName& attrType,
                  const SdfPath& connectionSourcePath = {});

SdfPath
_createShaderAttr(SdfAbstractData* data,
                  const SdfPath& shaderPath,
                  const TfToken& attrName,
                  const SdfValueTypeName& attrType,
                  const SdfPath& connectionSourcePath)
{
    SdfPath attrPath = createAttributeSpec(data, shaderPath, attrName, attrType);
    if (!connectionSourcePath.IsEmpty()) {
        appendAttributeConnection(data, attrPath, connectionSourcePath);
    }

    return attrPath;
}

} // end anonymous namespace

namespace adobe::usd {

const MinMaxVtValuePair*
getMaterialInputRange(const TfToken& input)
{
    auto it = inputRanges.find(input);
    return (it == inputRanges.cend()) ? nullptr : &(it->second);
}

const InputToMaterialInputTypeMap&
getUsdPreviewSurfaceInputRemapping()
{
    return usdPreviewSurfaceInputRemapping;
}

const InputToMaterialInputTypeMap&
getAsmInputRemapping()
{
    return asmInputRemapping;
}

const InputToMaterialInputTypeMap&
getMaterialXInputRemapping()
{
    return materialXInputRemapping;
}

SdfPath
createMaterialPrimSpec(SdfAbstractData* data,
                       const SdfPath& parentPath,
                       const TfToken& materialName)
{
    return createPrimSpec(data, parentPath, materialName, UsdShadeTokens->Material);
}

SdfPath
createShaderPrimSpec(SdfAbstractData* data,
                     const SdfPath& parentPath,
                     const TfToken& shaderName,
                     const TfToken& shaderType)
{
    SdfPath shaderPath = createPrimSpec(data, parentPath, shaderName, UsdShadeTokens->Shader);
    _setShaderType(data, shaderPath, shaderType);
    return shaderPath;
}

SdfPath
inputPath(const SdfPath& primPath, const std::string& inputName)
{
    return primPath.AppendProperty(TfToken("inputs:" + inputName));
}

SdfPath
outputPath(const SdfPath& primPath, const std::string& outputName)
{
    return primPath.AppendProperty(TfToken("outputs:" + outputName));
}

SdfPath
createShaderInput(SdfAbstractData* data,
                  const SdfPath& shaderPath,
                  const std::string& inputName,
                  const SdfValueTypeName& inputType,
                  const SdfPath& connectionSourcePath)
{
    TfToken inputToken("inputs:" + inputName);
    return _createShaderAttr(data, shaderPath, inputToken, inputType, connectionSourcePath);
}

SdfPath
createShaderOutput(SdfAbstractData* data,
                   const SdfPath& shaderPath,
                   const std::string& outputName,
                   const SdfValueTypeName& outputType,
                   const SdfPath& connectionSourcePath)
{
    TfToken outputToken("outputs:" + outputName);
    return _createShaderAttr(data, shaderPath, outputToken, outputType, connectionSourcePath);
}

void
setRangeMetadata(SdfAbstractData* sdfData, const SdfPath& inputPath, const MinMaxVtValuePair& range)
{
    VtDictionary customData;
    customData["range"] =
      VtDictionary({ { AdobeTokens->min, range.first }, { AdobeTokens->max, range.second } });
    setAttributeMetadata(sdfData, inputPath, SdfFieldKeys->CustomData, VtValue(customData));
}

SdfPath
addMaterialInputValue(SdfAbstractData* sdfData,
                      const SdfPath& materialPath,
                      const TfToken& name,
                      const SdfValueTypeName& type,
                      const VtValue& value,
                      MaterialInputs& materialInputs)
{
    auto ret = materialInputs.insert({ name.GetString(), SdfPath() });
    // If the insert took place, we need to set the value of the map to the real path
    if (ret.second) {
        TfToken inputToken("inputs:" + name.GetString());
        SdfPath path = _createShaderAttr(sdfData, materialPath, inputToken, type);
        setAttributeDefaultValue(sdfData, path, value);
        (*ret.first).second = path;
        return path;
    }
    return (*ret.first).second;
}

SdfPath
addMaterialInputTexture(SdfAbstractData* sdfData,
                        const SdfPath& materialPath,
                        const TfToken& name,
                        const std::string& texturePath,
                        MaterialInputs& materialInputs)
{

    VtValue value = VtValue(SdfAssetPath(texturePath));
    return addMaterialInputValue(
      sdfData, materialPath, name, SdfValueTypeNames->Asset, value, materialInputs);
}

SdfPath
createShader(SdfAbstractData* data,
             const SdfPath& parentPath,
             const TfToken& shaderName,
             const TfToken& shaderType,
             const std::string& outputName,
             const InputValues& inputValues,
             const InputConnections& inputConnections,
             const InputColorSpaces& inputColorSpaces)
{
    SdfPathVector outputPaths = createShader(data,
                                             parentPath,
                                             shaderName,
                                             shaderType,
                                             StringVector{ outputName },
                                             inputValues,
                                             inputConnections,
                                             inputColorSpaces);
    return !outputPaths.empty() ? outputPaths[0] : SdfPath();
}

SdfPathVector
createShader(SdfAbstractData* data,
             const SdfPath& parentPath,
             const TfToken& shaderName,
             const TfToken& shaderType,
             const StringVector& outputNames,
             const InputValues& inputValues,
             const InputConnections& inputConnections,
             const InputColorSpaces& inputColorSpaces)
{
    const auto it = shaderInfos.find(shaderType);
    if (it == shaderInfos.end()) {
        TF_WARN("Unsupported shader type %s", shaderType.GetText());
        return SdfPathVector();
    }
    const ShaderInfo& shaderInfo = it->second;

    SdfPath shaderPath = createShaderPrimSpec(data, parentPath, shaderName, shaderType);

    SdfPathVector outputPaths;
    outputPaths.reserve(outputNames.size());
    for (const std::string& outputName : outputNames) {
        TfToken outputToken("outputs:" + outputName);
        SdfValueTypeName outputType = shaderInfo.getOutputType(outputToken);
        SdfPath outputPath = _createShaderAttr(data, shaderPath, outputToken, outputType);
        outputPaths.push_back(outputPath);
    }

    for (const auto& [inputName, inputValue] : inputValues) {
        if (!inputValue.IsEmpty()) {
            TfToken inputToken("inputs:" + inputName);
            SdfValueTypeName inputType = shaderInfo.getInputType(inputToken);
            SdfPath p = _createShaderAttr(data, shaderPath, inputToken, inputType);
            setAttributeDefaultValue(data, p, inputValue);

            // Set the colorSpace metadata if we a specific value for this input
            const auto it = inputColorSpaces.find(inputName);
            if (it != inputColorSpaces.end()) {
                setAttributeMetadata(data, p, SdfFieldKeys->ColorSpace, VtValue(it->second));
            }
        }
    }
    for (const auto& [inputName, inputConnection] : inputConnections) {
        if (!inputConnection.IsEmpty()) {
            TfToken inputToken("inputs:" + inputName);
            SdfValueTypeName inputType = shaderInfo.getInputType(inputToken);
            SdfPath p = _createShaderAttr(data, shaderPath, inputToken, inputType, inputConnection);

            // Set the colorSpace metadata if we a specific value for this input
            const auto it = inputColorSpaces.find(inputName);
            if (it != inputColorSpaces.end()) {
                setAttributeMetadata(data, p, SdfFieldKeys->ColorSpace, VtValue(it->second));
            }
        }
    }

    return outputPaths;
}

}
