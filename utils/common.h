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
#include <pxr/usd/pcp/dynamicFileFormatContext.h>
#include <pxr/usd/sdf/fileFormat.h>

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
    (mirror) \
    (black) \
    (useMetadata) \
    (sourceColorSpace) \
    (result) \
    (raw) \
    (sRGB) \
    (st) \
    (in) \
    (file) \
    (scale) \
    (bias) \
    (fallback) \
    (rotation) \
    (translation) \
    (normals) \
    (tangents) \
    (varname) \
    (UsdUVTexture) \
    (UsdPrimvarReader_float2) \
    (UsdTransform2d) \
    (texCoordReader) \
    (stPrimvarName) \
    (surface) \
    (UsdPreviewSurface) \
    (useSpecularWorkflow) \
    (diffuseColor) \
    (emissiveColor) \
    (specularColor) \
    (normal) \
    (metallic) \
    (roughness) \
    (clearcoat) \
    (clearcoatRoughness) \
    (sheenOpacity) \
    (sheenColor) \
    (sheenRoughness) \
    (anisotropyLevel) \
    (anisotropyAngle) \
    (opacity) \
    (opacityThreshold) \
    (displacement) \
    (occlusion) \
    (ior) \
    (ASM) \
    ((adobeStandardMaterial, "AdobeStandardMaterial_4_0")) \
    (baseColor) \
    (specularEdgeColor) \
    (specularLevel) \
    (height) \
    (emissiveIntensity) \
    (emissive) \
    (translucency) \
    (IOR) \
    (absorptionColor) \
    (absorptionDistance) \
    (scatter) \
    (scatteringColor) \
    (scatteringDistance) \
    (coatOpacity) \
    (coatColor) \
    (coatRoughness) \
    (coatIOR) \
    (coatSpecularLevel) \
    (coatNormal) \
    (ambientOcclusion) \
    (volumeThickness) \
    (clearcoatModelsTransmissionTint) \
    (writeMaterialX) \
// clang-format on

/// Tokens for MaterialX nodes
// clang-format off
#define MATERIAL_X_TOKENS \
    (MaterialX) \
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
    (ND_add_vector3) \
    (ND_add_color3) \
    (ND_add_vector2) \
    (ND_add_float) \
    (ND_place2d_vector2) \
    (ND_separate4_vector4) \
    (ND_convert_float_color3) \
    (ND_normalmap) \
    (ND_adobe_standard_material) \
    (ND_open_pbr_surface_surfaceshader)
// clang-format on

/// Tokens for the inputs of the OpenPBR surface shader
// clang-format off
#define OPEN_PBR_TOKENS \
    (base_weight) \
    (base_color) \
    (base_roughness) \
    (base_metalness) \
    (specular_weight) \
    (specular_color) \
    (specular_roughness) \
    (specular_ior) \
    (specular_ior_level) \
    (specular_anisotropy) \
    (specular_rotation) \
    (transmission_weight) \
    (transmission_color) \
    (transmission_depth) \
    (transmission_scatter) \
    (transmission_scatter_anisotropy) \
    (transmission_dispersion) \
    (subsurface_weight) \
    (subsurface_color) \
    (subsurface_radius) \
    (subsurface_radius_scale) \
    (subsurface_anisotropy) \
    (fuzz_weight) \
    (fuzz_color) \
    (fuzz_roughness) \
    (coat_weight) \
    (coat_color) \
    (coat_roughness) \
    (coat_anisotropy) \
    (coat_rotation) \
    (coat_ior) \
    (coat_ior_level) \
    (thin_film_thickness) \
    (thin_film_ior) \
    (emission_luminance) \
    (emission_color) \
    (geometry_opacity) \
    (geometry_thin_walled) \
    (geometry_normal) \
    (geometry_coat_normal) \
    (geometry_tangent)
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

PXR_NAMESPACE_OPEN_SCOPE
TF_DECLARE_PUBLIC_TOKENS(AdobeTokens, USDFFUTILS_API, ADOBE_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(MtlXTokens, USDFFUTILS_API, MATERIAL_X_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(OpenPbrTokens, USDFFUTILS_API, OPEN_PBR_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(AdobeNgpTokens, USDFFUTILS_API, ADOBE_NGP_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

#define VOID_GUARD(x, ...)                                                                         \
    {                                                                                              \
        if ((x) == false) {                                                                        \
            TF_RUNTIME_ERROR(__VA_ARGS__);                                                         \
            return;                                                                                \
        }                                                                                          \
    }
#define GUARD(x, ...)                                                                              \
    {                                                                                              \
        if ((x) == false) {                                                                        \
            TF_RUNTIME_ERROR(__VA_ARGS__);                                                         \
            return false;                                                                          \
        }                                                                                          \
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
argReadString(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
              const std::string& arg,
              std::string& target,
              const std::string& debugTag);

void USDFFUTILS_API
argReadBool(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
            const std::string& arg,
            bool& target,
            const std::string& debugTag);

void USDFFUTILS_API
argReadFloat(const PXR_NS::SdfFileFormat::FileFormatArguments& args,
             const std::string& arg,
             float& target,
             const std::string& debugTag);

}
