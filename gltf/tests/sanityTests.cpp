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
#include <common_gtest_args.h>
#include <fileformatutils/featureFlags.h>
#include <fileformatutils/test.h>
#include <gtest/gtest.h>
#include <pxr/base/gf/range3f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/shader.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

PXR_NAMESPACE_USING_DIRECTIVE

// Returns the OpenPBR surface shader prim for a material at the given path, or an invalid prim
// if the material does not exist or was not written as OpenPBR (e.g. legacy mode).
// The OpenPBR writer nests shaders under a NodeGraph named "OpenPBR" inside the material,
// and the surface shader itself is also named "OpenPBR": <materialPath>/OpenPBR/OpenPBR
static UsdPrim
getOpenPbrSurfacePrim(UsdStageRefPtr stage, const std::string& materialPath)
{
    return stage->GetPrimAtPath(SdfPath(materialPath + "/OpenPBR/OpenPBR"));
}

// Reads the resolved value of an OpenPBR surface shader input.
// Uses UsdShadeShader / UsdShadeInput so that:
//   - connections are correctly prioritised over local values (UsdShade semantics), and
//   - multi-hop connection chains are fully traversed via GetValueProducingAttribute().
template<typename T>
static bool
getShaderInput(const UsdPrim& shaderPrim, const char* name, T* out)
{
    UsdShadeInput input = UsdShadeShader(shaderPrim).GetInput(TfToken(name));
    if (!input)
        return false;
    UsdShadeAttributeVector valueAttrs =
      input.GetValueProducingAttributes(/*shaderOutputsOnly=*/false);
    for (const UsdAttribute& attr : valueAttrs) {
        if (attr.Get(out))
            return true;
    }
    return false;
}

TEST(GlTFSanityTests, LoadCube)
{
    // Load a GLTF
    UsdStageRefPtr stage = openAssetStage(assetDir + "SanityCube.gltf");
    ASSERT_TRUE(stage);
    UsdPrim mesh = stage->GetPrimAtPath(SdfPath("/SanityCube/Cube"));
    ASSERT_TRUE(mesh);
}

// glTF import must author an extent on each mesh so downstream UsdGeomBBoxCache queries are
// O(1) rather than re-deriving bounds from every vertex. The authored extent must equal the
// bounds of the mesh points.
TEST(GlTFSanityTests, ImportAuthorsMeshExtent)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "SanityCube.gltf");
    ASSERT_TRUE(stage);

    // The mesh is nested under the node hierarchy, not at a fixed path, so find it by type.
    UsdGeomMesh mesh;
    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.IsA<UsdGeomMesh>()) {
            mesh = UsdGeomMesh(prim);
            break;
        }
    }
    ASSERT_TRUE(mesh) << "no UsdGeomMesh found in imported glTF";

    UsdAttribute extentAttr = mesh.GetExtentAttr();
    ASSERT_TRUE(extentAttr.IsAuthored()) << "glTF-imported mesh has no authored extent";

    VtVec3fArray extent;
    ASSERT_TRUE(extentAttr.Get(&extent));
    ASSERT_EQ(extent.size(), 2u);

    VtVec3fArray points;
    ASSERT_TRUE(mesh.GetPointsAttr().Get(&points));
    ASSERT_FALSE(points.empty());

    GfRange3f expected;
    for (const GfVec3f& pt : points) {
        expected.UnionWith(pt);
    }
    EXPECT_EQ(extent[0], expected.GetMin());
    EXPECT_EQ(extent[1], expected.GetMax());
}

// Thin-walled transmission: KHR_materials_transmission with no volume extension.
// The material is thin-walled by default, so base_color should be used as
// transmission_color directly without needing a coat layer.
TEST(GlTFSanityTests, ImportTransmissionThinWalled)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "TransmissionThinWalled.gltf");
    ASSERT_TRUE(stage);
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/TransmissionThinWalled")))
      << "Root prim missing — scene did not import";
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/TransmissionThinWalled/Cube")))
      << "Cube mesh prim missing";
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/TransmissionThinWalled/Materials")))
      << "Materials scope missing";

    // Material attribute checks require OpenPBR mode.
    UsdPrim shader = getOpenPbrSurfacePrim(
      stage, "/TransmissionThinWalled/Materials/TransmissionThinWalledMaterial");
    if (!shader) {
        GTEST_SKIP() << "OpenPBR surface shader not present — skipping attribute checks "
                        "(non-OpenPBR mode?)";
    }

    // transmission_weight should equal the glTF transmissionFactor (0.9).
    float transmissionWeight = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "transmission_weight", &transmissionWeight))
      << "transmission_weight input missing or unreadable";
    EXPECT_NEAR(transmissionWeight, 0.9f, 1e-5f);

    // transmission_color should equal base_color (0.8, 0.3, 0.1) — the thin-walled direct path.
    GfVec3f transmissionColor;
    ASSERT_TRUE(getShaderInput(shader, "transmission_color", &transmissionColor))
      << "transmission_color input missing or unreadable";
    EXPECT_EQ(transmissionColor, GfVec3f(0.8f, 0.3f, 0.1f))
      << "transmission_color should equal base_color for thin-walled transmission";

    // No coat layer should have been created — thin-walled takes the direct path.
    float coatWeight = 0.0f;
    if (getShaderInput(shader, "coat_weight", &coatWeight)) {
        EXPECT_EQ(coatWeight, 0.0f) << "coat_weight should be 0 for thin-walled transmission";
    }
}

// Volumetric transmission: KHR_materials_transmission + KHR_materials_volume with
// attenuationDistance > 0 and a non-white base color.  In OpenPBR mode this exercises
// the copyBaseSurfaceToCoat path — base_color is moved to coat_color so that surface
// tinting is preserved for the volumetric material.
TEST(GlTFSanityTests, ImportTransmissionVolumetric)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "TransmissionVolumetric.gltf");
    ASSERT_TRUE(stage);
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/TransmissionVolumetric")))
      << "Root prim missing — scene did not import";
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/TransmissionVolumetric/Cube")))
      << "Cube mesh prim missing";
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/TransmissionVolumetric/Materials")))
      << "Materials scope missing";

    // Material attribute checks require OpenPBR mode.
    UsdPrim shader = getOpenPbrSurfacePrim(
      stage, "/TransmissionVolumetric/Materials/TransmissionVolumetricMaterial");
    if (!shader) {
        GTEST_SKIP() << "OpenPBR surface shader not present — skipping attribute checks "
                        "(non-OpenPBR mode?)";
    }

    // copyBaseSurfaceToCoat copies transmission_weight → coat_weight (0.9).
    float coatWeight = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "coat_weight", &coatWeight))
      << "coat_weight input missing or unreadable — copyBaseSurfaceToCoat may not have run";
    EXPECT_NEAR(coatWeight, 0.9f, 1e-5f)
      << "coat_weight should equal transmissionFactor for volumetric transmission";

    // copyBaseSurfaceToCoat copies base_color → coat_color (0.8, 0.3, 0.1).
    GfVec3f coatColor;
    ASSERT_TRUE(getShaderInput(shader, "coat_color", &coatColor))
      << "coat_color input missing or unreadable";
    EXPECT_EQ(coatColor, GfVec3f(0.8f, 0.3f, 0.1f))
      << "coat_color should equal base_color for volumetric transmission";
}

// KHR_materials_coat: maps directly to OpenPBR coat_weight, coat_roughness, and coat_ior.
TEST(GlTFSanityTests, ImportExtCoat)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtCoat.gltf");
    ASSERT_TRUE(stage);
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/ExtCoat/Materials"))) << "Materials scope missing";

    UsdPrim shader = getOpenPbrSurfacePrim(stage, "/ExtCoat/Materials/CoatMaterial");
    if (!shader) {
        GTEST_SKIP() << "OpenPBR surface shader not present (non-OpenPBR mode?)";
    }

    float coatWeight = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "coat_weight", &coatWeight))
      << "coat_weight missing or unreadable";
    EXPECT_NEAR(coatWeight, 0.7f, 1e-5f);

    float coatRoughness = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "coat_roughness", &coatRoughness))
      << "coat_roughness missing or unreadable";
    EXPECT_NEAR(coatRoughness, 0.3f, 1e-5f);

    float coatIor = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "coat_ior", &coatIor)) << "coat_ior missing or unreadable";
    EXPECT_NEAR(coatIor, 1.8f, 1e-5f);
}

// KHR_materials_diffuse_roughness: maps diffuseRoughnessFactor to base_diffuse_roughness.
TEST(GlTFSanityTests, ImportExtDiffuseRoughness)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtDiffuseRoughness.gltf");
    ASSERT_TRUE(stage);
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/ExtDiffuseRoughness/Materials")))
      << "Materials scope missing";

    UsdPrim shader =
      getOpenPbrSurfacePrim(stage, "/ExtDiffuseRoughness/Materials/DiffuseRoughnessMaterial");
    if (!shader) {
        GTEST_SKIP() << "OpenPBR surface shader not present (non-OpenPBR mode?)";
    }

    float diffuseRoughness = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "base_diffuse_roughness", &diffuseRoughness))
      << "base_diffuse_roughness missing or unreadable";
    EXPECT_NEAR(diffuseRoughness, 0.4f, 1e-5f);
}

// KHR_materials_dispersion: maps dispersion to transmission_dispersion_scale and also sets
// transmission_dispersion_abbe_number to the hardcoded value 20.0.
TEST(GlTFSanityTests, ImportExtDispersion)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtDispersion.gltf");
    ASSERT_TRUE(stage);
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/ExtDispersion/Materials")))
      << "Materials scope missing";

    UsdPrim shader = getOpenPbrSurfacePrim(stage, "/ExtDispersion/Materials/DispersionMaterial");
    if (!shader) {
        GTEST_SKIP() << "OpenPBR surface shader not present (non-OpenPBR mode?)";
    }

    float dispersionScale = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "transmission_dispersion_scale", &dispersionScale))
      << "transmission_dispersion_scale missing or unreadable";
    EXPECT_NEAR(dispersionScale, 0.05f, 1e-5f);

    float abbeNumber = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "transmission_dispersion_abbe_number", &abbeNumber))
      << "transmission_dispersion_abbe_number missing or unreadable";
    EXPECT_NEAR(abbeNumber, 20.0f, 1e-5f);
}

// KHR_materials_fuzz: maps fuzzFactor, fuzzColorFactor, and fuzzRoughnessFactor to the
// OpenPBR fuzz lobe (fuzz_weight, fuzz_color, fuzz_roughness).
TEST(GlTFSanityTests, ImportExtFuzz)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtFuzz.gltf");
    ASSERT_TRUE(stage);
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/ExtFuzz/Materials"))) << "Materials scope missing";

    UsdPrim shader = getOpenPbrSurfacePrim(stage, "/ExtFuzz/Materials/FuzzMaterial");
    if (!shader) {
        GTEST_SKIP() << "OpenPBR surface shader not present (non-OpenPBR mode?)";
    }

    float fuzzWeight = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "fuzz_weight", &fuzzWeight))
      << "fuzz_weight missing or unreadable";
    EXPECT_NEAR(fuzzWeight, 0.8f, 1e-5f);

    GfVec3f fuzzColor;
    ASSERT_TRUE(getShaderInput(shader, "fuzz_color", &fuzzColor))
      << "fuzz_color missing or unreadable";
    EXPECT_EQ(fuzzColor, GfVec3f(0.9f, 0.5f, 0.2f));

    float fuzzRoughness = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "fuzz_roughness", &fuzzRoughness))
      << "fuzz_roughness missing or unreadable";
    EXPECT_NEAR(fuzzRoughness, 0.6f, 1e-5f);
}

// KHR_materials_iridescence: maps to the OpenPBR thin-film lobe.
// iridescenceThicknessMaximum is converted from nanometers to micrometers (×0.001).
TEST(GlTFSanityTests, ImportExtIridescence)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtIridescence.gltf");
    ASSERT_TRUE(stage);
    ASSERT_TRUE(stage->GetPrimAtPath(SdfPath("/ExtIridescence/Materials")))
      << "Materials scope missing";

    UsdPrim shader = getOpenPbrSurfacePrim(stage, "/ExtIridescence/Materials/IridescenceMaterial");
    if (!shader) {
        GTEST_SKIP() << "OpenPBR surface shader not present (non-OpenPBR mode?)";
    }

    float thinFilmWeight = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "thin_film_weight", &thinFilmWeight))
      << "thin_film_weight missing or unreadable";
    EXPECT_NEAR(thinFilmWeight, 0.75f, 1e-5f);

    float thinFilmIor = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "thin_film_ior", &thinFilmIor))
      << "thin_film_ior missing or unreadable";
    EXPECT_NEAR(thinFilmIor, 1.5f, 1e-5f);

    // 500 nm × 0.001 = 0.5 μm
    float thinFilmThickness = 0.0f;
    ASSERT_TRUE(getShaderInput(shader, "thin_film_thickness", &thinFilmThickness))
      << "thin_film_thickness missing or unreadable";
    EXPECT_NEAR(thinFilmThickness, 0.5f, 1e-5f)
      << "thin_film_thickness should be iridescenceThicknessMaximum (500 nm) × 0.001 = 0.5 μm";
}

// Helper: read an exported .gltf file back as a string.
static std::string
readExportedGltf(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Helper: parse an exported .gltf file as JSON.  Returns a null JSON value on failure.
static nlohmann::json
parseExportedGltf(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return nullptr;
    try {
        return nlohmann::json::parse(f);
    } catch (const nlohmann::json::parse_error&) {
        return nullptr;
    }
}

// KHR_materials_coat export — simple coat round-trip.
// ExtCoatSimple.gltf uses coatIor=1.5 and coatDarkeningFactor=0.0, which are both equal to
// the KHR_materials_clearcoat defaults.  On round-trip the exporter should therefore emit only
// KHR_materials_clearcoat (the simpler, more widely-supported extension) and NOT
// KHR_materials_coat.
TEST(GlTFSanityTests, ExportCoatSimple)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtCoatSimple.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtCoatSimple.gltf";

    std::string outPath = assetDir + "ExportCoatSimple_out.gltf";
    stage->Export(outPath);

    std::string json = readExportedGltf(outPath);
    ASSERT_FALSE(json.empty()) << "Exported GLTF file is empty or could not be read: " << outPath;

    // Simple coat should be represented by KHR_materials_clearcoat.
    EXPECT_NE(json.find("\"KHR_materials_clearcoat\""), std::string::npos)
      << "KHR_materials_clearcoat should be present in exported GLTF for simple coat";

    // KHR_materials_coat must NOT be present — all coat data fits within clearcoat.
    // Note: "KHR_materials_coat" is not a substring of "KHR_materials_clearcoat",
    // so this check is unambiguous.
    EXPECT_EQ(json.find("\"KHR_materials_coat\""), std::string::npos)
      << "KHR_materials_coat should NOT be present when coat data fits within clearcoat";
}

// KHR_materials_coat export — advanced coat round-trip.
// ExtCoat.gltf uses coatIor=1.8 (≠ clearcoat default 1.5) and omits coatDarkeningFactor
// (defaults to 1.0 ≠ clearcoat equivalent 0.0).  These values cannot be represented by
// KHR_materials_clearcoat, so KHR_materials_coat must be present in the exported output.
TEST(GlTFSanityTests, ExportCoatAdvanced)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtCoat.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtCoat.gltf";

    std::string outPath = assetDir + "ExportCoatAdvanced_out.gltf";
    stage->Export(outPath);

    std::string json = readExportedGltf(outPath);
    ASSERT_FALSE(json.empty()) << "Exported GLTF file is empty or could not be read: " << outPath;

    EXPECT_NE(json.find("\"KHR_materials_coat\""), std::string::npos)
      << "KHR_materials_coat should be present for advanced coat data (coatIor=1.8, "
         "coatDarkeningFactor=1.0)";
}

// KHR_materials_diffuse_roughness export round-trip.
TEST(GlTFSanityTests, ExportExtDiffuseRoughness)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtDiffuseRoughness.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtDiffuseRoughness.gltf";

    std::string outPath = assetDir + "ExportExtDiffuseRoughness_out.gltf";
    stage->Export(outPath);

    std::string json = readExportedGltf(outPath);
    ASSERT_FALSE(json.empty()) << "Exported GLTF file is empty or could not be read: " << outPath;

    EXPECT_NE(json.find("\"KHR_materials_diffuse_roughness\""), std::string::npos)
      << "KHR_materials_diffuse_roughness should be present in exported GLTF";
}

// KHR_materials_dispersion export round-trip.
TEST(GlTFSanityTests, ExportExtDispersion)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtDispersion.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtDispersion.gltf";

    std::string outPath = assetDir + "ExportExtDispersion_out.gltf";
    stage->Export(outPath);

    std::string json = readExportedGltf(outPath);
    ASSERT_FALSE(json.empty()) << "Exported GLTF file is empty or could not be read: " << outPath;

    EXPECT_NE(json.find("\"KHR_materials_dispersion\""), std::string::npos)
      << "KHR_materials_dispersion should be present in exported GLTF";
}

// KHR_materials_fuzz export round-trip.
TEST(GlTFSanityTests, ExportExtFuzz)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtFuzz.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtFuzz.gltf";

    std::string outPath = assetDir + "ExportExtFuzz_out.gltf";
    stage->Export(outPath);

    std::string json = readExportedGltf(outPath);
    ASSERT_FALSE(json.empty()) << "Exported GLTF file is empty or could not be read: " << outPath;

    EXPECT_NE(json.find("\"KHR_materials_fuzz\""), std::string::npos)
      << "KHR_materials_fuzz should be present in exported GLTF";
}

// KHR_materials_iridescence export round-trip.
TEST(GlTFSanityTests, ExportExtIridescence)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtIridescence.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtIridescence.gltf";

    std::string outPath = assetDir + "ExportExtIridescence_out.gltf";
    stage->Export(outPath);

    std::string json = readExportedGltf(outPath);
    ASSERT_FALSE(json.empty()) << "Exported GLTF file is empty or could not be read: " << outPath;

    EXPECT_NE(json.find("\"KHR_materials_iridescence\""), std::string::npos)
      << "KHR_materials_iridescence should be present in exported GLTF";
}

// Helper: look up KHR_materials_volume_scatter from material[0] of a parsed GLTF.
// Returns a null JSON value if the extension is absent.
static nlohmann::json
getVolumeScatterExt(const nlohmann::json& gltf)
{
    try {
        return gltf.at("materials").at(0).at("extensions").at("KHR_materials_volume_scatter");
    } catch (const nlohmann::json::out_of_range&) {
        return nullptr;
    }
}

// KHR_materials_volume_scatter export round-trip — specular (KHR_materials_transmission) variant.
//
// The source GLTF combines KHR_materials_transmission + KHR_materials_volume +
// KHR_materials_volume_scatter with both a multiscatterColorFactor [0.5, 0.7, 0.9] and a
// multiscatterColorTexture.
//
// On import, translateMultiscatterToSingleScatter multiplies each texel by the factor before
// applying the (nonlinear) conversion, then resets the scale to white.  This means the factor
// is fully consumed into the converted texture; transmission_scatter carries no residual scale.
//
// On re-export, translateSingleScatterToMultiscatter converts the texture back and
// addTextureToExt emits the (default-white) scale as multiscatterColorFactor = [1, 1, 1].
// The texture itself should also survive.
TEST(GlTFSanityTests, ExportExtVolumeScatterTransmission)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtVolumeScatterTransmission.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtVolumeScatterTransmission.gltf";

    std::string outPath = assetDir + "ExportExtVolumeScatterTransmission_out.gltf";
    stage->Export(outPath);

    nlohmann::json gltf = parseExportedGltf(outPath);
    ASSERT_FALSE(gltf.is_null()) << "Could not parse exported GLTF: " << outPath;

    // The round-trip should produce exactly one image: the re-converted multiscatter texture.
    // A second image would indicate the intermediate single-scatter image leaked into the output.
    ASSERT_TRUE(gltf.contains("images")) << "No images array in exported GLTF";
    EXPECT_EQ(gltf["images"].size(), 1u)
      << "Expected exactly 1 image; a second image suggests the intermediate singlescatter "
         "texture was incorrectly exported";

    nlohmann::json ext = getVolumeScatterExt(gltf);
    ASSERT_FALSE(ext.is_null()) << "KHR_materials_volume_scatter not found in exported material";

    EXPECT_TRUE(ext.contains("multiscatterColorTexture"))
      << "multiscatterColorTexture missing — texture did not survive the round-trip";

    // The source factor [0.5, 0.7, 0.9] was baked into the texture pixels during import.
    // The re-exported factor should therefore be white [1, 1, 1] (no residual tint).
    ASSERT_TRUE(ext.contains("multiscatterColorFactor"))
      << "multiscatterColorFactor missing — expected white [1,1,1] to be emitted alongside the "
         "re-exported texture";
    const auto& factor = ext["multiscatterColorFactor"];
    ASSERT_EQ(factor.size(), 3u) << "multiscatterColorFactor should have 3 components";
    EXPECT_NEAR(factor[0].get<double>(), 1.0, 1e-4);
    EXPECT_NEAR(factor[1].get<double>(), 1.0, 1e-4);
    EXPECT_NEAR(factor[2].get<double>(), 1.0, 1e-4);
}

// KHR_materials_volume_scatter export round-trip — diffuse (KHR_materials_diffuse_transmission)
// variant.
//
// The source GLTF combines KHR_materials_diffuse_transmission + KHR_materials_volume +
// KHR_materials_volume_scatter with both a multiscatterColorFactor [0.5, 0.7, 0.9] and a
// multiscatterColorTexture.  On import the texture is stored directly on subsurface_color
// (no formula conversion needed) and the factor is stored as the texture scale.  On re-export
// both should be written back out with the factor value intact.
TEST(GlTFSanityTests, ExportExtVolumeScatterDiffuseTransmission)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "ExtVolumeScatterDiffuseTransmission.gltf");
    ASSERT_TRUE(stage) << "Failed to open ExtVolumeScatterDiffuseTransmission.gltf";

    std::string outPath = assetDir + "ExportExtVolumeScatterDiffuseTransmission_out.gltf";
    stage->Export(outPath);

    nlohmann::json gltf = parseExportedGltf(outPath);
    ASSERT_FALSE(gltf.is_null()) << "Could not parse exported GLTF: " << outPath;

    nlohmann::json ext = getVolumeScatterExt(gltf);
    ASSERT_FALSE(ext.is_null()) << "KHR_materials_volume_scatter not found in exported material";

    EXPECT_TRUE(ext.contains("multiscatterColorTexture"))
      << "multiscatterColorTexture missing — texture did not survive the round-trip";

    ASSERT_TRUE(ext.contains("multiscatterColorFactor"))
      << "multiscatterColorFactor missing — factor was not re-exported alongside the texture";
    const auto& factor = ext["multiscatterColorFactor"];
    ASSERT_EQ(factor.size(), 3u) << "multiscatterColorFactor should have 3 components";
    EXPECT_NEAR(factor[0].get<double>(), 0.5, 1e-4);
    EXPECT_NEAR(factor[1].get<double>(), 0.7, 1e-4);
    EXPECT_NEAR(factor[2].get<double>(), 0.9, 1e-4);
}

// Helper: return the baseColorFactor array of the material with the given name, or a null JSON
// value if no such material (or factor) exists.
static nlohmann::json
getBaseColorFactor(const nlohmann::json& gltf, const std::string& materialName)
{
    if (!gltf.contains("materials"))
        return nullptr;
    for (const auto& material : gltf["materials"]) {
        if (material.value("name", std::string()) == materialName) {
            try {
                return material.at("pbrMetallicRoughness").at("baseColorFactor");
            } catch (const nlohmann::json::out_of_range&) {
                return nullptr;
            }
        }
    }
    return nullptr;
}

// OpenPBR base_weight export — the diffuse albedo is base_color * base_weight, but glTF has no
// separate base weight, so the exporter must fold the weight into baseColorFactor.  A material
// with base_weight = 1.0 must export unchanged.  Only the native OpenPBR export path reads
// base_weight, so this test is meaningless when native processing is disabled.
TEST(GlTFSanityTests, ExportOpenPbrBaseWeight)
{
    if (!adobe::usd::isNativeOpenPbrProcessingEnabled()) {
        GTEST_SKIP() << "base_weight is only read on the native OpenPBR export path";
    }

    UsdStageRefPtr stage = openAssetStage(assetDir + "openpbr_base_weight.usda");
    ASSERT_TRUE(stage) << "Failed to open openpbr_base_weight.usda";

    std::string outPath = assetDir + "ExportOpenPbrBaseWeight_out.gltf";
    stage->Export(outPath);

    nlohmann::json gltf = parseExportedGltf(outPath);
    ASSERT_FALSE(gltf.is_null()) << "Could not parse exported GLTF: " << outPath;

    // base_color (0.041, 0.5, 0.5) * base_weight 0.8 = (0.0328, 0.4, 0.4).
    nlohmann::json weighted = getBaseColorFactor(gltf, "WeightedMaterial");
    ASSERT_FALSE(weighted.is_null()) << "WeightedMaterial baseColorFactor not found";
    ASSERT_EQ(weighted.size(), 4u) << "baseColorFactor should have 4 components";
    EXPECT_NEAR(weighted[0].get<double>(), 0.0328, 1e-4)
      << "base_weight 0.8 was not folded into baseColorFactor";
    EXPECT_NEAR(weighted[1].get<double>(), 0.4, 1e-4);
    EXPECT_NEAR(weighted[2].get<double>(), 0.4, 1e-4);

    // base_weight 1.0 must leave base_color untouched.
    nlohmann::json unit = getBaseColorFactor(gltf, "UnitWeightMaterial");
    ASSERT_FALSE(unit.is_null()) << "UnitWeightMaterial baseColorFactor not found";
    ASSERT_EQ(unit.size(), 4u) << "baseColorFactor should have 4 components";
    EXPECT_NEAR(unit[0].get<double>(), 0.041, 1e-4)
      << "base_weight 1.0 should not change baseColorFactor";
    EXPECT_NEAR(unit[1].get<double>(), 0.5, 1e-4);
    EXPECT_NEAR(unit[2].get<double>(), 0.5, 1e-4);
}
