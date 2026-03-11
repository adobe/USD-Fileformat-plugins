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
#pragma once
#include "layerRead.h"

#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/shader.h>

#include <vector>

namespace adobe::usd {

/// Reading UsdShade material networks can get complicated, so we've created some machinery here to
/// make it easier to setup the parsing of these networks. The type of network topology we expect
/// is something like this:
///
///                                    +-------------+
///                                    |             |
///                       Constant --> |             |
///                                    |             |
///  +-----------+     +---------+     |             |
///  | TexCoords | --> | TexRead | --> |   Surface   |
///  +-----------+     +---------+     |   Shader    | --> Material
///                                    |             |
///  +-----------+     +---------+     |             |
///  | TexCoords | --> | TexRead | --> |             |
///  +-----------+     +---------+     |             |
///                                    |             |
///                                    +-------------+
///
/// We have a central surface shader (UsdPreviewSurface, ASM or OpenPBR) and for its inputs we
/// expect either:
///  1. No input
///  2. A constant value (can use UsdShade connections)
///  3. A small linear chain of shading nodes that express the reading of a texture.
///
/// The no input (1.) and constant (2.) cases are pretty straight forward, but the texture reading
/// case (3.) can get pretty complicated, even when we assume a linear chain of nodes (no branching
/// or multiple connected inputs per node).
///
/// The complication arises from UsdPreviewSurface and ASM using one set of nodes to express the
/// texture reading (Usd* shading nodes), while OpenPBR uses a different set of nodes (MaterialX).
/// We can have more than the depicted minimal case of two nodes in the chain. We can also have
/// optional nodes that might or might not be present.
///
///
/// So to make this more managable we've split the parsing of the texture chains into two concerns:
///  1. The types of the nodes, the order in which they are expected and whether they're optional
///  2. Extracting and converting the settings on a node and following to the next in the chain
///
/// The 2. part is done via handler functions (ShaderHandler below), which deal with a single
/// type (or class of nodes). There needs to be one such function for each type of node we might
/// encounter. That handler is given the UsdShadeOutput that is used downstream in the network.
///
/// \example Shader handler function for the UsdPrimvarReader node
/// ```cpp
/// bool
/// handleUsdPrimvarReader(InputContext& ctx, const UsdShadeOutput& shaderOutput)
/// {
///     UsdShadeShader shader(shaderOutput.GetPrim());
///
///     std::string texCoordPrimvarStr;
///     getShaderInputValue(shader, AdobeTokens->varname, texCoordPrimvarStr);
///
///     // Rest of node processing to fill in ctx.input
///
///     return true;
/// }
/// ```
///
/// The 1. part is done via a ShaderHandlerMappings vector, which is an ordered list of expected
/// shader types with their respective handler functions and whether that shader type is optional or
/// not.
///
/// \example Shader handler mapping for Usd* type nodes on UsdPreviewSurface or ASM
/// ```cpp
/// ShaderHandlerMappings handlers = {
///     { {TfToken("UsdPrimvarReader_float2")}, handleUsdPrimvarReader },
///     { {TfToken("UsdTransform2d")}, handleUsdTransform2d, kOptional },
///     { {TfToken("UsdUVTexture")}, handleUsdUVTexture }
/// };
/// ```
///
/// With these two pieces of information (and a bit more) an InputContext can be constructed, which
/// can then be given to readSurfaceInput() to either read a constant value or extract a textured
/// input by following a shader chain.
///
/// \example Invoke the readSurfaceInput() function to parse an input on a surface shader
/// ```cpp
/// // Read diffuse color input and populate the material.diffuseColor Input struct
/// InputContext ctx{readLayerContext, TfToken("diffuseColor"), handlers, material.diffuseColor};
/// readSurfaceInput(ctx, surfaceShader);
/// ```
///
/// The shader handler implementations can use followConnectedInput() to follow one of their inputs
/// (e.g. texture coordinate input) up the chain.

/// Each node handler gets called with the InputContext and the output attribute on shader node
using ShaderHandler = std::function<bool(struct InputContext&, const PXR_NS::UsdShadeOutput&)>;

/// Constant to use when declaring a shader handler as optional. This is clearer than a raw "true".
constexpr bool kOptional = true;

/// The handler mapping struct expresses that all nodes listed in nodeNames should be handled by
/// this handler function. If isOptional is true, this type of nodes and its handler can be skipped.
/// Non-optional nodes must not be skipped.
struct USDFFUTILS_API ShaderHandlerMapping
{
    PXR_NS::TfTokenVector nodeNames;
    ShaderHandler handler;
    bool isOptional = false;
};

/// We have a linear order of expected ShaderHandlerMapping. Some mappings are optional and can be
/// skipped, but for each input we expect to travers a linear sequence of nodes in order.
using ShaderHandlerMappings = std::vector<ShaderHandlerMapping>;

/// Small context struct to simplify the node handler interface
/// * readLayerContext is needed when reading texture images
/// * surfaceInputName is the name of the original surface input we're tracing (mostly for errors)
/// * handlerMappings is used to decide which code to call when encountering a certain node type
/// * input is a reference to the Input struct we're filling in for this chain of nodes
/// * handlerIndex is the position in the handlerMappings from where the search should start
///   This index is monotonically increasing as we traverse the linear chain of nodes.
///   Optional handlers are checked, but might be skipped, and each manadatory handler is check and
///   if the right shader type is not found triggers an error.
struct USDFFUTILS_API InputContext
{
    ReadLayerContext& readLayerContext;
    const PXR_NS::TfToken& surfaceInputName;
    const ShaderHandlerMappings& handlerMappings;
    Input& input;
    uint32_t handlerIndex = 0;
};

/// Given an InputContext check if the surface has that input, handle the case of a constant value
/// or if connected, trigger the upstream processing.
USDFFUTILS_API bool
readSurfaceInput(InputContext& ctx, const PXR_NS::UsdShadeShader& surface);

/// Given an InputContext, a shader and an input name, follow the connection on that input and
/// trigger the shader handling upstream. Returns false if either nothing was connected or if the
/// upstream handling failed.
/// This function is meant to be used by individual shader handlers to follow the input chain.
USDFFUTILS_API bool
followConnectedInput(InputContext& ctx,
                     const PXR_NS::UsdShadeShader& shader,
                     const PXR_NS::TfToken& inputName);

/// Reads an image specified by the assetPath and stores it in the ReadLayerContext.
/// Returns the image index in the internal store. If the image has been found before, it returns
/// the previous index.
USDFFUTILS_API int
readImage(ReadLayerContext& ctx, const PXR_NS::SdfAssetPath& assetPath);

/// Checks if the shader has the named input and if so extracts the associated default value.
/// Returns true if a value was extracted. Note, that this will follow UsdShade connections to
/// constant values.
template<typename T>
bool
getShaderInputValue(const PXR_NS::UsdShadeShader& shader, const PXR_NS::TfToken& name, T& value)
{
    PXR_NS::UsdShadeInput input = shader.GetInput(name);
    if (input) {
        PXR_NS::UsdShadeAttributeVector valueAttrs = input.GetValueProducingAttributes();
        if (!valueAttrs.empty()) {
            const PXR_NS::UsdAttribute& attr = valueAttrs.front();
            if (PXR_NS::UsdShadeInput::IsInput(attr)) {
                return attr.Get(&value);
            }
        }
    }
    return false;
}

}
