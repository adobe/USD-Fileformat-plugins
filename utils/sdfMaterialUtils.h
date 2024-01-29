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

#include <pxr/pxr.h>
#include <pxr/usd/sdf/abstractData.h>

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

using StringVector = std::vector<std::string>;
using InputValues = std::vector<KeyVtValuePair>;
using InputConnections = std::vector<std::pair<std::string, PXR_NS::SdfPath>>;
using InputColorSpaces = std::unordered_map<std::string, PXR_NS::TfToken>;

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

}
