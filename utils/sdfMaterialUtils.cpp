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
#include "sdfUtils.h"

#include <pxr/usd/usdShade/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace { // anonymous namespace

using namespace adobe::usd;

struct ShaderInfo
{
    std::unordered_map<TfToken, SdfValueTypeName, TfToken::HashFunctor> inputTypes;
    std::unordered_map<TfToken, SdfValueTypeName, TfToken::HashFunctor> outputTypes;

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
        { TfToken("inputs:varname"), SdfValueTypeNames->Token },
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

}

namespace adobe::usd {

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
