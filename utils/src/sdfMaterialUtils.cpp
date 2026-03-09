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
#include <fileformatutils/sdfMaterialUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace { // anonymous namespace

using namespace adobe::usd;

void
_setShaderType(SdfAbstractData* data, const SdfPath& shaderPath, const TfToken& shaderType)
{
    SdfPath p = createAttributeSpec(
      data, shaderPath, UsdShadeTokens->infoId, SdfValueTypeNames->Token, SdfVariabilityUniform);
    setAttributeDefaultValue(data, p, shaderType, SdfValueTypeNames->Token);
}

SdfPath
_createShaderAttr(SdfAbstractData* data,
                  const SdfPath& shaderPath,
                  const TfToken& attrName,
                  const SdfValueTypeName& attrType,
                  const SdfPath& connectionSourcePath = {})
{
    SdfPath attrPath = createAttributeSpec(data, shaderPath, attrName, attrType);
    if (!connectionSourcePath.IsEmpty()) {
        appendAttributeConnection(data, attrPath, connectionSourcePath);
    }

    return attrPath;
}

} // end anonymous namespace

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
    const auto [it, inserted] = materialInputs.insert({ name.GetString(), SdfPath() });
    // If the insert took place, we need to set the value of the map to the real path
    if (inserted) {
        TfToken inputToken("inputs:" + name.GetString());
        SdfPath path = _createShaderAttr(sdfData, materialPath, inputToken, type);
        setAttributeDefaultValue(sdfData, path, value, type);
        it->second = path;
        return path;
    }
    return it->second;
}

SdfPath
addMaterialInputTexture(SdfAbstractData* sdfData,
                        const SdfPath& materialPath,
                        const TfToken& name,
                        const std::string& texturePath,
                        bool isColorTexture,
                        MaterialInputs& materialInputs)
{
    VtValue value = VtValue(SdfAssetPath(texturePath));
    TfToken texturePathInputName(name.GetString() + "Texture");
    SdfPath inputAttrPath = addMaterialInputValue(
      sdfData, materialPath, texturePathInputName, SdfValueTypeNames->Asset, value, materialInputs);
    // We set the color space on the attribute that will carry the texture asset path
    // This is an important clue for the OpenPBR/MaterialX shading network, as the texture
    // reading nodes there do not have a field for the color space. That space is specified
    // on the attribute that holds the asset path.
    // XXX: In the future we should switch these tokens to GfColorSpaceNames, which
    // specifies a larger set of color spaces. For now we stick to "srgb_texture" and "raw", which
    // are MaterialX supported and recognized color space names.
    const TfToken& colorSpace = isColorTexture ? MtlXTokens->srgb_texture : AdobeTokens->raw;
    setAttributeMetadata(sdfData, inputAttrPath, SdfFieldKeys->ColorSpace, VtValue(colorSpace));
    return inputAttrPath;
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
    auto shaderInfos = ShaderRegistry::getInstance().getShaderInfos();
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
            setAttributeDefaultValue(data, p, inputValue, inputType);

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

SdfValueTypeName
ShaderInfo::getInputType(const TfToken& inputName) const
{
    const auto it = inputTypes.find(inputName);
    if (it != inputTypes.end()) {
        return it->second;
    }
    TF_WARN("Couldn't find type for input %s", inputName.GetText());
    return SdfValueTypeNames->Token;
}

SdfValueTypeName
ShaderInfo::getOutputType(const TfToken& outputName) const
{
    const auto it = outputTypes.find(outputName);
    if (it != outputTypes.end()) {
        return it->second;
    }
    TF_WARN("Couldn't find type for output %s", outputName.GetText());
    return SdfValueTypeNames->Token;
}

ShaderRegistry::ShaderRegistry()
{
    // Initialize shaderInfos
    // clang-format off
    m_shaderInfos = {
    { AdobeTokens->UsdUVTexture, {{
        { TfToken("inputs:file"), SdfValueTypeNames->Asset },
        { TfToken("inputs:st"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:wrapS"), SdfValueTypeNames->Token },
        { TfToken("inputs:wrapT"), SdfValueTypeNames->Token },
        { TfToken("inputs:minFilter"), SdfValueTypeNames->Token },
        { TfToken("inputs:magFilter"), SdfValueTypeNames->Token },
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
    { MtlXTokens->ND_convert_color3_vector3, {{
        { TfToken("inputs:in"), SdfValueTypeNames->Color3f }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float3 }
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
    { MtlXTokens->ND_mix_color3, {{
        { TfToken("inputs:fg"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:bg"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:mix"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Color3f }
    }}},
    { MtlXTokens->ND_add_float, {{
        { TfToken("inputs:in1"), SdfValueTypeNames->Float },
        { TfToken("inputs:in2"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float }
    }}},
    { MtlXTokens->ND_subtract_float, {{
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
    { MtlXTokens->ND_UsdUVTexture_23, {{
        { TfToken("inputs:file"), SdfValueTypeNames->Asset },
        { TfToken("inputs:st"), SdfValueTypeNames->Float2 },
        { TfToken("inputs:wrapS"), SdfValueTypeNames->String },
        { TfToken("inputs:wrapT"), SdfValueTypeNames->String },
        { TfToken("inputs:fallback"), SdfValueTypeNames->Float4 },
        { TfToken("inputs:scale"), SdfValueTypeNames->Float4 },
        { TfToken("inputs:bias"), SdfValueTypeNames->Float4 }
    }, {
        { TfToken("outputs:r"), SdfValueTypeNames->Float },
        { TfToken("outputs:g"), SdfValueTypeNames->Float },
        { TfToken("outputs:b"), SdfValueTypeNames->Float },
        { TfToken("outputs:a"), SdfValueTypeNames->Float },
        { TfToken("outputs:rgb"), SdfValueTypeNames->Color3f }
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
        { TfToken("inputs:in"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:scale"), SdfValueTypeNames->Float },
        { TfToken("inputs:normal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:tangent"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:bitangent"), SdfValueTypeNames->Float3 }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float3 }
    }}},
    { MtlXTokens->ND_displacement_float, {{
        { TfToken("inputs:displacement"), SdfValueTypeNames->Float },
        { TfToken("inputs:scale"), SdfValueTypeNames->Float }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Token }
    }}},
    { MtlXTokens->ND_geompropvalue_vector2, {{
        { TfToken("inputs:geomprop"), SdfValueTypeNames->String },
        { TfToken("inputs:default"), SdfValueTypeNames->Float2 }
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Float2 }
    }}},
    { MtlXTokens->ND_open_pbr_surface_surfaceshader, {{
        { TfToken("inputs:base_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:base_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:base_diffuse_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:base_metalness"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:specular_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_ior"), SdfValueTypeNames->Float },
        { TfToken("inputs:specular_roughness_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:transmission_depth"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_scatter"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:transmission_scatter_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_dispersion_scale"), SdfValueTypeNames->Float },
        { TfToken("inputs:transmission_dispersion_abbe_number"), SdfValueTypeNames->Float },
        { TfToken("inputs:subsurface_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:subsurface_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:subsurface_radius"), SdfValueTypeNames->Float },
        { TfToken("inputs:subsurface_radius_scale"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:subsurface_scatter_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:fuzz_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:fuzz_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:fuzz_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:coat_roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_roughness_anisotropy"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_ior"), SdfValueTypeNames->Float },
        { TfToken("inputs:coat_darkening"), SdfValueTypeNames->Float },
        { TfToken("inputs:thin_film_weight"), SdfValueTypeNames->Float },
        { TfToken("inputs:thin_film_thickness"), SdfValueTypeNames->Float },
        { TfToken("inputs:thin_film_ior"), SdfValueTypeNames->Float },
        { TfToken("inputs:emission_luminance"), SdfValueTypeNames->Float },
        { TfToken("inputs:emission_color"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:geometry_opacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:geometry_thin_walled"), SdfValueTypeNames->Bool },
        { TfToken("inputs:geometry_normal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:geometry_coat_normal"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:geometry_tangent"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:geometry_coat_tangent"), SdfValueTypeNames->Float3 },
        // XXX Non-OpenPBR inputs we support for round-tripping purposes
        // XXX turn displacement into actual MaterialX displacement
        { TfToken("inputs:displacement"), SdfValueTypeNames->Float },
        { TfToken("inputs:occlusion"), SdfValueTypeNames->Float },
        { TfToken("inputs:anisotropyAngle"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatSpecularLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:volumeThickness"), SdfValueTypeNames->Float },
    }, {
        { TfToken("outputs:out"), SdfValueTypeNames->Token }
    }}},
    // Adobe Standard Material surface node
    { AdobeTokens->adobeStandardMaterial, {{
        { TfToken("inputs:baseColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:roughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:metallic"), SdfValueTypeNames->Float },
        { TfToken("inputs:opacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:specularLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:specularEdgeColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:normal"), SdfValueTypeNames->Normal3f },
        { TfToken("inputs:normalScale"), SdfValueTypeNames->Float },
        { TfToken("inputs:combineNormalAndHeight"), SdfValueTypeNames->Bool },
        { TfToken("inputs:height"), SdfValueTypeNames->Float },
        { TfToken("inputs:heightScale"), SdfValueTypeNames->Float },
        { TfToken("inputs:heightLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:anisotropyLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:anisotropyAngle"), SdfValueTypeNames->Float },
        { TfToken("inputs:emissiveIntensity"), SdfValueTypeNames->Float },
        { TfToken("inputs:emissive"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:sheenOpacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:sheenColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:sheenRoughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:translucency"), SdfValueTypeNames->Float },
        { TfToken("inputs:IOR"), SdfValueTypeNames->Float },
        { TfToken("inputs:dispersion"), SdfValueTypeNames->Float },
        { TfToken("inputs:absorptionColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:absorptionDistance"), SdfValueTypeNames->Float },
        { TfToken("inputs:scatter"), SdfValueTypeNames->Bool },
        { TfToken("inputs:scatteringColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:scatteringDistance"), SdfValueTypeNames->Float },
        { TfToken("inputs:scatteringDistanceScale"), SdfValueTypeNames->Float3 },
        { TfToken("inputs:scatteringRedShift"), SdfValueTypeNames->Float },
        { TfToken("inputs:scatteringRayleigh"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatOpacity"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatColor"), SdfValueTypeNames->Color3f },
        { TfToken("inputs:coatRoughness"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatIOR"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatSpecularLevel"), SdfValueTypeNames->Float },
        { TfToken("inputs:coatNormal"), SdfValueTypeNames->Normal3f },
        { TfToken("inputs:coatNormalScale"), SdfValueTypeNames->Float },
        { TfToken("inputs:ambientOcclusion"), SdfValueTypeNames->Float },
        { TfToken("inputs:volumeThickness"), SdfValueTypeNames->Float },
        { TfToken("inputs:volumeThicknessScale"), SdfValueTypeNames->Float },
    }, {
        { TfToken("outputs:surface"), SdfValueTypeNames->Token }
    }}}
    };

    // Initialize inputRanges
    // Note, *Scale inputs don't have a range limit. Neither do absorptionDistance,
    // scatteringDistance, emissiveIntensity, scatteringRedShift, scatteringRayleigh
    m_inputRanges = {
        { AsmTokens->ambientOcclusion, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->anisotropyAngle, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->anisotropyLevel, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->coatIOR, { VtValue(1.0), VtValue(3.0) } },
        { AsmTokens->coatOpacity, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->coatRoughness, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->coatSpecularLevel, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->dispersion, { VtValue(0.0), VtValue(1.0) } }, // Apparently it can go as high as 20
        { AsmTokens->height, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->heightLevel, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->IOR, { VtValue(1.0), VtValue(3.0) } },
        { AsmTokens->metallic, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->opacity, { VtValue(0.0), VtValue(1.0) } },
        { UsdPreviewSurfaceTokens->opacityThreshold, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->roughness, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->sheenOpacity, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->sheenRoughness, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->specularLevel, { VtValue(0.0), VtValue(1.0) } },
        { AsmTokens->translucency, { VtValue(0.0), VtValue(1.0) } },
        { UsdPreviewSurfaceTokens->useSpecularWorkflow, { VtValue(0), VtValue(1) } },
        { AsmTokens->volumeThickness, { VtValue(0.0),VtValue(1.0) } },
    };

    // Initialize usdPreviewSurfaceInputRemapping
    m_usdPreviewSurfaceInputRemapping = {
        { UsdPreviewSurfaceTokens->clearcoat, { AsmTokens->coatOpacity, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->clearcoatRoughness, { AsmTokens->coatRoughness, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->diffuseColor, { AsmTokens->baseColor, SdfValueTypeNames->Color3f } },
        { UsdPreviewSurfaceTokens->displacement, { AsmTokens->height, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->emissiveColor, { AsmTokens->emissive, SdfValueTypeNames->Color3f } },
        { UsdPreviewSurfaceTokens->ior, { AsmTokens->IOR, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->metallic, { AsmTokens->metallic, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->normal, { AsmTokens->normal, SdfValueTypeNames->Normal3f } },
        { UsdPreviewSurfaceTokens->occlusion, { AsmTokens->ambientOcclusion, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->opacity, { AsmTokens->opacity, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->opacityThreshold, { UsdPreviewSurfaceTokens->opacityThreshold, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->roughness, { AsmTokens->roughness, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->specularColor, { AsmTokens->specularEdgeColor, SdfValueTypeNames->Color3f } },
        { UsdPreviewSurfaceTokens->useSpecularWorkflow, { UsdPreviewSurfaceTokens->useSpecularWorkflow, SdfValueTypeNames->Int } }
    };

    // Initialize asmInputRemapping
    // XXX This is incomplete
    m_asmInputRemapping = {
        { AsmTokens->absorptionColor, { AsmTokens->absorptionColor, SdfValueTypeNames->Color3f } },
        { AsmTokens->absorptionDistance, { AsmTokens->absorptionDistance, SdfValueTypeNames->Float } },
        { AsmTokens->ambientOcclusion, { AsmTokens->ambientOcclusion, SdfValueTypeNames->Float } },
        { AsmTokens->anisotropyAngle, { AsmTokens->anisotropyAngle, SdfValueTypeNames->Float } },
        { AsmTokens->anisotropyLevel, { AsmTokens->anisotropyLevel, SdfValueTypeNames->Float } },
        { AsmTokens->baseColor, { AsmTokens->baseColor, SdfValueTypeNames->Color3f } },
        { AsmTokens->coatColor, { AsmTokens->coatColor, SdfValueTypeNames->Color3f } },
        { AsmTokens->coatIOR, { AsmTokens->coatIOR, SdfValueTypeNames->Float } },
        { AsmTokens->coatNormal, { AsmTokens->coatNormal, SdfValueTypeNames->Normal3f } },
        { AsmTokens->coatOpacity, { AsmTokens->coatOpacity, SdfValueTypeNames->Float } },
        { AsmTokens->coatRoughness, { AsmTokens->coatRoughness, SdfValueTypeNames->Float } },
        { AsmTokens->coatSpecularLevel, { AsmTokens->coatSpecularLevel, SdfValueTypeNames->Float } },
        { AsmTokens->dispersion, { AsmTokens->dispersion, SdfValueTypeNames->Float } },
        { AsmTokens->emissiveIntensity, { AsmTokens->emissiveIntensity, SdfValueTypeNames->Float } },
        { AsmTokens->emissive, { AsmTokens->emissive, SdfValueTypeNames->Color3f } },
        { AsmTokens->height, { AsmTokens->height, SdfValueTypeNames->Float } },
        { AsmTokens->heightScale, { AsmTokens->heightScale, SdfValueTypeNames->Float } },
        { AsmTokens->IOR, { AsmTokens->IOR, SdfValueTypeNames->Float } },
        { AsmTokens->metallic, { AsmTokens->metallic, SdfValueTypeNames->Float } },
        { AsmTokens->normal, { AsmTokens->normal, SdfValueTypeNames->Normal3f } },
        { AsmTokens->normalScale, { AsmTokens->normalScale, SdfValueTypeNames->Float} },
        { AsmTokens->opacity, { AsmTokens->opacity, SdfValueTypeNames->Float } },
        { AsmTokens->roughness, { AsmTokens->roughness, SdfValueTypeNames->Float } },
        { AsmTokens->scatteringColor, { AsmTokens->scatteringColor, SdfValueTypeNames->Color3f } },
        { AsmTokens->scatteringDistance, { AsmTokens->scatteringDistance, SdfValueTypeNames->Float } },
        { AsmTokens->scatteringDistanceScale, { AsmTokens->scatteringDistanceScale, SdfValueTypeNames->Float3 } },
        { AsmTokens->sheenColor, { AsmTokens->sheenColor, SdfValueTypeNames->Color3f } },
        { AsmTokens->sheenOpacity, { AsmTokens->sheenOpacity, SdfValueTypeNames->Float } },
        { AsmTokens->sheenRoughness, { AsmTokens->sheenRoughness, SdfValueTypeNames->Float } },
        { AsmTokens->specularEdgeColor, { AsmTokens->specularEdgeColor, SdfValueTypeNames->Color3f } },
        { AsmTokens->specularLevel, { AsmTokens->specularLevel, SdfValueTypeNames->Float } },
        { AsmTokens->translucency, { AsmTokens->translucency, SdfValueTypeNames->Float } },
        { AsmTokens->volumeThickness, { AsmTokens->volumeThickness, SdfValueTypeNames->Float } },
    };

    // Initialize openPbrInputRemapping
    m_openPbrInputRemapping = {
        { OpenPbrTokens->base_weight, { OpenPbrMaterialInputTokens->baseWeight, SdfValueTypeNames->Float } },
        { OpenPbrTokens->base_color, { AsmTokens->baseColor, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->base_diffuse_roughness, { OpenPbrMaterialInputTokens->baseDiffuseRoughness, SdfValueTypeNames->Float } },
        { OpenPbrTokens->base_metalness, { AsmTokens->metallic, SdfValueTypeNames->Float } },
        { OpenPbrTokens->specular_weight, { OpenPbrMaterialInputTokens->specularWeight, SdfValueTypeNames->Float } },
        { OpenPbrTokens->specular_color, { AsmTokens->specularEdgeColor, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->specular_roughness, { OpenPbrMaterialInputTokens->specularRoughness, SdfValueTypeNames->Float } },
        { OpenPbrTokens->specular_ior, { AsmTokens->IOR, SdfValueTypeNames->Float } },
        { OpenPbrTokens->specular_roughness_anisotropy, { OpenPbrMaterialInputTokens->specularRoughnessAnisotropy, SdfValueTypeNames->Float } },
        { OpenPbrTokens->transmission_weight, { OpenPbrMaterialInputTokens->transmissionWeight , SdfValueTypeNames->Float } },
        { OpenPbrTokens->transmission_color, { OpenPbrMaterialInputTokens->transmissionColor , SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->transmission_depth, { OpenPbrMaterialInputTokens->transmissionDepth , SdfValueTypeNames->Float } },
        { OpenPbrTokens->transmission_scatter, { OpenPbrMaterialInputTokens->transmissionScatter, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->transmission_scatter_anisotropy, { OpenPbrMaterialInputTokens->transmissionScatterAnisotropy, SdfValueTypeNames->Float } },
        { OpenPbrTokens->transmission_dispersion_scale, { OpenPbrMaterialInputTokens->transmissionDispersionScale, SdfValueTypeNames->Float } },
        { OpenPbrTokens->transmission_dispersion_abbe_number, { OpenPbrMaterialInputTokens->transmissionDispersionAbbeNumber, SdfValueTypeNames->Float } },
        { OpenPbrTokens->subsurface_weight, { OpenPbrMaterialInputTokens->subsurfaceWeight, SdfValueTypeNames->Float } },
        { OpenPbrTokens->subsurface_color, { OpenPbrMaterialInputTokens->subsurfaceColor, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->subsurface_radius, { OpenPbrMaterialInputTokens->subsurfaceRadius, SdfValueTypeNames->Float } },
        { OpenPbrTokens->subsurface_radius_scale, { OpenPbrMaterialInputTokens->subsurfaceRadiusScale, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->subsurface_scatter_anisotropy, { OpenPbrMaterialInputTokens->subsurfaceScatterAnisotropy, SdfValueTypeNames->Float } },
        { OpenPbrTokens->fuzz_weight, { OpenPbrMaterialInputTokens->fuzzWeight, SdfValueTypeNames->Float } },
        { OpenPbrTokens->fuzz_color, { OpenPbrMaterialInputTokens->fuzzColor, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->fuzz_roughness, { OpenPbrMaterialInputTokens->fuzzRoughness, SdfValueTypeNames->Float } },
        { OpenPbrTokens->coat_weight, { AsmTokens->coatOpacity, SdfValueTypeNames->Float } },
        { OpenPbrTokens->coat_color, { AsmTokens->coatColor, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->coat_roughness, { AsmTokens->coatRoughness, SdfValueTypeNames->Float } },
        { OpenPbrTokens->coat_roughness_anisotropy, { OpenPbrMaterialInputTokens->coatRoughnessAnisotropy, SdfValueTypeNames->Float } },
        { OpenPbrTokens->coat_ior, { AsmTokens->coatIOR, SdfValueTypeNames->Float } },
        { OpenPbrTokens->coat_darkening, { OpenPbrMaterialInputTokens->coatDarkening, SdfValueTypeNames->Float } },
        { OpenPbrTokens->thin_film_weight, { OpenPbrMaterialInputTokens->thinFilmWeight, SdfValueTypeNames->Float } },
        { OpenPbrTokens->thin_film_thickness, { OpenPbrMaterialInputTokens->thinFilmThickness, SdfValueTypeNames->Float } },
        { OpenPbrTokens->thin_film_ior, { OpenPbrMaterialInputTokens->thinFilmIOR, SdfValueTypeNames->Float } },
        { OpenPbrTokens->emission_luminance, { OpenPbrMaterialInputTokens->emissionLuminance, SdfValueTypeNames->Float } },
        { OpenPbrTokens->emission_color, { AsmTokens->emissive, SdfValueTypeNames->Color3f } },
        { OpenPbrTokens->geometry_opacity, { AsmTokens->opacity, SdfValueTypeNames->Float } },
        { OpenPbrTokens->geometry_thin_walled, { OpenPbrMaterialInputTokens->thinWalled, SdfValueTypeNames->Bool } },
        { OpenPbrTokens->geometry_normal, { AsmTokens->normal, SdfValueTypeNames->Float3 } },
        { OpenPbrTokens->geometry_coat_normal, { AsmTokens->coatNormal, SdfValueTypeNames->Float3 } },
        { OpenPbrTokens->geometry_tangent, { OpenPbrMaterialInputTokens->tangent, SdfValueTypeNames->Float3 } },
        { OpenPbrTokens->geometry_coat_tangent, { OpenPbrMaterialInputTokens->coatTangent, SdfValueTypeNames->Float3 } },
        // Non-OpenPBR inputs
        { UsdPreviewSurfaceTokens->displacement, { AsmTokens->height, SdfValueTypeNames->Float } },
        { UsdPreviewSurfaceTokens->occlusion, { AsmTokens->ambientOcclusion, SdfValueTypeNames->Float } },
        { AsmTokens->anisotropyAngle, { AsmTokens->anisotropyAngle, SdfValueTypeNames->Float } },
        { AsmTokens->coatSpecularLevel, {AsmTokens->coatSpecularLevel, SdfValueTypeNames->Float } },
        { AsmTokens->volumeThickness, { AsmTokens->volumeThickness, SdfValueTypeNames->Float } },
    };
    // clang-format on
}

}
