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
#include <fileformatutils/layerReadMaterialUtils.h>

#include <fileformatutils/debugCodes.h>
#include <fileformatutils/images.h>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd {

// Fetches the first value-producing attribute connected to a given shader input.
// If 'expectShader' is true, verify that the connected source is a shader and that the connection
// exists. Returns true and sets outAttribute if a suitable attribute is found.
bool
_fetchPrimaryConnectedAttribute(const UsdShadeInput& shadeInput,
                                UsdAttribute& outAttribute,
                                bool expectShader)
{
    if (expectShader) {
        if (!shadeInput.HasConnectedSource()) {
            TF_WARN("Input %s has no connected source.", shadeInput.GetAttr().GetPath().GetText());
            return false;
        }
    }
    UsdShadeAttributeVector attrs = shadeInput.GetValueProducingAttributes();
    if (attrs.empty()) {
        return false;
    }
    if (attrs.size() > 1) {
        TF_WARN("Input %s is connected to multiple producing attributes, only the first will be "
                "processed.",
                shadeInput.GetAttr().GetPath().GetText());
    }
    outAttribute = attrs.front();
    if (expectShader) {
        if (UsdShadeInput::IsInput(outAttribute)) {
            TF_WARN("Input %s is connected to an attribute that is not a shader output.",
                    shadeInput.GetAttr().GetPath().GetText());
            return false;
        }
    }
    return true;
}

// Given an InputContext and a shader output, find the right handler based on the type of the shader
// node and call that handler. Return false if the output was not valid, a handler could not be
// found or the upstream handling failed.
bool
_handleShader(InputContext& ctx, const UsdShadeOutput& shaderOutput)
{
    UsdShadeShader shader(shaderOutput.GetPrim());
    if (!shader) {
        TF_WARN("Prim %s is not a shader prim (input %s)",
                shader.GetPath().GetText(),
                ctx.surfaceInputName.GetText());
        return false;
    }

    TfToken shaderId;
    shader.GetShaderId(&shaderId);

    TF_DEBUG_MSG(
      FILE_FORMAT_UTIL, "Handle shader %s at %s", shaderId.GetText(), shader.GetPath().GetText());

    if (ctx.handlerIndex >= ctx.handlerMappings.size()) {
        TF_CODING_ERROR("handlerIndex out of range");
        return false;
    }

    const uint32_t numHandlers = ctx.handlerMappings.size();
    for (; ctx.handlerIndex < numHandlers; ++ctx.handlerIndex) {
        const ShaderHandlerMapping& handlerMapping = ctx.handlerMappings[ctx.handlerIndex];
        for (const TfToken& handlerId : handlerMapping.nodeNames) {
            if (handlerId == shaderId) {
                // Advance the index, now that we've found this handler
                ctx.handlerIndex++;
                return handlerMapping.handler(ctx, shaderOutput);
            }
        }

        // We can't continue over a non-optional node
        if (!handlerMapping.isOptional) {
            TF_WARN(
              "Expected shader of type %s (or equivalent) but got %s for prim at %s (input %s)",
              handlerMapping.nodeNames[0].GetText(),
              shaderId.GetText(),
              shader.GetPath().GetText(),
              ctx.surfaceInputName.GetText());
            return false;
        }
    }

    TF_WARN("Unexpected shader of type %s for prim at %s (input %s)",
            shaderId.GetText(),
            shader.GetPath().GetText(),
            ctx.surfaceInputName.GetText());

    return false;
}

bool
readSurfaceInput(InputContext& ctx, const UsdShadeShader& surface)
{
    UsdShadeInput shadeInput = surface.GetInput(ctx.surfaceInputName);
    if (!shadeInput) {
        return true;
    }

    // fetchPrimaryConnectedAttribute will return the current shadeInput as an attribute if there is
    // no connection, but the attribute exists
    UsdAttribute attr;
    if (_fetchPrimaryConnectedAttribute(shadeInput, attr, false)) {
        if (UsdShadeInput::IsInput(attr)) {
            // Attempt to retrieve the constant value from the attribute. Not having a value is the
            // same as the input attribute not existing
            attr.Get(&ctx.input.value);
        } else {
            return _handleShader(ctx, UsdShadeOutput(attr));
        }
    }

    return true;
}

bool
followConnectedInput(InputContext& ctx, const UsdShadeShader& shader, const TfToken& inputName)
{
    UsdShadeInput input = shader.GetInput(inputName);
    if (!input) {
        TF_WARN("No input %s on node %s (input %s)",
                inputName.GetText(),
                shader.GetPath().GetText(),
                ctx.surfaceInputName.GetText());
        return false;
    }

    UsdAttribute attr;
    if (_fetchPrimaryConnectedAttribute(input, attr, true)) {
        return _handleShader(ctx, UsdShadeOutput(attr));
    } else {
        TF_WARN("Expected valid shader input on %s for node %s (input %s)",
                inputName.GetText(),
                shader.GetPath().GetText(),
                ctx.surfaceInputName.GetText());
        return false;
    }
}

// Populates the absolute path, base name, and sanitized extension for an SBSAR asset by resolving
// the absolute path from the provided URI.
void
_populatePathPartsFromAssetPath(const SdfAssetPath& path,
                                std::string& resolvedAssetPath,
                                std::string& filePath,
                                std::string& name,
                                std::string& extension,
                                bool warnAboutUnresolvedAssets)
{
    // Make sure we have a resolved path, either coming from SdfAssetPath value or by running it
    // throught the resolver.
    resolvedAssetPath = path.GetResolvedPath();
    if (resolvedAssetPath.empty()) {
        resolvedAssetPath = ArGetResolver().Resolve(path.GetAssetPath());
        if (resolvedAssetPath.empty()) {
            // As a fallback we continue with the unresolved path
            resolvedAssetPath = path.GetAssetPath();
            if (warnAboutUnresolvedAssets) {
                TF_WARN("Continuing with unresolved path '%s'", resolvedAssetPath.c_str());
            }
        }
    }

    // This will extract the inner most path to the asset:
    // path/to/package.usdz[path/to/image.png] -> path/to/image.png
    std::string innerAssetPath = getLayerFilePath(resolvedAssetPath);
    // This helper function will detect "funky" paths, like those to SBSAR images and convert them
    // to good usable file paths
    filePath = extractFilePathFromAssetPath(innerAssetPath);
    // Strip the path part since we only want the filename and the extension
    std::string baseName = TfGetBaseName(filePath);
    name = TfStringGetBeforeSuffix(baseName);
    extension = TfGetExtension(baseName);
}

int
readImage(ReadLayerContext& ctx, const SdfAssetPath& assetPath)
{
    std::string resolvedAssetPath, filePath, name, extension;
    _populatePathPartsFromAssetPath(
      assetPath, resolvedAssetPath, filePath, name, extension, ctx.warnAboutMissingAssets);

    // Check in the cache if we've processed this image before
    if (const auto& it = ctx.images.find(resolvedAssetPath); it != ctx.images.end()) {
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Image (cached): %s\n",
                     ctx.debugTag.c_str(),
                     resolvedAssetPath.c_str());
        return it->second;
    }

    // The image is new. Make sure we don't get name collisions in the short name
    if (const auto& itName = ctx.imageNames.find(name); itName != ctx.imageNames.end()) {
        itName->second++;
        name += "_" + std::to_string(itName->second);
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "%s: Deduplicated image name: %s\n",
                     ctx.debugTag.c_str(),
                     name.c_str());
    } else {
        ctx.imageNames[name] = 1;
    }

    auto [imageIndex, image] = ctx.usd->addImage();
    if (extension == "sbsarimage") {
        // SBSAR images are a special cases where the data is stored raw and must be transcoded to a
        // different image in memory
        extension = getSbsarImageExtension(resolvedAssetPath);
        transcodeImageAssetToMemory(resolvedAssetPath, image.uri, image.image);
    } else {
        auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedAssetPath));
        if (asset) {
            image.image.resize(asset->GetSize());
            memcpy(image.image.data(), asset->GetBuffer().get(), asset->GetSize());
        } else {
            if (ctx.warnAboutMissingAssets) {
                TF_WARN("%s: Unable to open asset: %s\n",
                        ctx.debugTag.c_str(),
                        resolvedAssetPath.c_str());
            }
        }
    }

    image.name = name;
    image.uri = filePath;
    image.format = getFormat(extension);
    ctx.images[resolvedAssetPath] = imageIndex;

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: Image (new): index: %d uri: %s\n",
                 ctx.debugTag.c_str(),
                 imageIndex,
                 resolvedAssetPath.c_str());

    return imageIndex;
}

}
