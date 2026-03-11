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

/// @brief OpenPBR material struct
/// This is based on OpenPBR 1.0
/// https://github.com/AcademySoftwareFoundation/OpenPBR/blob/44fe76650880914980402221672446ad44df15bd/reference/open_pbr_surface.mtlx
///
/// The latest version can be found here (currently at 1.1)
/// https://github.com/AcademySoftwareFoundation/OpenPBR/blob/main/reference/open_pbr_surface.mtlx
///
/// Note that there are additions at the bottom that are not from the OpenPBR spec, but that are
/// useful extensions to carry additional information that is important for the transcoding of
/// materials, especially for the backwards compatibility with ASM.
struct USDFFUTILS_API OpenPbrMaterial
{
    std::string name;
    std::string displayName;

    // Note, the naming convention here follows the OpenPBR input names
    Input base_weight;
    Input base_color;
    Input base_diffuse_roughness;
    Input base_metalness;
    Input specular_weight;
    Input specular_color;
    Input specular_roughness;
    Input specular_ior;
    Input specular_roughness_anisotropy;
    Input transmission_weight;
    Input transmission_color;
    Input transmission_depth;
    Input transmission_scatter;
    Input transmission_scatter_anisotropy;
    Input transmission_dispersion_scale;
    Input transmission_dispersion_abbe_number;
    Input subsurface_weight;
    Input subsurface_color;
    Input subsurface_radius;
    Input subsurface_radius_scale;
    Input subsurface_scatter_anisotropy;
    Input fuzz_weight;
    Input fuzz_color;
    Input fuzz_roughness;
    Input coat_weight;
    Input coat_color;
    Input coat_roughness;
    Input coat_roughness_anisotropy;
    Input coat_ior;
    Input coat_darkening;
    Input thin_film_weight;
    Input thin_film_thickness;
    Input thin_film_ior;
    Input emission_luminance;
    Input emission_color;
    Input geometry_opacity;
    Input geometry_thin_walled;
    Input geometry_normal;
    Input geometry_coat_normal;
    Input geometry_tangent;
    Input geometry_coat_tangent;

    /// The OpenPBR spec is only concerned with BXDF properties and hence does not have a
    /// displacement input. But this can be expressed in MaterialX via displacement shader and
    /// directly in other material models.
    Input displacement;

    /// An occlusion signal is sometimes available for renderers that do implement their own global
    /// illumination
    Input occlusion;

    /// This is an ASM concept, which is hard to express in OpenPBR as the anisotropy direction is
    /// derived from the tangent and not a texturable input of the angle.
    /// We're keeping this for now until we have an actual transfer mechanism.
    Input anisotropyAngle;

    /// This is an ASM concept, to control the strength of the specular reflection of the coat.
    /// In OpenPBR some of this control is available via the coat_ior, but the equation is not
    /// trivial and coat_ior or coatSpecularLevel could be a constant or textured
    Input coatSpecularLevel;

    /// This is an ASM concept, with no correspondence in OpenPBR. It is designed for real-time
    /// rasterizers to have an approximate notion of the depth of a absorbing/scattering object.
    Input volumeThickness;

    /// This is an ASM concept, which can also be expressed via the scale of the normal Input.
    /// We have it here for backwards compatibility, but should consider removing it.
    float normalScale = 1.0f;

    /// This is a flag used by UsdPreviewSurface to switch between a metallic workflow, where the
    /// specular color is derived from the base_color and a workflow that has an explicit
    /// specular_color.
    bool useSpecularWorkflow = false;

    /// This float value is used by UsdPreviewSurface to express alpha masking based on an opacity
    /// texture that is thresholded by this value. If this is zero, normal opacity is used. If this
    /// larger than 0.0 the masking will be used. This maps to the alphaCutoff value in GLTF.
    float opacityThreshold = 0.0f;

    // Import of transmission from GLTF can activate the clearcoat lobe to model tinting of
    // transmission, which ASM doesn't do automatically. If this was activated on import, we do
    // not want to export clearcoat to GLTF again.
    bool clearcoatModelsTransmissionTint = false;

    // Since USD doesn't support glTF unlit materials, we convert them on import to emissive. We
    // keep this information, and store it as metadata in the file, so we can convert it back on
    // export
    bool isUnlit = false;
};

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
