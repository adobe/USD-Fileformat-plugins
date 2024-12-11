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
#include "sbsarLuxDomeLight.h"
#include "usdGenerationHelpers.h"
#include <sbsarDebug.h>

#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdLux/tokens.h>

// File format utils
#include <fileformatutils/sdfMaterialUtils.h>
#include <fileformatutils/sdfUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace SubstanceAir;

namespace adobe::usd::sbsar {

SdfPath
addLuxDomeLight(SdfAbstractData* sdfData,
                const MappedSymbol& graphName,
                const SubstanceAir::GraphDesc& graphDesc,
                const std::string& packagePath,
                size_t sbsarHash,
                SymbolMapper& symbolMapper,
                const SBSAROptions& sbsarData)
{
    TF_DEBUG(FILE_FORMAT_SBSAR).Msg("addLuxDomeLight: Depth: %i\n", sbsarData.depth);

    const SdfPath rootPath = SdfPath::AbsoluteRootPath();
    SdfPath lightPath;
    if (sbsarData.depth == 0) {
        // Create a prototype light as an "over", which does not instantiate an actual prim in
        // the scene. On this prototype create everything except the variants with their sbsar
        // parameter overrides and the final procedural texture paths.
        SdfPath refLightPath = createPrimSpec(sdfData,
                                              rootPath,
                                              TfToken(graphName.usdName + "_prototype"),
                                              TfToken(),
                                              SdfSpecifier::SdfSpecifierOver);
        // Mark prototype prim as active=false, so that it is discarded when the stage is flattened
        setPrimMetadata(sdfData, refLightPath, SdfFieldKeys->Active, VtValue(false));

        setGraphMetadataOnPrim(sdfData, refLightPath, graphDesc);

        // Create the definition of all of the procedural parameters with default values
        setupProceduralParameters(
          sdfData, refLightPath, graphDesc.mInputs, symbolMapper, /*isEnvironmentTexture*/ true);

        // Now create the actual light prim that references the prototype
        // This makes sure the opinions in the protoype are weaker than in the variants and the
        // variants can override any of the procedural parameters with their preset values.
        lightPath =
          createPrimSpec(sdfData, rootPath, TfToken(graphName.usdName), UsdLuxTokens->DomeLight);
        addPrimReference(sdfData, lightPath, SdfReference("", refLightPath));
        setPrimMetadata(sdfData, lightPath, SdfFieldKeys->Active, VtValue(true));

        // Due to a bug in USD (in 23.08), the attributes in a variant are not found by the
        // PcpDynamicFileFormatContext::ComposeAttributeDefaultValue method. So to allow the use of
        // variants, we store the payload in the variant metadata instead of the material prim
        // metadata. So the variant must be nested instead of side by side. It works but it
        // generates more asset paths than necessary. See
        // https://groups.google.com/g/usd-interest/c/mUJ64KpU9cU/m/Hf3n7OQFAwAJ
        addResolutionVariantSet(sdfData,
                                symbolMapper,
                                graphDesc,
                                packagePath,
                                lightPath,
                                lightPath,
                                /*isEnvironmentTexture*/ true);
        addResolutionVariantSelection(sdfData, lightPath, true);
    } else if (sbsarData.depth == 1) {
        lightPath =
          createPrimSpec(sdfData, rootPath, TfToken(graphName.usdName), UsdLuxTokens->DomeLight);
        SdfPath texAttrPath =
          createShaderInput(sdfData, lightPath, "texture:file", SdfValueTypeNames->Asset);
        JsValue params = convertSbsarParameters(sbsarData.sbsarParameters);
        SdfAssetPath path =
          SdfAssetPath(generateSbsarInfoPath("environment", graphName, sbsarHash, params));
        setAttributeMetadata(sdfData, texAttrPath, SdfFieldKeys->Hidden, VtValue(true));
        setAttributeDefaultValue(sdfData, texAttrPath, path);
    }

    return lightPath;
}

}
