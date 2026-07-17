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
#include "sdfUtils.h"
#include "usdData.h"

#include <pxr/pxr.h>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/sdf/path.h>

#include <unordered_map>

namespace adobe::usd {

struct WriteLayerOptions
{
    WriteLayerOptions()
    {
        applyMaterialModelDefaults(writeUsdPreviewSurface, writeASM, writeOpenPBR);
    }
    WriteLayerOptions(const PXR_NS::FileFormatDataBase& fileFormatData)
      : writeUsdPreviewSurface(fileFormatData.writeUsdPreviewSurface)
      , writeASM(fileFormatData.writeASM)
      , writeOpenPBR(fileFormatData.writeOpenPBR)
      , preserveExtraMaterialInfo(fileFormatData.preserveExtraMaterialInfo)
      , assetsPath(fileFormatData.assetsPath)
    {}

    bool writeUsdPreviewSurface = true;
    bool writeASM = true;
    bool writeOpenPBR = false;
    bool preserveExtraMaterialInfo = true;
    bool pruneJoints = false;
    bool animationTracks = false;
    bool createRenderSettingsPrim = false;
    std::string assetsPath;
};

struct WriteSdfContext
{
    const WriteLayerOptions* options;
    PXR_NS::SdfAbstractData* sdfData;
    const UsdData* usdData;

    PXR_NS::SdfPathVector nodeMap;
    PXR_NS::SdfPathVector materialMap;
    PXR_NS::SdfPathVector skeletonMap;
    PXR_NS::SdfPathVector meshPrototypeMap;
    PXR_NS::SdfPathVector lightMap;

    // Per-parent registry of already-used child prim names, keyed by parent prim path. Used to
    // uniquify node names at write time against siblings the import-time uniquify pass cannot see
    // (e.g. the synthesized "Materials" scope). Routes through UniqueNameEnforcer ->
    // _makeUniqueAndAdd so the suffix algorithm stays single-sourced.
    std::unordered_map<PXR_NS::SdfPath, UniqueNameEnforcer, PXR_NS::SdfPath::Hash>
      childNameEnforcers;

    std::string srcAssetFilename;
    std::string debugTag;
};

USDFFUTILS_API std::string
getSTPrimvarAttrName(int uvIndex);

USDFFUTILS_API int
parseIntEnding(const std::string& str);

USDFFUTILS_API int
getSTPrimvarTokenIndex(PXR_NS::TfToken token);

USDFFUTILS_API PXR_NS::TfToken
getSTPrimvarAttrToken(int uvIndex);

USDFFUTILS_API PXR_NS::TfToken
getSTTexCoordReaderToken(int uvIndex);

USDFFUTILS_API PXR_NS::VtValue
getTextureZeroVtValue(const PXR_NS::TfToken& channel);

USDFFUTILS_API std::string
createTexturePath(const std::string& srcAssetFilename, const std::string& imageUri);

/// @brief Converts a Material struct into an OpenPbrMaterial struct
///
/// It implements a channel-by-channel mapping where there is a correspondence between the
/// UsdPreviewSurface and ASM channels in the Material struct and the OpenPBR inputs. It also
/// transfers many channels that do not exist in OpenPBR, but that are required to implement
/// previous behaviors. The documentation for these is on the OpenPbrMaterial struct.
USDFFUTILS_API OpenPbrMaterial
mapMaterialStructToOpenPbrMaterialStruct(const Material& material);

/// @brief Converts an OpenPbrMaterial struct into a Material struct
///
/// This implements the inverse of mapMaterialStructToOpenPbrMaterialStruct()
USDFFUTILS_API Material
mapOpenPbrMaterialStructToMaterialStruct(const OpenPbrMaterial& material);

/// @brief Create custom attributes to carry extra non-OpenPBR fields
///
/// This covers: normalScale, useSpecularWorkflow, opacityThreshold, clearcoatModelsTransmissionTint
/// and isUnlit
USDFFUTILS_API void
createExtraConstantAttribute(PXR_NS::SdfAbstractData* sdfData,
                             const OpenPbrMaterial& material,
                             const PXR_NS::SdfPath& surfaceShaderPath);

/// OpenPBR emission values are in nits, but ASM value are not scaled to the surface area. As an
/// approximate conversion we apply a factor to get the output into a usable range.
static constexpr float kAsmToOpenPbrEmissionFactor = 1000.0f;
static constexpr float kOpenPbrToAsmEmissionFactor = 1.0f / kAsmToOpenPbrEmissionFactor;

}
