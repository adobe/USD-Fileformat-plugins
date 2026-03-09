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

#include "pxr/base/tf/staticTokens.h"
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/pcp/dynamicFileFormatContext.h>
#include <pxr/usd/sdf/fileFormat.h>

#include <filesystem>

#if PXR_VERSION >= 2508
#include <pxr/usd/sdf/usdaFileFormat.h>
#define FileFormatsUsdaFileFormatTokensId SdfUsdaFileFormatTokens->Id
#else
#include <pxr/usd/usd/usdaFileFormat.h>
#define FileFormatsUsdaFileFormatTokensId UsdUsdaFileFormatTokens->Id
#endif

/// We defined these tokens to skip linking to usd imaging, which is heavy.
// XXX Split this list into categories for easier maintenance
// clang-format off
#define ADOBE_TOKENS \
    (adobe) \
    (invalid) \
    (r) \
    (g) \
    (b) \
    (a) \
    (rgb) \
    (rgba) \
    (repeat) \
    (clamp) \
    (wrapS) \
    (wrapT) \
    (magFilter) \
    (minFilter) \
    (linear) \
    (nearest) \
    (linearMipmapLinear) \
    (linearMipmapNearest) \
    (nearestMipmapNearest) \
    (nearestMipmapLinear) \
    (mirror) \
    (black) \
    (useMetadata) \
    (sourceColorSpace) \
    (result) \
    (raw) \
    (sRGB) \
    (st) \
    (in) \
    (in1) \
    (in2) \
    (bg) \
    (fg) \
    (mix) \
    (file) \
    (scale) \
    (bias) \
    (fallback) \
    (rotation) \
    (translation) \
    (index) \
    (rotate) \
    (offset) \
    (normals) \
    (tangents) \
    (varname) \
    (texcoord) \
    (uaddressmode) \
    (vaddressmode) \
    ((defaultValue, "default")) \
    (outx) \
    (outy) \
    (outz) \
    (outw) \
    (UsdUVTexture) \
    (UsdPrimvarReader_float2) \
    (UsdTransform2d) \
    (texCoordReader) \
    (stPrimvarName) \
    (surface) \
    (UsdPreviewSurface) \
    (anisotropyLevelTexture) \
    (anisotropyAngleTexture) \
    (ASM) \
    ((adobeStandardMaterial, "AdobeStandardMaterial_4_0")) \
    (clearcoatModelsTransmissionTint) \
    (unlit) \
    (transmission) \
    (min) \
    (max) \
    (originalColorSpace) \
    (AmbientOcclusionAsColor) \
    (AmbientOcclusionBaseColor)
// clang-format on

/// Tokens for MaterialX nodes
// clang-format off
#define MATERIAL_X_TOKENS \
    (mtlx) \
    (OpenPBR) \
    (srgb_texture) \
    (ND_image_vector4) \
    (ND_image_color3) \
    (ND_image_vector3) \
    (ND_image_float) \
    (ND_texcoord_vector2) \
    (ND_rotate2d_vector2) \
    (ND_multiply_vector3) \
    (ND_multiply_color3) \
    (ND_multiply_vector2) \
    (ND_multiply_float) \
    (ND_mix_color3) \
    (ND_add_vector3) \
    (ND_add_color3) \
    (ND_add_vector2) \
    (ND_add_float) \
    (ND_subtract_float) \
    (ND_place2d_vector2) \
    (ND_separate4_vector4) \
    (ND_convert_float_color3) \
    (ND_convert_color3_vector3) \
    (ND_normalmap) \
    (ND_UsdUVTexture_23) \
    (ND_displacement_float) \
    (ND_geompropvalue_vector2) \
    (geomprop) \
    (periodic) \
    (clamp) \
    (ND_open_pbr_surface_surfaceshader)
// clang-format on

/// Tokens for the inputs of the UsdPreviewSurface shader
/// The order of tokens listed below is based on the order defined in
/// https://github.com/PixarAnimationStudios/OpenUSD/blob/b9282cb274d111878707baff97d4223a81ef23d8/pxr/usd/plugin/usdShaders/shaders/shaderDefs.usda
// clang-format off
#define USD_PREVIEW_SURFACE_TOKENS \
    (diffuseColor) \
    (emissiveColor) \
    (useSpecularWorkflow) \
    (specularColor) \
    (metallic) \
    (roughness) \
    (clearcoat) \
    (clearcoatRoughness) \
    (opacity) \
    (opacityMode) \
    (opacityThreshold) \
    (ior) \
    (normal) \
    (displacement) \
    (occlusion)
// clang-format on

/// Tokens for the inputs of the AdobeStandardMaterial 4.0 shader
/// The order of tokens listed below is based on the order defined in the ASM spec found at
/// https://helpx.adobe.com/substance-3d-general/adobe-standard-material.html
// clang-format off
#define ASM_TOKENS \
    (baseColor) \
    (roughness) \
    (metallic) \
    (opacity) \
    (specularLevel) \
    (specularEdgeColor) \
    (normal) \
    (normalScale) \
    (combineNormalAndHeight) \
    (height) \
    (heightScale) \
    (heightLevel) \
    (anisotropyLevel) \
    (anisotropyAngle) \
    (emissiveIntensity) \
    (emissive) \
    (sheenOpacity) \
    (sheenColor) \
    (sheenRoughness) \
    (translucency) \
    (IOR) \
    (dispersion) \
    (absorptionColor) \
    (absorptionDistance) \
    (scatter) \
    (scatteringColor) \
    (scatteringDistance) \
    (scatteringDistanceScale) \
    (scatteringRedShift) \
    (scatteringRayleigh) \
    (coatOpacity) \
    (coatColor) \
    (coatRoughness) \
    (coatIOR) \
    (coatSpecularLevel) \
    (coatNormal) \
    (coatNormalScale) \
    (ambientOcclusion) \
    (volumeThickness) \
    (volumeThicknessScale)
// clang-format on

/// Tokens for the inputs of the OpenPBR surface shader
/// The order of tokens listed below is based on the order defined in
/// https://github.com/AcademySoftwareFoundation/OpenPBR/blob/main/reference/open_pbr_surface.mtlx
// clang-format off
#define OPEN_PBR_TOKENS \
    (base_weight) \
    (base_color) \
    (base_diffuse_roughness) \
    (base_metalness) \
    (specular_weight) \
    (specular_color) \
    (specular_roughness) \
    (specular_ior) \
    (specular_roughness_anisotropy) \
    (transmission_weight) \
    (transmission_color) \
    (transmission_depth) \
    (transmission_scatter) \
    (transmission_scatter_anisotropy) \
    (transmission_dispersion_scale) \
    (transmission_dispersion_abbe_number) \
    (subsurface_weight) \
    (subsurface_color) \
    (subsurface_radius) \
    (subsurface_radius_scale) \
    (subsurface_scatter_anisotropy) \
    (fuzz_weight) \
    (fuzz_color) \
    (fuzz_roughness) \
    (coat_weight) \
    (coat_color) \
    (coat_roughness) \
    (coat_roughness_anisotropy) \
    (coat_ior) \
    (coat_darkening) \
    (thin_film_weight) \
    (thin_film_thickness) \
    (thin_film_ior) \
    (emission_luminance) \
    (emission_color) \
    (geometry_opacity) \
    (geometry_thin_walled) \
    (geometry_normal) \
    (geometry_coat_normal) \
    (geometry_tangent) \
    (geometry_coat_tangent)
// clang-format on

/// Tokens for the naming of OpenPBR inputs on the material that don't have ASM equivalents
// clang-format off
#define OPEN_PBR_MATERIAL_INPUT_TOKENS \
    (baseDiffuseRoughness) \
    (baseWeight) \
    (coatDarkening) \
    (coatRoughnessAnisotropy) \
    (coatNormal) \
    (coatTangent) \
    (emissionLuminance) \
    (fuzzWeight) \
    (fuzzColor) \
    (fuzzRoughness) \
    (specularRoughness) \
    (specularWeight) \
    (specularRoughnessAnisotropy) \
    (subsurfaceColor) \
    (subsurfaceRadius) \
    (subsurfaceRadiusScale) \
    (subsurfaceScatterAnisotropy) \
    (subsurfaceWeight) \
    (tangent) \
    (thinFilmIOR) \
    (thinFilmThickness) \
    (thinFilmWeight) \
    (thinWalled) \
    (transmissionWeight) \
    (transmissionColor) \
    (transmissionDepth) \
    (transmissionDispersionAbbeNumber) \
    (transmissionDispersionScale) \
    (transmissionScatter) \
    (transmissionScatterAnisotropy)
// clang-format on

/// Tokens for the inputs of the neural graphics primitives (NGPs)
// clang-format off
#define ADOBE_NGP_TOKENS \
    (Ngp) \
    ((fieldNgp, "field:ngp")) \
    (densityMlpLayer0Weight) \
    (densityMlpLayer0Bias) \
    (densityMlpLayer1Weight) \
    (densityMlpLayer1Bias) \
    (colorMlpLayer0Weight) \
    (colorMlpLayer0Bias) \
    (colorMlpLayer1Weight) \
    (colorMlpLayer1Bias) \
    (colorMlpLayer2Weight) \
    (colorMlpLayer2Bias) \
    (densityGrid) \
    (densityThreshold) \
    (distanceGrid) \
    (hashGrid) \
    (hashGridResolution)
// clang-format on

/// Tokens for the inputs of Gaussian splats
/// These tokens are copied from the .PLY version of Gaussian splat,
/// which are defined in the original Gsplat codebase. Refer to:
/// https://github.com/graphdeco-inria/gaussian-splatting/blob/main/scene/gaussian_model.py
/// for more details.
///
/// rot: Rotation of the splat, in the form of a quaternion.
/// widths*: Additional scales of the splat in Y- and Z- axis, in the object space
/// fRest*: 1st and above (up to 3rd) orders of spherical harmonics coefficients.
///         There are 15 coefficients each of which is a 3D vector, and thus we
///         have 45 floats.
// clang-format off
#define ADOBE_GSPLAT_BASE_TOKENS \
    (rot) \
    (widths1) \
    (widths2)

// clang-format on

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_PUBLIC_TOKENS(AdobeTokens, USDFFUTILS_API, ADOBE_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(MtlXTokens, USDFFUTILS_API, MATERIAL_X_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(UsdPreviewSurfaceTokens, USDFFUTILS_API, USD_PREVIEW_SURFACE_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(AsmTokens, USDFFUTILS_API, ASM_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(OpenPbrTokens, USDFFUTILS_API, OPEN_PBR_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(OpenPbrMaterialInputTokens,
                         USDFFUTILS_API,
                         OPEN_PBR_MATERIAL_INPUT_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(AdobeNgpTokens, USDFFUTILS_API, ADOBE_NGP_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(AdobeGsplatBaseTokens, USDFFUTILS_API, ADOBE_GSPLAT_BASE_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

#define VOID_GUARD(x, ...)                 \
    {                                      \
        if ((x) == false) {                \
            TF_RUNTIME_ERROR(__VA_ARGS__); \
            return;                        \
        }                                  \
    }
#define GUARD(x, ...)                      \
    {                                      \
        if ((x) == false) {                \
            TF_RUNTIME_ERROR(__VA_ARGS__); \
            return false;                  \
        }                                  \
    }

namespace adobe::usd {

USDFFUTILS_API extern const double pi;
USDFFUTILS_API extern const double deg2rad;
USDFFUTILS_API extern const double rad2deg;

void USDFFUTILS_API
argComposeString(const PXR_NS::PcpDynamicFileFormatContext& context,
                 PXR_NS::SdfFileFormat::FileFormatArguments* args,
                 const PXR_NS::TfToken& token,
                 const std::string& debugTag);

void USDFFUTILS_API
argComposeBool(const PXR_NS::PcpDynamicFileFormatContext& context,
               PXR_NS::SdfFileFormat::FileFormatArguments* args,
               const PXR_NS::TfToken& token,
               const std::string& debugTag);

void USDFFUTILS_API
argComposeFloat(const PXR_NS::PcpDynamicFileFormatContext& context,
                PXR_NS::SdfFileFormat::FileFormatArguments* args,
                const PXR_NS::TfToken& token,
                const std::string& debugTag);

void USDFFUTILS_API
argComposeFloatArray(const PXR_NS::PcpDynamicFileFormatContext& context,
                     PXR_NS::SdfFileFormat::FileFormatArguments* args,
                     const PXR_NS::TfToken& token,
                     const std::string& debugTag);

bool USDFFUTILS_API
argReadString(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
              const std::string& arg,
              std::string& target,
              const std::string& debugTag);

bool USDFFUTILS_API
argReadString(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
              const std::string& arg,
              PXR_NS::TfToken& target,
              const std::string& debugTag);

bool USDFFUTILS_API
argReadBool(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
            const std::string& arg,
            bool& target,
            const std::string& debugTag);

bool USDFFUTILS_API
argReadFloat(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
             const std::string& arg,
             float& target,
             const std::string& debugTag);

bool USDFFUTILS_API
argReadFloatArray(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
                  const std::string& arg,
                  PXR_NS::VtFloatArray& target,
                  const std::string& debugTag);

/// Issues a warning if the specified arg is present that it has been deprecated
void USDFFUTILS_API
argWarnDeprecatedArg(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
                     const std::string& arg,
                     const std::string& debugTag);

std::string USDFFUTILS_API
getFileExtension(const std::string& filePath, const std::string& defaultValue);

std::string USDFFUTILS_API
getCurrentDate();

inline void USDFFUTILS_API
ltrim(std::string& s)
{
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

inline void USDFFUTILS_API
rtrim(std::string& s)
{
    s.erase(
      std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
      s.end());
}

inline void USDFFUTILS_API
trim(std::string& s)
{
    rtrim(s);
    ltrim(s);
}

std::vector<std::string> USDFFUTILS_API
split(const std::string& str, char delimiter);

bool USDFFUTILS_API
createDirectory(const std::filesystem::path& directoryPath);

/**
 * Writes out a block of data at a given path
 *
 * @param assetsPath The filepath to write the data
 * @param data The file data. This buffer must be at least size bytes
 * @param size The size of the raw data buffer
 *
 * @return Whether the image was successfully written
 */
bool USDFFUTILS_API
writeDataToDisk(const std::filesystem::path& filepath, const void* data, size_t size);

std::string USDFFUTILS_API
getLayerFilePath(const std::string& layerIdentifier);

std::filesystem::path USDFFUTILS_API
convertStringToPath(const std::string& str);

#if __cplusplus >= 202002L
std::filesystem::path USDFFUTILS_API
convertStringToPath(const std::u8string& str);
#endif

std::string USDFFUTILS_API
convertPathToString(const std::filesystem::path& path);

/// converts u8 literal to a std::string
#if __cplusplus >= 202002L
// C++20: u8"..." → const char8_t*, need reinterpret_cast
inline std::string USDFFUTILS_API
u8_literal(const char8_t* s)
{
    return std::string(reinterpret_cast<const char*>(s));
}
#else
// C++17: u8"..." → const char*, no cast needed
inline std::string USDFFUTILS_API
u8_literal(const char* s)
{
    return std::string(s);
}
#endif

}
