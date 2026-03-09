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
#include <fileformatutils/test.h>
#include <gtest/gtest.h>

#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerReadMaterial.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/layerWriteShared.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/sdf/data.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>

#include <filesystem>

// Set to true to (re-)generate USDA baselines, false to compare against them
constexpr bool UPDATE_USDA_BASELINES = false;

// Set to true to dump the output next to the baselines if a comparison fails
constexpr bool DUMP_FAILED_USDA_OUTPUT = false;

// Macro to compare against or generate a USDA baseline
#define ASSERT_USDA(usdaLayer, baselinePath) \
    ASSERT_TRUE(assertUsda(usdaLayer, baselinePath, UPDATE_USDA_BASELINES, DUMP_FAILED_USDA_OUTPUT))

// if running locally from VSCode, replace the empty assetDir with the commented out line below
std::string assetDir = "";
// std::string assetDir = std::filesystem::current_path().string() + "/utils/tests/";

PXR_NAMESPACE_USING_DIRECTIVE

using namespace adobe::usd;

// This class is here to expose the protected SdfFileFormat::_SetLayerData function to this test
class TestFileFormat : public SdfFileFormat
{
public:
    static void SetLayerData(SdfLayer* layer, SdfAbstractDataRefPtr& data)
    {
        SdfFileFormat::_SetLayerData(layer, data);
    }
};

Material&
fillGeneralTestMaterial(UsdData& data)
{
    Material& m = data.addMaterial().second;
    m.name = "GeneralTestMaterial";
    m.displayName = "General Test Material";

    // Set every input to a constant value
    m.useSpecularWorkflow = Input{ VtValue(1) };
    m.diffuseColor = Input{ VtValue(GfVec3f(1.0f, 0.5f, 0.25f)) };
    m.emissiveColor = Input{ VtValue(GfVec3f(1.0f, 0.5f, 0.25f)) };
    m.specularLevel = Input{ VtValue(0.5f) };
    m.specularColor = Input{ VtValue(GfVec3f(1.0f, 0.0f, 1.0f)) };
    m.normal = Input{ VtValue(GfVec3f(0.5f, 0.5f, 0.5f)) };
    m.normalScale = Input{ VtValue(0.5f) };
    m.metallic = Input{ VtValue(0.22f) };
    m.roughness = Input{ VtValue(0.66f) };
    m.clearcoat = Input{ VtValue(0.55f) };
    m.clearcoatColor = Input{ VtValue(GfVec3f(1.0f, 1.0f, 0.0f)) };
    m.clearcoatRoughness = Input{ VtValue(0.44f) };
    m.clearcoatIor = Input{ VtValue(1.33f) };
    m.clearcoatSpecular = Input{ VtValue(0.88f) };
    m.clearcoatNormal = Input{ VtValue(GfVec3f(0.66f, 0.0f, 0.66f)) };
    m.sheenColor = Input{ VtValue(GfVec3f(0.0f, 1.0f, 1.0f)) };
    m.sheenRoughness = Input{ VtValue(0.5f) };
    m.anisotropyLevel = Input{ VtValue(0.321f) };
    m.anisotropyAngle = Input{ VtValue(0.777f) };
    m.opacity = Input{ VtValue(0.8f) };
    m.opacityThreshold = Input{ VtValue(0.75f) };
    m.displacement = Input{ VtValue(1.23f) };
    m.occlusion = Input{ VtValue(0.01f) };
    m.ior = Input{ VtValue(1.55f) };
    m.transmission = Input{ VtValue(0.123f) };
    m.volumeThickness = Input{ VtValue(0.987f) };
    m.absorptionDistance = Input{ VtValue(111.0f) };
    m.absorptionColor = Input{ VtValue(GfVec3f(0.25f, 0.5f, 1.0f)) };
    m.scatteringDistance = Input{ VtValue(222.0f) };
    m.scatteringColor = Input{ VtValue(GfVec3f(1.0f, 0.5f, 1.0f)) };
    m.clearcoatModelsTransmissionTint = true;
    m.isUnlit = true;
    return m;
}

void
fillTextureTestMaterial(UsdData& data)
{
    // Add some images to use
    auto [colorId, colorImage] = data.addImage();
    colorImage.name = "color";
    colorImage.uri = "textures/color.png";
    colorImage.format = ImageFormat::ImageFormatPng;
    auto [normalId, normalImage] = data.addImage();
    normalImage.name = "normal";
    normalImage.uri = "textures/normal.png";
    normalImage.format = ImageFormat::ImageFormatPng;
    auto [greyscaleId, greyscaleImage] = data.addImage();
    greyscaleImage.name = "greyscale";
    greyscaleImage.uri = "textures/greyscale.png";
    greyscaleImage.format = ImageFormat::ImageFormatPng;
    auto [occlusionId, occlusionImage] = data.addImage();
    occlusionImage.name = "occlusion";
    occlusionImage.uri = "textures/occlusion.png";
    occlusionImage.format = ImageFormat::ImageFormatPng;

    Material& m = data.addMaterial().second;
    m.name = "TextureTestMaterial";
    m.displayName = "Texture Test Material";

    // Set different inputs to specific texture setups

    // Color textures
    {
        Input input;
        input.image = colorId;
        input.channel = AdobeTokens->rgb;
        input.colorspace = AdobeTokens->sRGB;
        m.diffuseColor = input;

        // Wrap mode, scale & bias, UV transform
        input.wrapS = AdobeTokens->clamp;
        input.wrapT = AdobeTokens->mirror;
        input.scale = GfVec4f(1.0f, 2.0f, 0.5f, 1.0f);
        input.bias = GfVec4f(0.1f, 0.2f, 0.3f, 0.0f);
        input.uvRotation = 15.0f;
        input.uvScale = GfVec2f(1.5f, 0.75f);
        input.uvTranslation = GfVec2f(0.12f, 3.45f);
        m.emissiveColor = input;
    }

    // Normal maps
    {
        Input input;
        input.image = normalId;
        input.channel = AdobeTokens->rgb;
        input.colorspace = AdobeTokens->raw;
        // GLTF files sometimes scale the normals
        const float nmScale = 0.75f;
        input.scale = GfVec4f(2.0f, 2.0f, 2.0f, 1.0f) * nmScale;
        input.bias = GfVec4f(-1.0f, -1.0f, -1.0f, 0.0f) * nmScale;
        m.normal = input;
        m.clearcoatNormal = input;
        // Put DirectX convention decoding values on the clearcoat normals
        m.clearcoatNormal.scale = GfVec4f(2.0f, -2.0f, 2.0f, 1.0f);
        m.clearcoatNormal.bias = GfVec4f(-1.0f, 1.0f, -1.0f, 0.0f);
        // XXX test normal map scale the way GLTF can create
    }

    // Greyscale maps
    {
        Input input;
        input.image = greyscaleId;
        input.channel = AdobeTokens->r;
        input.colorspace = AdobeTokens->raw;

        // Wrap mode, scale & bias, UV transform for the single channel case
        input.wrapS = AdobeTokens->black;
        input.wrapT = AdobeTokens->black;
        input.scale = GfVec4f(0.55f, 1.0f, 1.0f, 1.0f);
        input.bias = GfVec4f(0.1f, 0.0f, 0.0f, 0.0f);
        input.uvRotation = 15.0f;
        input.uvScale = GfVec2f(1.5f, 0.75f);
        input.uvTranslation = GfVec2f(0.12f, 3.45f);
        m.roughness = input;
    }

    // Occlusion maps
    {
        Input input;
        input.image = occlusionId;
        input.channel = AdobeTokens->r;
        input.colorspace = AdobeTokens->raw;

        m.occlusion = input;
    }

    // Single channel from RGB map
    {
        Input input;
        input.image = colorId;
        input.channel = AdobeTokens->g;
        input.colorspace = AdobeTokens->raw;
        m.clearcoat = input;
    }
}

void
fillTransmissionMaterial(UsdData& data)
{
    Material& m = data.addMaterial().second;
    m.name = "TransmissionTestMaterial";
    m.displayName = "Transmission Test Material";

    // Set transmission, but NOT opacity. For UsdPreviewSurface this should be mapped as an inverse
    // to opacity
    m.transmission = Input{ VtValue(0.543f) };
}

void
fillTransmissionMaterialAsUsdPreviewSurfaceBaseline(UsdData& data)
{
    Material& m = data.addMaterial().second;
    m.name = "TransmissionTestMaterial";
    m.displayName = "Transmission Test Material";

    // Set opacity to the inverse of the above transmission, since this is how this should be read
    // from a UsdPreviewSurface representation
    m.opacity = Input{ VtValue(1.0f - 0.543f) };
}

void
compareInputs(const std::string& inputName,
              const Input& input,
              const Input& baseline,
              const UsdData& data,
              const UsdData& baselineData)
{
    EXPECT_EQ(input.value, baseline.value) << inputName;
    // Comparing the `image` member is a bit involved, since it is an index into the owning UsdData
    // Either both image indices are invalid or both are valid
    EXPECT_EQ(input.image == -1, baseline.image == -1) << inputName;
    if (input.image != -1 && baseline.image != -1) {
        ASSERT_TRUE(input.image < data.images.size()) << inputName;
        ASSERT_TRUE(baseline.image < baselineData.images.size()) << inputName;
        const ImageAsset& image = data.images[input.image];
        const ImageAsset& baselineImage = baselineData.images[baseline.image];
        EXPECT_EQ(image.name, baselineImage.name) << inputName;
        EXPECT_EQ(image.uri, baselineImage.uri) << inputName;
        EXPECT_EQ(image.format, baselineImage.format) << inputName;
    }
    EXPECT_EQ(input.uvIndex, baseline.uvIndex) << inputName;
    EXPECT_EQ(input.channel, baseline.channel) << inputName;
    EXPECT_EQ(input.wrapS, baseline.wrapS) << inputName;
    EXPECT_EQ(input.wrapT, baseline.wrapT) << inputName;
    EXPECT_EQ(input.minFilter, baseline.minFilter) << inputName;
    EXPECT_EQ(input.magFilter, baseline.magFilter) << inputName;
    EXPECT_EQ(input.colorspace, baseline.colorspace) << inputName;
    EXPECT_EQ(input.scale, baseline.scale) << inputName;
    EXPECT_EQ(input.bias, baseline.bias) << inputName;
    EXPECT_EQ(input.uvRotation, baseline.uvRotation) << inputName;
    EXPECT_EQ(input.uvScale, baseline.uvScale) << inputName;
    EXPECT_EQ(input.uvTranslation, baseline.uvTranslation) << inputName;
}

void
compareMaterials(const Material& material,
                 const Material& baseline,
                 const UsdData& data,
                 const UsdData& baselineData)
{
    EXPECT_EQ(material.name, baseline.name);
    EXPECT_EQ(material.displayName, baseline.displayName);

    EXPECT_EQ(material.clearcoatModelsTransmissionTint, baseline.clearcoatModelsTransmissionTint);
    EXPECT_EQ(material.isUnlit, baseline.isUnlit);

#define COMP_INPUT(x) compareInputs(#x, material.x, baseline.x, data, baselineData);
    COMP_INPUT(useSpecularWorkflow)
    COMP_INPUT(diffuseColor)
    COMP_INPUT(emissiveColor)
    COMP_INPUT(specularLevel)
    COMP_INPUT(specularColor)
    COMP_INPUT(normal)
    COMP_INPUT(normalScale)
    COMP_INPUT(metallic)
    COMP_INPUT(roughness)
    COMP_INPUT(clearcoat)
    COMP_INPUT(clearcoatColor)
    COMP_INPUT(clearcoatRoughness)
    COMP_INPUT(clearcoatIor)
    COMP_INPUT(clearcoatSpecular)
    COMP_INPUT(clearcoatNormal)
    COMP_INPUT(sheenColor)
    COMP_INPUT(sheenRoughness)
    COMP_INPUT(anisotropyLevel)
    COMP_INPUT(anisotropyAngle)
    COMP_INPUT(opacity)
    COMP_INPUT(opacityThreshold)
    COMP_INPUT(displacement)
    COMP_INPUT(occlusion)
    COMP_INPUT(ior)
    COMP_INPUT(transmission)
    COMP_INPUT(volumeThickness)
    COMP_INPUT(absorptionDistance)
    COMP_INPUT(absorptionColor)
    COMP_INPUT(scatteringDistance)
    COMP_INPUT(scatteringColor)
#undef COMP_INPUT
}

TEST(FileFormatUtilsTests, materialStructConversions)
{
    // Note, only Material -> OpenPbrMaterial -> Material needs to be preserving all information
    // OpenPbrMaterial can carry additional information that would not correctly round trip

    {
        UsdData data;
        fillGeneralTestMaterial(data);
        ASSERT_EQ(data.materials.size(), 1);
        Material& baselineMaterial = data.materials[0];

        OpenPbrMaterial openPbrMaterial =
          mapMaterialStructToOpenPbrMaterialStruct(baselineMaterial);
        Material outputMaterial = mapOpenPbrMaterialStructToMaterialStruct(openPbrMaterial);

        compareMaterials(outputMaterial, baselineMaterial, data, data);
    }

    {
        UsdData data;
        fillTextureTestMaterial(data);
        ASSERT_EQ(data.materials.size(), 1);
        Material& baselineMaterial = data.materials[0];

        OpenPbrMaterial openPbrMaterial =
          mapMaterialStructToOpenPbrMaterialStruct(baselineMaterial);
        Material outputMaterial = mapOpenPbrMaterialStructToMaterialStruct(openPbrMaterial);

        compareMaterials(outputMaterial, baselineMaterial, data, data);
    }

    {
        UsdData data;
        fillTransmissionMaterial(data);
        ASSERT_EQ(data.materials.size(), 1);
        Material& baselineMaterial = data.materials[0];

        OpenPbrMaterial openPbrMaterial =
          mapMaterialStructToOpenPbrMaterialStruct(baselineMaterial);
        Material outputMaterial = mapOpenPbrMaterialStructToMaterialStruct(openPbrMaterial);

        compareMaterials(outputMaterial, baselineMaterial, data, data);
    }
}

TEST(FileFormatUtilsTests, writeUsdPreviewSurface)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    fillGeneralTestMaterial(data);
    fillTextureTestMaterial(data);
    fillTransmissionMaterial(data);

    WriteLayerOptions options;
    options.writeUsdPreviewSurface = true;
    options.writeASM = false;
    options.writeOpenPBR = false;

    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);
    // Clear the doc string, since it contains the date and version number and hence would have to
    // be updated all the time
    layer->SetDocumentation("");

    ASSERT_USDA(layer, assetDir + "data/baseline_writeUsdPreviewSurface.usda");
}

#ifdef USD_FILEFORMATS_ENABLE_ASM
TEST(FileFormatUtilsTests, writeASM)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    fillGeneralTestMaterial(data);
    fillTextureTestMaterial(data);
    fillTransmissionMaterial(data);

    WriteLayerOptions options;
    options.writeUsdPreviewSurface = false;
    options.writeASM = true;
    options.writeOpenPBR = false;

    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);
    // Clear the doc string, since it contains the date and version number and hence would have to
    // be updated all the time
    layer->SetDocumentation("");

    ASSERT_USDA(layer, assetDir + "data/baseline_writeASM.usda");
}
#endif // USD_FILEFORMATS_ENABLE_ASM

TEST(FileFormatUtilsTests, writeOpenPBR)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    fillGeneralTestMaterial(data);
    fillTextureTestMaterial(data);
    fillTransmissionMaterial(data);

    WriteLayerOptions options;
    options.writeUsdPreviewSurface = false;
    options.writeASM = false;
    options.writeOpenPBR = true;

    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);
    // Clear the doc string, since it contains the date and version number and hence would have to
    // be updated all the time
    layer->SetDocumentation("");

    ASSERT_USDA(layer, assetDir + "data/baseline_writeOpenPBR.usda");
}

const SdfPath generalTestMaterialPath("/Scene/Materials/GeneralTestMaterial");
const SdfPath textureTestMaterialPath("/Scene/Materials/TextureTestMaterial");
const SdfPath transmissionTestMaterialPath("/Scene/Materials/TransmissionTestMaterial");

void
defaultBaselineProcessor(Material&)
{}

template<typename BaselineGenerator, typename BaselineProcessor>
void
readAndCompareMaterial(const UsdStageRefPtr& stage,
                       const SdfPath& materialPath,
                       BaselineGenerator baselineGenerator,
                       BaselineProcessor baselineProcessor)
{
    UsdPrim materialPrim = stage->GetPrimAtPath(materialPath);
    ASSERT_TRUE(materialPrim);

    UsdData usdData;

    ReadLayerOptions options;
    ReadLayerContext ctx;
    ctx.stage = stage;
    ctx.usd = &usdData;
    ctx.options = &options;
    ctx.debugTag = "Test";
    ctx.warnAboutMissingAssets = false;
    EXPECT_TRUE(readMaterial(ctx, materialPrim));

    ASSERT_EQ(usdData.materials.size(), 1);
    const Material& material = usdData.materials[0];

    UsdData baselineData;
    baselineGenerator(baselineData);
    ASSERT_EQ(baselineData.materials.size(), 1);
    Material& baselineMaterial = baselineData.materials[0];
    baselineProcessor(baselineMaterial);

    compareMaterials(material, baselineMaterial, usdData, baselineData);
}

TEST(FileFormatUtilsTests, readUsdPreviewSurface)
{
    UsdStageRefPtr stage = UsdStage::Open(assetDir + "data/baseline_writeUsdPreviewSurface.usda");
    ASSERT_TRUE(stage);

    auto usdPreviewSurfaceBaselineProcessor = [](Material& baselineMaterial) {
        // UsdPreviewSurface doesn't support a couple of inputs, so we clear them
        baselineMaterial.specularLevel = {};
        baselineMaterial.normalScale = {};
        baselineMaterial.clearcoatColor = {};
        baselineMaterial.clearcoatIor = {};
        baselineMaterial.clearcoatSpecular = {};
        baselineMaterial.clearcoatNormal = {};
        baselineMaterial.sheenColor = {};
        baselineMaterial.sheenRoughness = {};
        baselineMaterial.anisotropyLevel = {};
        baselineMaterial.anisotropyAngle = {};
        baselineMaterial.transmission = {};
        baselineMaterial.volumeThickness = {};
        baselineMaterial.absorptionDistance = {};
        baselineMaterial.absorptionColor = {};
        baselineMaterial.scatteringDistance = {};
        baselineMaterial.scatteringColor = {};
        baselineMaterial.clearcoatModelsTransmissionTint = false;
        baselineMaterial.isUnlit = false;
    };
    readAndCompareMaterial(
      stage, generalTestMaterialPath, fillGeneralTestMaterial, usdPreviewSurfaceBaselineProcessor);

    readAndCompareMaterial(
      stage, textureTestMaterialPath, fillTextureTestMaterial, usdPreviewSurfaceBaselineProcessor);

    readAndCompareMaterial(stage,
                           transmissionTestMaterialPath,
                           fillTransmissionMaterialAsUsdPreviewSurfaceBaseline,
                           usdPreviewSurfaceBaselineProcessor);
}

TEST(FileFormatUtilsTests, readASM)
{
    UsdStageRefPtr stage = UsdStage::Open(assetDir + "data/baseline_writeASM.usda");
    ASSERT_TRUE(stage);

    readAndCompareMaterial(
      stage, generalTestMaterialPath, fillGeneralTestMaterial, defaultBaselineProcessor);

    readAndCompareMaterial(
      stage, textureTestMaterialPath, fillTextureTestMaterial, defaultBaselineProcessor);

    readAndCompareMaterial(
      stage, transmissionTestMaterialPath, fillTransmissionMaterial, defaultBaselineProcessor);
}

TEST(FileFormatUtilsTests, readOpenPBR)
{
    UsdStageRefPtr stage = UsdStage::Open(assetDir + "data/baseline_writeOpenPBR.usda");
    ASSERT_TRUE(stage);

    readAndCompareMaterial(
      stage, generalTestMaterialPath, fillGeneralTestMaterial, defaultBaselineProcessor);

    readAndCompareMaterial(
      stage, textureTestMaterialPath, fillTextureTestMaterial, defaultBaselineProcessor);

    readAndCompareMaterial(
      stage, transmissionTestMaterialPath, fillTransmissionMaterial, defaultBaselineProcessor);
}

TEST(FileFormatUtilsTests, invalidNetworkReading)
{
    UsdStageRefPtr stage = UsdStage::Open(assetDir + "data/test_invalidNetworks.usda");
    ASSERT_TRUE(stage);

    UsdPrim materials = stage->GetPrimAtPath(SdfPath("/Scene/Materials"));
    ASSERT_TRUE(materials);

    for (UsdPrim materialPrim : materials.GetChildren()) {
        UsdShadeMaterial material(materialPrim);
        ASSERT_TRUE(material);

        std::cout << "Reading bad network at " << materialPrim.GetPath().GetText() << std::endl;

        UsdData usdData;

        ReadLayerOptions options;
        ReadLayerContext ctx;
        ctx.stage = stage;
        ctx.usd = &usdData;
        ctx.options = &options;
        ctx.debugTag = "Test";
        ctx.warnAboutMissingAssets = false;
        // This material is bad and we expect a failure to read the network
        EXPECT_FALSE(readMaterial(ctx, materialPrim));

        // XXX This is worth debating, whether or not we should have a partial material in the scene
        // We current continue with a partial material
        ASSERT_EQ(usdData.materials.size(), 1);
    }
}
