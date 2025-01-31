/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include "sbsarMaterial.h"
#include "sbsarAsm.h"
#include "sbsarMtlx.h"
#include "sbsarUsdPreviewSurface.h"
#include "usdGenerationHelpers.h"
#include <iostream>
#include <sbsarDebug.h>
#include <sbsarEngine/sbsarRenderThread.h>

#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/tokens.h>

// File format utils
#include <fileformatutils/sdfMaterialUtils.h>
#include <fileformatutils/sdfUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace SubstanceAir;

namespace {

using namespace adobe::usd;
using namespace adobe::usd::sbsar;

void
setupPysicalSize(SdfAbstractData* sdfData,
                 const SdfPath& materialPath,
                 const SubstanceAir::GraphDesc& graphDesc,
                 SymbolMapper& symbolMapper)
{
    const MappedSymbol paramName = symbolMapper.GetSymbol("physicalsize");
    SdfPath paramPath =
      createShaderOutput(sdfData, materialPath, paramName.usdName, SdfValueTypeNames->Float3);
    // Note, we set the default value of the physical size output attribute to the static value
    // from the metadata of the graph description. But this value can be computed by the graph as
    // an additional output from its computation.
    // TODO the dynamically computed evaluation of the physical size is not yet implemented.
    GfVec3f physicalSize(
      graphDesc.mPhysicalSize.x, graphDesc.mPhysicalSize.y, graphDesc.mPhysicalSize.z);
    setAttributeDefaultValue(sdfData, paramPath, physicalSize);
    TF_DEBUG(FILE_FORMAT_SBSAR)
      .Msg("setupPysicalSize: %f %f %f\n",
           graphDesc.mPhysicalSize.x,
           graphDesc.mPhysicalSize.y,
           graphDesc.mPhysicalSize.z);
}

//! \brief Initialize all texture inputs of the given material including texture asset paths
//! This is not considering the current sbsarParameters and leaves the asset paths empty
//! \param sdfData          Sdf layer data.
//! \param materialPath     Material to initialize.
//! \param graphDesc        Description of the current sbsar graph.
//! @param graphName        Graph name
//! @param sbsarHash        Hash of the sbsar.
void
initDefaultMaterialInputs(SdfAbstractData* sdfData,
                          const SdfPath& materialPath,
                          const SubstanceAir::GraphDesc& graphDesc,
                          const MappedSymbol& graphName,
                          size_t sbsarHash)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("initDefaultMaterialInputs: Creating material inputs\n");

    NormalFormat normalFormat = getDefaultNormalFormat(graphDesc);

    for (const auto& usage : mapped_usages) {
        if (hasUsage(usage, graphDesc)) {
            std::string textureAssetName = getTextureAssetName(usage);
            SdfPath textureAssetPath =
              createShaderInput(sdfData, materialPath, textureAssetName, SdfValueTypeNames->Asset);
            setAttributeMetadata(sdfData, textureAssetPath, SdfFieldKeys->Hidden, VtValue(true));
            // Not setting a default value here, so that it has to be overwritten in the payload
            // reference
        }
        auto defaultIt = default_channels.find(usage);
        if (defaultIt != default_channels.end()) {
            auto names = getDefaultValueNames(usage);

            SdfPath inputPath =
              createShaderInput(sdfData, materialPath, names.first, defaultIt->second.type);
            setAttributeDefaultValue(sdfData, inputPath, defaultIt->second.value);
            setRangeMetadata(sdfData, inputPath, defaultIt->second.range);
            setAttributeMetadata(sdfData, inputPath, SdfFieldKeys->Hidden, VtValue(true));

            SdfPath textureBlendPath =
              createShaderInput(sdfData, materialPath, names.second, SdfValueTypeNames->Float);
            setAttributeDefaultValue(sdfData, textureBlendPath, 1.0f);
            setRangeMetadata(sdfData, textureBlendPath, { VtValue(0.0f), VtValue(1.0f) });
            setAttributeMetadata(sdfData, textureBlendPath, SdfFieldKeys->Hidden, VtValue(true));
        }
        if (isNormal(usage)) {
            const auto [scaleName, biasName] = getNormalMapScaleAndBiasNames(usage);
            SdfPath scaleAttrPath =
              createShaderInput(sdfData, materialPath, scaleName, SdfValueTypeNames->Float4);
            SdfPath biasAttrPath =
              createShaderInput(sdfData, materialPath, biasName, SdfValueTypeNames->Float4);
            setAttributeMetadata(sdfData, scaleAttrPath, SdfFieldKeys->Hidden, VtValue(true));
            setAttributeMetadata(sdfData, biasAttrPath, SdfFieldKeys->Hidden, VtValue(true));

            const auto [scale, bias] = getNormalMapScaleAndBias(normalFormat);
            setAttributeDefaultValue(sdfData, scaleAttrPath, scale);
            setAttributeDefaultValue(sdfData, biasAttrPath, bias);
        }
    }
}

//! \brief Set the texture inputs to the procedural texture paths based on the sbsarParameters
//! \param sdfData          Sdf layer data.
//! \param materialPath     Material to initialize.
//! \param graphDesc        Description of the current sbsar graph.
//! @param graphName        Graph name
//! @param sbsarHash        Hash of the sbsar.
//! @param sbsarParameters  Sbsar parameters used to generate texture asset path.
void
setMaterialTexturePaths(SdfAbstractData* sdfData,
                        const SdfPath& materialPath,
                        const SubstanceAir::GraphDesc& graphDesc,
                        const MappedSymbol& graphName,
                        size_t sbsarHash,
                        const JsValue& jsParams)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("setMaterialTexturePaths\n");
    for (const auto& usage : mapped_usages) {
        if (hasUsage(usage, graphDesc)) {
            std::string textureAssetName = getTextureAssetName(usage);
            SdfPath textureAssetPath =
              createShaderInput(sdfData, materialPath, textureAssetName, SdfValueTypeNames->Asset);
            std::string sbsarPath = generateSbsarInfoPath(usage, graphName, sbsarHash, jsParams);

            // The "./" makes the path anchored on this layer and it is resolved relative to it
            // inside of the same SBSAR package.
            SdfAssetPath path = SdfAssetPath("./" + sbsarPath);
            setAttributeDefaultValue(sdfData, textureAssetPath, path);
        }
    }
}

void
setMaterialValues(SdfAbstractData* sdfData,
                  const SdfPath& materialPath,
                  const SubstanceAir::GraphDesc& graphDesc,
                  const MappedSymbol& graphName,
                  size_t sbsarHash,
                  const JsValue& jsParams,
                  const std::string& packagePath)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("setMaterialOutputValues\n");
    for (const auto& usage : uniform_usages) {
        if (hasUsage(usage, graphDesc)) {
            auto defaultIt = default_channels.find(usage);
            if (defaultIt != default_channels.end()) {
                std::string textureAssetName = usage;
                SdfPath textureAssetPath = createShaderInput(
                  sdfData, materialPath, textureAssetName, defaultIt->second.type);
                std::string infoPath = generateSbsarInfoPath(usage, graphName, sbsarHash, jsParams);
                TF_DEBUG(FILE_FORMAT_SBSAR).Msg("Using engine to get value for %s", usage.c_str());
                setAttributeDefaultValue(
                  sdfData, textureAssetPath, renderSbsarValue(packagePath, infoPath));
            }
        }
    }
}

void
setMaterialNormalScaleAndBias(SdfAbstractData* sdfData,
                              const SdfPath& materialPath,
                              const SubstanceAir::GraphDesc& graphDesc,
                              const JsValue& jsParams)
{
    // If we don't have concrete information on the normal format, we don't author an explict scale
    // and bias to adjust for that and instead rely on the default that was authored with the
    // default material inputs.
    NormalFormat normalFormat = determineNormalFormat(jsParams);
    if (normalFormat == NormalFormat::Unknown) {
        return;
    }

    // If the current format matches the default, there is nothing to be done
    NormalFormat defaultNormalFormat = getDefaultNormalFormat(graphDesc);
    if (normalFormat == defaultNormalFormat) {
        return;
    }

    // The scale and bias needs to be authored for each normal map usage
    for (const auto& usage : normal_usages) {
        if (hasUsage(usage, graphDesc)) {
            const auto [scaleName, biasName] = getNormalMapScaleAndBiasNames(usage);
            SdfPath scaleAttrPath =
              createShaderInput(sdfData, materialPath, scaleName, SdfValueTypeNames->Float4);
            SdfPath biasAttrPath =
              createShaderInput(sdfData, materialPath, biasName, SdfValueTypeNames->Float4);
            const auto [scale, bias] = getNormalMapScaleAndBias(normalFormat);
            setAttributeDefaultValue(sdfData, scaleAttrPath, scale);
            setAttributeDefaultValue(sdfData, biasAttrPath, bias);
        }
    }
}

//! \brief Add transform inputs to the given material.
//! \param sdfData       Sdf layer data.
//! \param materialPath  Path to the material
void
addMaterialTransform(SdfAbstractData* sdfData, const SdfPath& materialPath)
{
    SdfPath uvScalePath =
      createShaderInput(sdfData, materialPath, uv_scale_input, SdfValueTypeNames->Float2);
    setAttributeDefaultValue(sdfData, uvScalePath, GfVec2f(1.0f, 1.0f));

    SdfPath uvRotationPath =
      createShaderInput(sdfData, materialPath, uv_rotation_input, SdfValueTypeNames->Float);
    setAttributeDefaultValue(sdfData, uvRotationPath, 0.0f);

    SdfPath uvTranslationPath =
      createShaderInput(sdfData, materialPath, uv_translation_input, SdfValueTypeNames->Float2);
    setAttributeDefaultValue(sdfData, uvTranslationPath, GfVec2f(0.0f, 0.0f));
}

//! \brief Add standard material networks according to the compilation options.
//!  The standard material networks create only connections with the main material.
//! \param sdfData       Sdf layer data.
//! \param materialPath  Path to the parent material
//! \param graphDesc     Description of the current sbsar graph.
void
addStandardMaterial(SdfAbstractData* sdfData,
                    const SdfPath& materialPath,
                    const SubstanceAir::GraphDesc& graphDesc,
                    const SBSAROptions& options)
{

    bool isRefractive = hasUsage("refraction", graphDesc);

#ifdef USDSBSAR_ENABLE_TEXTURE_TRANSFORM
    addMaterialTransform(sdfData, materialPath);
#endif // USDSBSAR_ENABLE_TEXTURE_TRANSFORM

    // Set the default UV channel name
    SdfPath uvChannelNamePath =
      createShaderInput(sdfData, materialPath, uv_channel_name, SdfValueTypeNames->String);
    setAttributeDefaultValue(sdfData, uvChannelNamePath, std::string("st"));
    setAttributeMetadata(sdfData, uvChannelNamePath, SdfFieldKeys->Hidden, VtValue(true));

    // Add ASM Implementation
    if (options.writeASM) {
        addAsmShader(sdfData, materialPath, graphDesc);
    }

    if (isRefractive) {
        // Add Refractive UsdPreviewSurface Implementation
        if (options.writeUsdPreviewSurface) {
            addUsdPreviewSurfaceRefractive(sdfData, materialPath, graphDesc);
        }
        // Add Refractive MaterialX Implementation
        if (options.writeMaterialX) {
            addMtlxShaderRefractive(sdfData, materialPath, graphDesc);
        }
    }

    else {
        // Add UsdPreviewSurface Implementation
        if (options.writeUsdPreviewSurface) {
            addUsdPreviewSurface(sdfData, materialPath, graphDesc);
        }
        // Add Refractive MaterialX Implementation
        if (options.writeMaterialX) {
            addMtlxShader(sdfData, materialPath, graphDesc);
        }
    }
}

} // namespace

namespace adobe::usd::sbsar {

SdfPath
addMaterialPrim(SdfAbstractData* sdfData,
                const MappedSymbol& graphName,
                const SubstanceAir::GraphDesc& graphDesc,
                const std::string& packagePath,
                const SdfPath& classPath,
                size_t sbsarHash,
                SymbolMapper& symbolMapper,
                const SBSAROptions& sbsarData)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("addMaterialPrim: Depth: %i\n", sbsarData.depth);

    const SdfPath rootPath = SdfPath::AbsoluteRootPath();
    SdfPath materialPath;
    if (sbsarData.depth == 0) {
        // Create a prototype material as an "over", which does not instantiate an actual prim in
        // the scene. On this prototype create everything except the variants with their sbsar
        // parameter overrides and the final procedural texture paths.
        SdfPath refMaterialPath = createPrimSpec(sdfData,
                                                 rootPath,
                                                 TfToken(graphName.usdName + "_prototype"),
                                                 TfToken(),
                                                 SdfSpecifier::SdfSpecifierOver);
        // Mark prototype prim as active=false, so that it is discarded when the stage is flattened
        setPrimMetadata(sdfData, refMaterialPath, SdfFieldKeys->Active, VtValue(false));

        setGraphMetadataOnPrim(sdfData, refMaterialPath, graphDesc);

        // Create the definition of all of the procedural parameters with default values
        setupProceduralParameters(sdfData, refMaterialPath, graphDesc.mInputs, symbolMapper);
        // Create the physical size output attribute
        setupPysicalSize(sdfData, refMaterialPath, graphDesc, symbolMapper);
        // Create all material inputs shared by the different material network implementations
        // Note, that texture asset paths are empty, since we can't use the procedural parameters
        // yet.
        initDefaultMaterialInputs(sdfData, refMaterialPath, graphDesc, graphName, sbsarHash);
        // Create all the different material networks
        addStandardMaterial(sdfData, refMaterialPath, graphDesc, sbsarData);

        // Now create the actual material prim that references the prototype
        // This makes sure the opinions in the protoype are weaker than in the variants and the
        // variants can override any of the procedural parameters with their preset values.
        materialPath = createMaterialPrimSpec(sdfData, rootPath, TfToken(graphName.usdName));
        addPrimInherit(sdfData, materialPath, classPath);
        addPrimReference(sdfData, materialPath, SdfReference("", refMaterialPath));
        setPrimMetadata(sdfData, materialPath, SdfFieldKeys->Active, VtValue(true));

        if (hasInput("$outputsize", graphDesc)) {
            // Add the default resolution variant choice
            // we're authoring the variant choice on the referenced material path,
            // which is the prototype of the material and not the actual material prim
            addResolutionVariantSelection(sdfData, refMaterialPath);
            // Due to a bug in USD (in 23.08), the attributes in a variant are not found by the
            // PcpDynamicFileFormatContext::ComposeAttributeDefaultValue method. So to allow the use
            // of variants, we store the payload in the variant metadata instead of the material
            // prim metadata. So the variant must be nested instead of side by side. It works but it
            // generates more asset paths than necessary. See
            // https://groups.google.com/g/usd-interest/c/mUJ64KpU9cU/m/Hf3n7OQFAwAJ
            addResolutionVariantSet(
              sdfData, symbolMapper, graphDesc, packagePath, materialPath, materialPath);
        } else {
            TF_DEBUG(FILE_FORMAT_SBSAR)
              .Msg("addMaterialPrim: '$outputsize' input is not exposed : skip resolution variant "
                   "creation");
            addPresetVariant(
              sdfData, symbolMapper, graphDesc, packagePath, materialPath, materialPath);
        }

    } else if (sbsarData.depth == 1) {
        SdfPath materialPath =
          createMaterialPrimSpec(sdfData, rootPath, TfToken(graphName.usdName));
        // process usd sbsarParameters into a js dict
        JsValue jsParams = convertSbsarParameters(sbsarData.sbsarParameters);
        // Set the procedural texture paths based on the sbsarParameters
        setMaterialTexturePaths(sdfData, materialPath, graphDesc, graphName, sbsarHash, jsParams);
        // Set procedural values for uniform usage
        setMaterialValues(
          sdfData, materialPath, graphDesc, graphName, sbsarHash, jsParams, packagePath);
        // Set normal scale and bias depending on the normal format
        setMaterialNormalScaleAndBias(sdfData, materialPath, graphDesc, jsParams);
    }

    return materialPath;
}

SdfPath
addClassPrim(SdfAbstractData* sdfData, const TfToken& className, const TfToken& classType)
{
    const SdfPath rootPath = SdfPath::AbsoluteRootPath();
    SdfPath classPath =
      createPrimSpec(sdfData, rootPath, className, classType, SdfSpecifier::SdfSpecifierClass);
    return classPath;
}

} // namespace UsdSbsar
