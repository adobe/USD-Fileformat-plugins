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

#include "api.h"
#include "common.h"
#include "debugCodes.h"
#include "sdfUtils.h"

#include <pxr/pxr.h>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/usdShade/tokens.h>

#include <unordered_map>

namespace adobe::usd {

/// These helpers are build on the sdfUtils helpers and provide convenience utilities to create the
/// specs for material networks.

/// Create a material prim spec
USDFFUTILS_API PXR_NS::SdfPath
createMaterialPrimSpec(PXR_NS::SdfAbstractData* data,
                       const PXR_NS::SdfPath& parentPrimPath,
                       const PXR_NS::TfToken& materialName);

/// Create a shader prim spec and set the shader type
USDFFUTILS_API PXR_NS::SdfPath
createShaderPrimSpec(PXR_NS::SdfAbstractData* data,
                     const PXR_NS::SdfPath& parentPrimPath,
                     const PXR_NS::TfToken& shaderName,
                     const PXR_NS::TfToken& shaderType);

/// Get the path of an input attribute of the given name on the given prim
///
/// Note, this applies to Material, NodeGraph and Shader prims
USDFFUTILS_API PXR_NS::SdfPath
inputPath(const PXR_NS::SdfPath& primPath, const std::string& inputName);

/// Get the path of an output attribute of the given name on the given prim
///
/// Note, this applies to Material, NodeGraph and Shader prims
USDFFUTILS_API PXR_NS::SdfPath
outputPath(const PXR_NS::SdfPath& primPath, const std::string& outputName);

/// Create an shader input attribute spec
///
/// If connectionSourcePath is valid, the new input attribute is connected to this attribute path
USDFFUTILS_API PXR_NS::SdfPath
createShaderInput(PXR_NS::SdfAbstractData* data,
                  const PXR_NS::SdfPath& shaderPath,
                  const std::string& inputName,
                  const PXR_NS::SdfValueTypeName& inputType,
                  const PXR_NS::SdfPath& connectionSourcePath = {});

/// Create an shader output attribute spec
///
/// If connectionSourcePath is valid, the new output attribute is connected to this attribute path
USDFFUTILS_API PXR_NS::SdfPath
createShaderOutput(PXR_NS::SdfAbstractData* data,
                   const PXR_NS::SdfPath& shaderPath,
                   const std::string& outputName,
                   const PXR_NS::SdfValueTypeName& outputType,
                   const PXR_NS::SdfPath& connectionSourcePath = {});

/// Specialized version of std::pair<std::string, VtValue>
struct KeyVtValuePair
{
    std::string first;
    PXR_NS::VtValue second;

    KeyVtValuePair(const std::string& key, const PXR_NS::VtValue& value)
      : first(key)
      , second(value)
    {
    }

    // Convenient constructor to create the key from char* and the VtValue from an arbitrarily typed
    // value
    template<typename T>
    KeyVtValuePair(const char* key, const T& value)
      : first(key)
      , second(value)
    {
    }
};

struct InputTypePair
{
    PXR_NS::TfToken name;          // name of input
    PXR_NS::SdfValueTypeName type; // type of input
};

using StringVector = std::vector<std::string>;
using InputValues = std::vector<KeyVtValuePair>;
using InputConnections = std::vector<std::pair<std::string, PXR_NS::SdfPath>>;
using InputColorSpaces = std::unordered_map<std::string, PXR_NS::TfToken>;

using MinMaxVtValuePair = std::pair<PXR_NS::VtValue, PXR_NS::VtValue>; // min/max

/// The map used to lookup the name of a material input variable to get the path
/// and check if the value has already been added
using MaterialInputs = std::unordered_map<std::string, PXR_NS::SdfPath>;

/// Map used to lookup a shading model input to find the corresponding material level input and its
/// type. This is used to create the material level input variable.
using InputToMaterialInputTypeMap =
  std::unordered_map<PXR_NS::TfToken, InputTypePair, PXR_NS::TfToken::HashFunctor>;

/// Add CustomData min/max range values on attribute
USDFFUTILS_API void
setRangeMetadata(PXR_NS::SdfAbstractData* sdfData,
                 const PXR_NS::SdfPath& inputPath,
                 const MinMaxVtValuePair& range);

/// Add an input value to the material prim
USDFFUTILS_API PXR_NS::SdfPath
addMaterialInputValue(PXR_NS::SdfAbstractData* sdfData,
                      const PXR_NS::SdfPath& materialPath,
                      const PXR_NS::TfToken& name,
                      const PXR_NS::SdfValueTypeName& type,
                      const PXR_NS::VtValue& value,
                      MaterialInputs& materialInputs);

/// Add an input texture to the material prim and add new input to MaterialInputs map
/// to prevent duplicates.
USDFFUTILS_API PXR_NS::SdfPath
addMaterialInputTexture(PXR_NS::SdfAbstractData* sdfData,
                        const PXR_NS::SdfPath& materialPath,
                        const PXR_NS::TfToken& name,
                        const std::string& texturePath,
                        MaterialInputs& materialInputs);

/// Create shader prim spec with inputs and one output
///
/// This one-stop-shop will create a shader prim spec and set the shader type. It will also create a
/// single output attribute of name outputName. It will create input attributes and set them to a
/// default value for every entry in inputValues. It will create input attribute and connect them
/// for every entry in inputConnections. The function returns the attribute path of the output
/// attribute.
///
/// This function uses an internal table of most common shaders from the UsdPreviewSurface and
/// MaterialX networks to determine the types of input and output attributes. The function will fail
/// if the shaderType is not supported. It will issue warnings and skip inputs or outputs it does
/// not recognize.
USDFFUTILS_API PXR_NS::SdfPath
createShader(PXR_NS::SdfAbstractData* data,
             const PXR_NS::SdfPath& parentPath,
             const PXR_NS::TfToken& shaderName,
             const PXR_NS::TfToken& shaderType,
             const std::string& outputName,
             const InputValues& inputValues = {},
             const InputConnections& inputConnections = {},
             const InputColorSpaces& inputColorSpaces = {});

/// Overload of the above function that can create multiple outputs. It returns a vector of the
/// generated output paths
USDFFUTILS_API PXR_NS::SdfPathVector
createShader(PXR_NS::SdfAbstractData* data,
             const PXR_NS::SdfPath& parentPath,
             const PXR_NS::TfToken& shaderName,
             const PXR_NS::TfToken& shaderType,
             const StringVector& outputNames,
             const InputValues& inputValues = {},
             const InputConnections& inputConnections = {},
             const InputColorSpaces& inputColorSpaces = {});

using TokenToSdfValueTypeMap = std::unordered_map<PXR_NS::TfToken, PXR_NS::SdfValueTypeName, PXR_NS::TfToken::HashFunctor>;

struct ShaderInfo
{
    TokenToSdfValueTypeMap inputTypes;
    TokenToSdfValueTypeMap outputTypes;

    PXR_NS::SdfValueTypeName
    getInputType(const PXR_NS::TfToken& inputName) const;

    PXR_NS::SdfValueTypeName
    getOutputType(const PXR_NS::TfToken& outputName) const;
};

// Table of shaders with input and outputs and their respective types
// This table is used to make createShader extra convenient to use.
// The data here is essentially a mini form of the shader schemas. If we're concerned about this
// staying up-to-date we could investigate gathering this information at run-time via the
// shader definition registry (Sdr) module. Unfortunate, the ASM terminal nodes are not found there.
class ShaderRegistry {
public:
    static ShaderRegistry&
    getInstance() {
        static ShaderRegistry m_instance;
        return m_instance;
    }

    /// Return the shader info tokens
    const std::map<PXR_NS::TfToken, ShaderInfo>&
    getShaderInfos() const {
        return m_shaderInfos;
    }

    /// Given a token for a material input, return a pointer (possibly null) to the range
    const MinMaxVtValuePair*
    getMaterialInputRange(const PXR_NS::TfToken& input) const {
        auto it = m_inputRanges.find(input);
        return (it == m_inputRanges.cend()) ? nullptr : &(it->second);
    }

    /// Return UsdPreviewSurface shader inputs to material inputs map
    const InputToMaterialInputTypeMap&
    getUsdPreviewSurfaceInputRemapping() const {
        return m_usdPreviewSurfaceInputRemapping;
    }

    /// Return ASM shader inputs to material inputs map
    const InputToMaterialInputTypeMap&
    getAsmInputRemapping() const {
        return m_asmInputRemapping;
    }

    /// Return MaterialX shader inputs to material inputs map
    const InputToMaterialInputTypeMap&
    getMaterialXInputRemapping() const {
        return m_materialXInputRemapping;
    }

private:
    ShaderRegistry();
    ~ShaderRegistry() = default;

    // Prohibit copy constructor and assignment operator
    ShaderRegistry(const ShaderRegistry&) = delete;
    ShaderRegistry& operator=(const ShaderRegistry&) = delete;

    std::map<PXR_NS::TfToken, ShaderInfo> m_shaderInfos;
    std::unordered_map<PXR_NS::TfToken, MinMaxVtValuePair, PXR_NS::TfToken::HashFunctor> m_inputRanges;
    InputToMaterialInputTypeMap m_usdPreviewSurfaceInputRemapping;
    InputToMaterialInputTypeMap m_asmInputRemapping;
    InputToMaterialInputTypeMap m_materialXInputRemapping;
};

}
