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

#include <fileformatutils/featureFlags.h>
#include <fileformatutils/images.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerReadMaterial.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/layerWriteShared.h>
#include <fileformatutils/materials.h>
#include <fileformatutils/naming.h>
#include <fileformatutils/sdfUtils.h>
#include <fileformatutils/usdData.h>

#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/sdf/data.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/schema.h>

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
        input.scale = kOpenGLNormalTexScale;
        input.bias = kOpenGLNormalTexBias;
        m.normal = input;
        m.clearcoatNormal = input;
        // Put DirectX convention decoding values on the clearcoat normals
        m.clearcoatNormal.scale = kDirectXNormalTexScale;
        m.clearcoatNormal.bias = kDirectXNormalTexBias;
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
fillScaledNormalTestMaterial(UsdData& data)
{
    auto [normalId, normalImage] = data.addImage();
    normalImage.name = "normal";
    normalImage.uri = "textures/normal.png";
    normalImage.format = ImageFormat::ImageFormatPng;

    Material& m = data.addMaterial().second;
    m.name = "ScaledNormalTestMaterial";
    m.displayName = "Scaled Normal Test Material";

    // Scaled OpenGL convention normals on both the primary and clearcoat normal slots. This
    // exercises the OpenPBR write path's extraction of normalScale onto ND_normalmap's scale
    // input (the standard MaterialX normal strength pattern, e.g. glTF KHR_materials_normalTexture
    // scale).
    Input input;
    input.image = normalId;
    input.channel = AdobeTokens->rgb;
    input.colorspace = AdobeTokens->raw;
    const float nmScale = 0.75f;
    input.scale = kOpenGLNormalTexScale * nmScale;
    input.bias = kOpenGLNormalTexBias * nmScale;
    m.normal = input;
    m.clearcoatNormal = input;
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

OpenPbrMaterial&
fillGeneralTestOpenPbrMaterial(UsdData& data)
{
    OpenPbrMaterial& m = data.addOpenPbrMaterial().second;
    m.name = "GeneralTestMaterial";
    m.displayName = "General Test Material";

    m.useSpecularWorkflow = true;
    m.base_color = Input{ VtValue(GfVec3f(1.0f, 0.5f, 0.25f)) };
    m.emission_color = Input{ VtValue(GfVec3f(1.0f, 0.5f, 0.25f)) };
    m.emission_luminance = Input{ VtValue(kAsmToOpenPbrEmissionFactor) };
    m.specular_weight = Input{ VtValue(0.5f) };
    m.specular_color = Input{ VtValue(GfVec3f(1.0f, 0.0f, 1.0f)) };
    m.geometry_normal = Input{ VtValue(GfVec3f(0.5f, 0.5f, 0.5f)) };
    m.normalScale = 0.5f;
    m.base_metalness = Input{ VtValue(0.22f) };
    m.specular_roughness = Input{ VtValue(0.66f) };
    m.coat_weight = Input{ VtValue(0.55f) };
    m.coat_color = Input{ VtValue(GfVec3f(1.0f, 1.0f, 0.0f)) };
    m.coat_roughness = Input{ VtValue(0.44f) };
    m.coat_ior = Input{ VtValue(1.33f) };
    m.coatSpecularLevel = Input{ VtValue(0.88f) };
    m.geometry_coat_normal = Input{ VtValue(GfVec3f(0.66f, 0.0f, 0.66f)) };
    m.fuzz_weight = Input{ VtValue(1.0f) };
    m.fuzz_color = Input{ VtValue(GfVec3f(0.0f, 1.0f, 1.0f)) };
    m.fuzz_roughness = Input{ VtValue(0.5f) };
    m.specular_roughness_anisotropy = Input{ VtValue(0.321f) };
    m.anisotropyAngle = Input{ VtValue(0.777f) };
    m.geometry_opacity = Input{ VtValue(0.8f) };
    m.opacityThreshold = 0.75f;
    m.displacement = Input{ VtValue(1.23f) };
    m.occlusion = Input{ VtValue(0.01f) };
    m.specular_ior = Input{ VtValue(1.55f) };
    m.transmission_weight = Input{ VtValue(0.123f) };
    m.volumeThickness = Input{ VtValue(0.987f) };
    m.transmission_depth = Input{ VtValue(111.0f) };
    m.transmission_color = Input{ VtValue(GfVec3f(0.25f, 0.5f, 1.0f)) };
    m.subsurface_weight = Input{ VtValue(1.0f) };
    m.subsurface_radius = Input{ VtValue(222.0f) };
    m.subsurface_color = Input{ VtValue(GfVec3f(1.0f, 0.5f, 1.0f)) };
    m.clearcoatModelsTransmissionTint = true;
    m.isUnlit = true;
    return m;
}

void
fillTextureTestOpenPbrMaterial(UsdData& data)
{
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

    OpenPbrMaterial& m = data.addOpenPbrMaterial().second;
    m.name = "TextureTestMaterial";
    m.displayName = "Texture Test Material";

    // Color textures
    {
        Input input;
        input.image = colorId;
        input.channel = AdobeTokens->rgb;
        input.colorspace = AdobeTokens->sRGB;
        m.base_color = input;

        input.wrapS = AdobeTokens->clamp;
        input.wrapT = AdobeTokens->mirror;
        input.scale = GfVec4f(1.0f, 2.0f, 0.5f, 1.0f);
        input.bias = GfVec4f(0.1f, 0.2f, 0.3f, 0.0f);
        input.uvRotation = 15.0f;
        input.uvScale = GfVec2f(1.5f, 0.75f);
        input.uvTranslation = GfVec2f(0.12f, 3.45f);
        m.emission_color = input;
    }
    m.emission_luminance = Input{ VtValue(kAsmToOpenPbrEmissionFactor) };

    // Normal maps
    {
        Input input;
        input.image = normalId;
        input.channel = AdobeTokens->rgb;
        input.colorspace = AdobeTokens->raw;
        input.scale = kOpenGLNormalTexScale;
        input.bias = kOpenGLNormalTexBias;
        m.geometry_normal = input;
        m.geometry_coat_normal = input;
        // Put DirectX convention decoding values on the clearcoat normals
        m.geometry_coat_normal.scale = kDirectXNormalTexScale;
        m.geometry_coat_normal.bias = kDirectXNormalTexBias;
    }

    // Greyscale maps
    {
        Input input;
        input.image = greyscaleId;
        input.channel = AdobeTokens->r;
        input.colorspace = AdobeTokens->raw;

        input.wrapS = AdobeTokens->black;
        input.wrapT = AdobeTokens->black;
        input.scale = GfVec4f(0.55f, 1.0f, 1.0f, 1.0f);
        input.bias = GfVec4f(0.1f, 0.0f, 0.0f, 0.0f);
        input.uvRotation = 15.0f;
        input.uvScale = GfVec2f(1.5f, 0.75f);
        input.uvTranslation = GfVec2f(0.12f, 3.45f);
        m.specular_roughness = input;
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
        m.coat_weight = input;
    }
}

void
fillScaledNormalTestOpenPbrMaterial(UsdData& data)
{
    auto [normalId, normalImage] = data.addImage();
    normalImage.name = "normal";
    normalImage.uri = "textures/normal.png";
    normalImage.format = ImageFormat::ImageFormatPng;

    OpenPbrMaterial& m = data.addOpenPbrMaterial().second;
    m.name = "ScaledNormalTestMaterial";
    m.displayName = "Scaled Normal Test Material";

    // Scaled OpenGL convention normals on both geometry_normal and geometry_coat_normal. The
    // OpenPBR write path should extract the normalScale factor onto ND_normalmap's scale input
    // for each, leaving the texture reader at identity.
    Input input;
    input.image = normalId;
    input.channel = AdobeTokens->rgb;
    input.colorspace = AdobeTokens->raw;
    const float nmScale = 0.75f;
    input.scale = kOpenGLNormalTexScale * nmScale;
    input.bias = kOpenGLNormalTexBias * nmScale;
    m.geometry_normal = input;
    m.geometry_coat_normal = input;
}

void
fillTransmissionOpenPbrMaterial(UsdData& data)
{
    OpenPbrMaterial& m = data.addOpenPbrMaterial().second;
    m.name = "TransmissionTestMaterial";
    m.displayName = "Transmission Test Material";

    m.transmission_weight = Input{ VtValue(0.543f) };
}

void
fillTransmissionOpenPbrMaterialAsUsdPreviewSurfaceBaseline(UsdData& data)
{
    OpenPbrMaterial& m = data.addOpenPbrMaterial().second;
    m.name = "TransmissionTestMaterial";
    m.displayName = "Transmission Test Material";

    m.geometry_opacity = Input{ VtValue(1.0f - 0.543f) };
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
        ASSERT_TRUE(input.image < static_cast<int>(data.images.size())) << inputName;
        ASSERT_TRUE(baseline.image < static_cast<int>(baselineData.images.size())) << inputName;
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

void
compareOpenPbrMaterials(const OpenPbrMaterial& material,
                        const OpenPbrMaterial& baseline,
                        const UsdData& data,
                        const UsdData& baselineData)
{
    EXPECT_EQ(material.name, baseline.name);
    EXPECT_EQ(material.displayName, baseline.displayName);

    EXPECT_EQ(material.clearcoatModelsTransmissionTint, baseline.clearcoatModelsTransmissionTint);
    EXPECT_EQ(material.isUnlit, baseline.isUnlit);
    EXPECT_EQ(material.useSpecularWorkflow, baseline.useSpecularWorkflow);
    EXPECT_FLOAT_EQ(material.normalScale, baseline.normalScale);
    EXPECT_FLOAT_EQ(material.opacityThreshold, baseline.opacityThreshold);

#define COMP_INPUT(x) compareInputs(#x, material.x, baseline.x, data, baselineData);
    COMP_INPUT(base_color)
    COMP_INPUT(base_metalness)
    COMP_INPUT(specular_weight)
    COMP_INPUT(specular_color)
    COMP_INPUT(specular_roughness)
    COMP_INPUT(specular_ior)
    COMP_INPUT(specular_roughness_anisotropy)
    COMP_INPUT(transmission_weight)
    COMP_INPUT(transmission_color)
    COMP_INPUT(transmission_depth)
    COMP_INPUT(subsurface_weight)
    COMP_INPUT(subsurface_color)
    COMP_INPUT(subsurface_radius)
    COMP_INPUT(subsurface_radius_scale)
    COMP_INPUT(fuzz_weight)
    COMP_INPUT(fuzz_color)
    COMP_INPUT(fuzz_roughness)
    COMP_INPUT(coat_weight)
    COMP_INPUT(coat_color)
    COMP_INPUT(coat_roughness)
    COMP_INPUT(coat_ior)
    COMP_INPUT(emission_luminance)
    COMP_INPUT(emission_color)
    COMP_INPUT(geometry_opacity)
    COMP_INPUT(geometry_normal)
    COMP_INPUT(geometry_coat_normal)
    COMP_INPUT(displacement)
    COMP_INPUT(occlusion)
    COMP_INPUT(anisotropyAngle)
    COMP_INPUT(coatSpecularLevel)
    COMP_INPUT(volumeThickness)
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

    const bool useOpenPbr = isNativeOpenPbrProcessingEnabled();
    if (useOpenPbr) {
        fillGeneralTestOpenPbrMaterial(data);
        fillTextureTestOpenPbrMaterial(data);
        fillScaledNormalTestOpenPbrMaterial(data);
        fillTransmissionOpenPbrMaterial(data);
    } else {
        fillGeneralTestMaterial(data);
        fillTextureTestMaterial(data);
        fillScaledNormalTestMaterial(data);
        fillTransmissionMaterial(data);
    }

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

// image.uri can originate in untrusted file content, so the assetsPath write
// loop must refuse any uri that resolves outside the chosen export directory.
TEST(FileFormatUtilsTests, assetsPathRejectsTraversalUris)
{
    namespace fs = std::filesystem;
    const fs::path outDir = fs::temp_directory_path() / "fileformatutils-assetspath-traversal";
    const fs::path sentinelDir = outDir.parent_path();
    const fs::path escapedRel = sentinelDir / "fileformatutils-escape-rel.png";
    const fs::path escapedAbs = sentinelDir / "fileformatutils-escape-abs.png";
    fs::remove_all(outDir);
    fs::remove(escapedRel);
    fs::remove(escapedAbs);

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    const std::vector<uint8_t> bytes = { 1, 2, 3, 4 };
    // A benign nested uri (must be written) plus two hostile ones that try to
    // escape assetsPath via "../" and via an absolute path (must be refused).
    ImageAsset& safeImg = data.addImage().second;
    safeImg.uri = "nested/safe.png";
    safeImg.image = bytes;
    ImageAsset& relImg = data.addImage().second;
    relImg.uri = "../fileformatutils-escape-rel.png";
    relImg.image = bytes;
    ImageAsset& absImg = data.addImage().second;
    absImg.uri = escapedAbs.string();
    absImg.image = bytes;

    WriteLayerOptions options;
    options.assetsPath = outDir.string();
    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);

    // The benign image lands inside assetsPath...
    EXPECT_TRUE(fs::exists(outDir / "nested" / "safe.png"));
    // ...and neither hostile uri escaped the export directory.
    EXPECT_FALSE(fs::exists(escapedRel)) << "\"../\" traversal escaped assetsPath";
    EXPECT_FALSE(fs::exists(escapedAbs)) << "absolute uri escaped assetsPath";

    // Remove the escape targets too, so a guard regression doesn't leave stray
    // files in the temp dir's parent on failure.
    fs::remove_all(outDir);
    fs::remove(escapedRel);
    fs::remove(escapedAbs);
}

#ifdef USD_FILEFORMATS_ENABLE_ASM
TEST(FileFormatUtilsTests, writeASM)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    const bool useOpenPbr = isNativeOpenPbrProcessingEnabled();
    if (useOpenPbr) {
        fillGeneralTestOpenPbrMaterial(data);
        fillTextureTestOpenPbrMaterial(data);
        fillScaledNormalTestOpenPbrMaterial(data);
        fillTransmissionOpenPbrMaterial(data);
    } else {
        fillGeneralTestMaterial(data);
        fillTextureTestMaterial(data);
        fillScaledNormalTestMaterial(data);
        fillTransmissionMaterial(data);
    }

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

    const bool useOpenPbr = isNativeOpenPbrProcessingEnabled();
    if (useOpenPbr) {
        fillGeneralTestOpenPbrMaterial(data);
        fillTextureTestOpenPbrMaterial(data);
        fillScaledNormalTestOpenPbrMaterial(data);
        fillTransmissionOpenPbrMaterial(data);
    } else {
        fillGeneralTestMaterial(data);
        fillTextureTestMaterial(data);
        fillScaledNormalTestMaterial(data);
        fillTransmissionMaterial(data);
    }

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

TEST(FileFormatUtilsTests, writeNodeCustomProperties)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    auto [rootIdx, root] = data.addNode(-1); // addNode(-1) pushes to rootNodes
    root.name = "Root";
    root.customProperties["test:str"] = VtValue(std::string("hello"));
    root.customProperties["test:flag"] = VtValue(true);
    root.customProperties["test:count"] = VtValue(int(7));
    root.customProperties["test:scale"] = VtValue(2.5f);
    root.customProperties["test:vec"] = VtValue(GfVec3f(1.0f, 2.0f, 3.0f));
    root.customProperties["test:arr"] = VtValue(VtArray<int>({ 1, 2, 3 }));
    root.customProperties["test:nested"] =
      VtValue(VtDictionary{ { "a", VtValue(int(1)) }, { "b", VtValue(int(2)) } });

    // writeLayer nests UsdData root nodes under a prim named from the layer's display name,
    // so "Root" is authored at "/Scene/Root".
    WriteLayerOptions options;
    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);

    SdfPrimSpecHandle prim = layer->GetPrimAtPath(SdfPath("/Scene/Root"));
    ASSERT_TRUE(prim);
    VtDictionary customData = prim->GetCustomData();

    // Each entry lands verbatim in customData: values hold their native type directly (no
    // SdfValueTypeName wrapping), and a nested dictionary round-trips intact.
    EXPECT_EQ(customData.size(), 7u);
    EXPECT_EQ(customData["test:str"], VtValue(std::string("hello")));
    EXPECT_EQ(customData["test:flag"], VtValue(true));
    EXPECT_EQ(customData["test:count"], VtValue(int(7)));
    EXPECT_EQ(customData["test:scale"], VtValue(2.5f));
    EXPECT_EQ(customData["test:vec"], VtValue(GfVec3f(1, 2, 3)));
    EXPECT_EQ(customData["test:arr"], VtValue(VtArray<int>({ 1, 2, 3 })));
    EXPECT_EQ(customData["test:nested"],
              VtValue(VtDictionary{ { "a", VtValue(int(1)) }, { "b", VtValue(int(2)) } }));
}

TEST(FileFormatUtilsTests, writeNodeCustomPropertiesEmpty)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    auto [rootIdx, root] = data.addNode(-1);
    root.name = "Root";
    // customProperties left empty.

    WriteLayerOptions options;
    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);

    SdfPrimSpecHandle prim = layer->GetPrimAtPath(SdfPath("/Scene/Root"));
    ASSERT_TRUE(prim);
    EXPECT_TRUE(prim->GetCustomData().empty());
}

TEST(FileFormatUtilsTests, writeNodeCustomPropertiesAuthorsVerbatim)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());
    UsdData data;

    auto [rootIdx, root] = data.addNode(-1);
    root.name = "Root";
    // customData is opaque metadata, not typed attributes, so nothing is filtered: keys that are
    // not valid attribute names and empty values are authored as-is.
    root.customProperties["test:ok"] = VtValue(true);
    root.customProperties["bad name"] = VtValue(int(5));
    root.customProperties["test:empty"] = VtValue();

    WriteLayerOptions options;
    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);

    SdfPrimSpecHandle prim = layer->GetPrimAtPath(SdfPath("/Scene/Root"));
    ASSERT_TRUE(prim);
    VtDictionary customData = prim->GetCustomData();
    EXPECT_EQ(customData.size(), 3u);
    EXPECT_EQ(customData["test:ok"], VtValue(true));
    EXPECT_EQ(customData["bad name"], VtValue(int(5)));
    ASSERT_GT(customData.count("test:empty"), 0u) << "empty value should still be authored";
    EXPECT_TRUE(customData["test:empty"].IsEmpty());
}

// Guards that writeCustomProperties composes recursively (VtDictionaryOverRecursive), not
// shallowly: overlaying a partial update under a nested key must preserve the untouched siblings
// rather than replacing the whole sub-dictionary.
TEST(SdfUtilsTest, writeCustomPropertiesRecursivelyMergesPreservingSiblings)
{
    SdfAbstractDataRefPtr data(new SdfData());
    createPseudoRootSpec(&*data);
    const SdfPath prim = createPrimSpec(&*data, SdfPath::AbsoluteRootPath(), TfToken("Node"));

    // Seed existing customData with a nested sub-dictionary and a top-level sibling.
    VtDictionary seed{ { "test",
                         VtValue(VtDictionary{ { "keep", VtValue(int(1)) },
                                               { "x", VtValue(std::string("old")) } }) },
                       { "otherTop", VtValue(int(9)) } };
    setPrimMetadata(&*data, prim, SdfFieldKeys->CustomData, VtValue(seed));

    // Overlay a partial update under the same nested key.
    writeCustomProperties(
      &*data,
      prim,
      VtDictionary{ { "test",
                      VtValue(VtDictionary{ { "x", VtValue(std::string("new")) },
                                            { "y", VtValue(int(2)) } }) } });

    VtDictionary result;
    SdfAbstractDataTypedValue<VtDictionary> getter(&result);
    ASSERT_TRUE(data->Has(prim, SdfFieldKeys->CustomData, &getter));

    // Top-level sibling survives; "test" remains a dictionary.
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result["otherTop"], VtValue(int(9)));
    ASSERT_TRUE(result["test"].IsHolding<VtDictionary>());

    // Within "test": untouched key preserved, overlapping key overwritten, new key added. A shallow
    // merge would have replaced the whole sub-dict, dropping "keep".
    VtDictionary nested = result["test"].UncheckedGet<VtDictionary>();
    EXPECT_EQ(nested.size(), 3u);
    EXPECT_EQ(nested["keep"], VtValue(int(1)));
    EXPECT_EQ(nested["x"], VtValue(std::string("new")));
    EXPECT_EQ(nested["y"], VtValue(int(2)));
}

// The single-key convenience setter must accumulate, not clobber: a second write keeps the first.
TEST(SdfUtilsTest, writeCustomPropertySingleKeyPreservesExisting)
{
    SdfAbstractDataRefPtr data(new SdfData());
    createPseudoRootSpec(&*data);
    const SdfPath prim = createPrimSpec(&*data, SdfPath::AbsoluteRootPath(), TfToken("Node"));

    writeCustomProperty(&*data, prim, "test:first", VtValue(int(1)));
    writeCustomProperty(&*data, prim, "test:second", VtValue(int(2)));

    VtDictionary result;
    SdfAbstractDataTypedValue<VtDictionary> getter(&result);
    ASSERT_TRUE(data->Has(prim, SdfFieldKeys->CustomData, &getter));

    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result["test:first"], VtValue(int(1)));
    EXPECT_EQ(result["test:second"], VtValue(int(2)));
}

const SdfPath generalTestMaterialPath("/Scene/Materials/GeneralTestMaterial");
const SdfPath textureTestMaterialPath("/Scene/Materials/TextureTestMaterial");
const SdfPath scaledNormalTestMaterialPath("/Scene/Materials/ScaledNormalTestMaterial");
const SdfPath transmissionTestMaterialPath("/Scene/Materials/TransmissionTestMaterial");

void
defaultBaselineProcessor(Material&)
{}

// The OpenPBR write path extracts normalScale from the texture reader's scale/bias and passes it
// to ND_normalmap's scale input. The read path reconstructs standard GL decode values, so the
// round-trip normalizes the normal map scale/bias to standard OpenGL convention.
void
normalScaleRoundTripMaterialProcessor(Material& m)
{
    m.normal.scale = kOpenGLNormalTexScale;
    m.normal.bias = kOpenGLNormalTexBias;
}

// Same rationale as normalScaleRoundTripMaterialProcessor, applied to both normal slots of the
// scaled-normal test material.
void
scaledNormalRoundTripMaterialProcessor(Material& m)
{
    m.normal.scale = kOpenGLNormalTexScale;
    m.normal.bias = kOpenGLNormalTexBias;
    m.clearcoatNormal.scale = kOpenGLNormalTexScale;
    m.clearcoatNormal.bias = kOpenGLNormalTexBias;
}

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

void
defaultOpenPbrBaselineProcessor(OpenPbrMaterial&)
{}

void
normalScaleRoundTripOpenPbrProcessor(OpenPbrMaterial& m)
{
    m.geometry_normal.scale = kOpenGLNormalTexScale;
    m.geometry_normal.bias = kOpenGLNormalTexBias;
}

// Scaled-normal test has scaled-OpenGL on both geometry_normal and geometry_coat_normal.
void
scaledNormalRoundTripOpenPbrProcessor(OpenPbrMaterial& m)
{
    m.geometry_normal.scale = kOpenGLNormalTexScale;
    m.geometry_normal.bias = kOpenGLNormalTexBias;
    m.geometry_coat_normal.scale = kOpenGLNormalTexScale;
    m.geometry_coat_normal.bias = kOpenGLNormalTexBias;
}

template<typename BaselineGenerator, typename BaselineProcessor>
void
readAndCompareOpenPbrMaterial(const UsdStageRefPtr& stage,
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

    ASSERT_EQ(usdData.openPbrMaterials.size(), 1);
    const OpenPbrMaterial& material = usdData.openPbrMaterials[0];

    UsdData baselineData;
    baselineGenerator(baselineData);
    ASSERT_EQ(baselineData.openPbrMaterials.size(), 1);
    OpenPbrMaterial& baselineMaterial = baselineData.openPbrMaterials[0];
    baselineProcessor(baselineMaterial);

    compareOpenPbrMaterials(material, baselineMaterial, usdData, baselineData);
}

TEST(FileFormatUtilsTests, readUsdPreviewSurface)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "data/baseline_writeUsdPreviewSurface.usda");
    ASSERT_TRUE(stage);

    const bool useOpenPbr = isNativeOpenPbrProcessingEnabled();
    if (useOpenPbr) {
        // UsdPreviewSurface doesn't support a number of inputs, so we clear them
        // from the baseline before comparison
        auto processor = [](OpenPbrMaterial& m) {
            m.specular_weight = {};
            m.normalScale = 1.0f;
            m.coat_color = {};
            m.coat_ior = {};
            m.coatSpecularLevel = {};
            m.geometry_coat_normal = {};
            m.fuzz_weight = {};
            m.fuzz_color = {};
            m.fuzz_roughness = {};
            m.specular_roughness_anisotropy = {};
            m.anisotropyAngle = {};
            m.transmission_weight = {};
            m.volumeThickness = {};
            m.transmission_depth = {};
            m.transmission_color = {};
            m.subsurface_weight = {};
            m.subsurface_radius = {};
            m.subsurface_color = {};
            m.emission_luminance = {};
            m.clearcoatModelsTransmissionTint = false;
            m.isUnlit = false;
        };
        readAndCompareOpenPbrMaterial(
          stage, generalTestMaterialPath, fillGeneralTestOpenPbrMaterial, processor);
        readAndCompareOpenPbrMaterial(
          stage, textureTestMaterialPath, fillTextureTestOpenPbrMaterial, processor);
        readAndCompareOpenPbrMaterial(
          stage, scaledNormalTestMaterialPath, fillScaledNormalTestOpenPbrMaterial, processor);
        readAndCompareOpenPbrMaterial(stage,
                                      transmissionTestMaterialPath,
                                      fillTransmissionOpenPbrMaterialAsUsdPreviewSurfaceBaseline,
                                      processor);
    } else {
        // UsdPreviewSurface doesn't support a number of inputs, so we clear them
        // from the baseline before comparison
        auto processor = [](Material& baselineMaterial) {
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
        readAndCompareMaterial(stage, generalTestMaterialPath, fillGeneralTestMaterial, processor);
        readAndCompareMaterial(stage, textureTestMaterialPath, fillTextureTestMaterial, processor);
        readAndCompareMaterial(
          stage, scaledNormalTestMaterialPath, fillScaledNormalTestMaterial, processor);
        readAndCompareMaterial(stage,
                               transmissionTestMaterialPath,
                               fillTransmissionMaterialAsUsdPreviewSurfaceBaseline,
                               processor);
    }
}

TEST(FileFormatUtilsTests, readASM)
{
    // ASM-specific: this test reads an ASM-network golden USDA. Remove this skip (and the
    // test) when ASM is fully retired.
    if (!isWriteAsmEnabled()) {
        GTEST_SKIP() << "ASM writer is disabled; ASM-specific test will be removed with ASM";
    }

    UsdStageRefPtr stage = openAssetStage(assetDir + "data/baseline_writeASM.usda");
    ASSERT_TRUE(stage);

    const bool useOpenPbr = isNativeOpenPbrProcessingEnabled();
    if (useOpenPbr) {
        readAndCompareOpenPbrMaterial(stage,
                                      generalTestMaterialPath,
                                      fillGeneralTestOpenPbrMaterial,
                                      defaultOpenPbrBaselineProcessor);
        readAndCompareOpenPbrMaterial(stage,
                                      textureTestMaterialPath,
                                      fillTextureTestOpenPbrMaterial,
                                      defaultOpenPbrBaselineProcessor);
        readAndCompareOpenPbrMaterial(stage,
                                      scaledNormalTestMaterialPath,
                                      fillScaledNormalTestOpenPbrMaterial,
                                      defaultOpenPbrBaselineProcessor);
        readAndCompareOpenPbrMaterial(stage,
                                      transmissionTestMaterialPath,
                                      fillTransmissionOpenPbrMaterial,
                                      defaultOpenPbrBaselineProcessor);
    } else {
        readAndCompareMaterial(
          stage, generalTestMaterialPath, fillGeneralTestMaterial, defaultBaselineProcessor);
        readAndCompareMaterial(
          stage, textureTestMaterialPath, fillTextureTestMaterial, defaultBaselineProcessor);
        readAndCompareMaterial(stage,
                               scaledNormalTestMaterialPath,
                               fillScaledNormalTestMaterial,
                               defaultBaselineProcessor);
        readAndCompareMaterial(
          stage, transmissionTestMaterialPath, fillTransmissionMaterial, defaultBaselineProcessor);
    }
}

TEST(FileFormatUtilsTests, readOpenPBR)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "data/baseline_writeOpenPBR.usda");
    ASSERT_TRUE(stage);

    const bool useOpenPbr = isNativeOpenPbrProcessingEnabled();
    if (useOpenPbr) {
        readAndCompareOpenPbrMaterial(stage,
                                      generalTestMaterialPath,
                                      fillGeneralTestOpenPbrMaterial,
                                      defaultOpenPbrBaselineProcessor);
        readAndCompareOpenPbrMaterial(stage,
                                      textureTestMaterialPath,
                                      fillTextureTestOpenPbrMaterial,
                                      normalScaleRoundTripOpenPbrProcessor);
        readAndCompareOpenPbrMaterial(stage,
                                      scaledNormalTestMaterialPath,
                                      fillScaledNormalTestOpenPbrMaterial,
                                      scaledNormalRoundTripOpenPbrProcessor);
        readAndCompareOpenPbrMaterial(stage,
                                      transmissionTestMaterialPath,
                                      fillTransmissionOpenPbrMaterial,
                                      defaultOpenPbrBaselineProcessor);
    } else {
        readAndCompareMaterial(
          stage, generalTestMaterialPath, fillGeneralTestMaterial, defaultBaselineProcessor);
        readAndCompareMaterial(stage,
                               textureTestMaterialPath,
                               fillTextureTestMaterial,
                               normalScaleRoundTripMaterialProcessor);
        readAndCompareMaterial(stage,
                               scaledNormalTestMaterialPath,
                               fillScaledNormalTestMaterial,
                               scaledNormalRoundTripMaterialProcessor);
        readAndCompareMaterial(
          stage, transmissionTestMaterialPath, fillTransmissionMaterial, defaultBaselineProcessor);
    }
}

TEST(FileFormatUtilsTests, invalidNetworkReading)
{
    UsdStageRefPtr stage = openAssetStage(assetDir + "data/test_invalidNetworks.usda");
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
        if (isNativeOpenPbrProcessingEnabled()) {
            ASSERT_EQ(usdData.openPbrMaterials.size(), 1);
        } else {
            ASSERT_EQ(usdData.materials.size(), 1);
        }
    }
}

namespace {
int
countWarningsMentioning(const std::vector<std::string>& warnings, const std::string& needle)
{
    int count = 0;
    for (const auto& w : warnings) {
        if (w.find(needle) != std::string::npos) {
            ++count;
        }
    }
    return count;
}
}

TEST(FileFormatUtilsTests, deprecatedMaterialSettingsWarnOncePerProcess)
{
    resetDeprecationWarningOnceFlagsForTesting();

    UsdDiagnosticDelegate delegate;

    // Three calls with both deprecated settings enabled — expect exactly one
    // warning per setting regardless of call count.
    warnOnceOnDeprecatedMaterialSettings(/*writeASM*/ true, /*writeUsdPreviewSurface*/ true);
    warnOnceOnDeprecatedMaterialSettings(/*writeASM*/ true, /*writeUsdPreviewSurface*/ true);
    warnOnceOnDeprecatedMaterialSettings(/*writeASM*/ true, /*writeUsdPreviewSurface*/ true);

    EXPECT_EQ(countWarningsMentioning(delegate.GetWarnings(), "USD_FILEFORMATS_WRITE_ASM"), 1);
    EXPECT_EQ(
      countWarningsMentioning(delegate.GetWarnings(), "USD_FILEFORMATS_WRITE_USDPREVIEWSURFACE"),
      1);

    // Each warning should tell the user how to suppress it by setting the deprecated
    // flag to 0.
    for (const auto& w : delegate.GetWarnings()) {
        EXPECT_NE(w.find("=0"), std::string::npos);
    }

    // After reset, a call with both settings false must stay silent.
    resetDeprecationWarningOnceFlagsForTesting();
    UsdDiagnosticDelegate silent;
    warnOnceOnDeprecatedMaterialSettings(/*writeASM*/ false, /*writeUsdPreviewSurface*/ false);
    EXPECT_EQ(countWarningsMentioning(silent.GetWarnings(), "USD_FILEFORMATS_WRITE_ASM"), 0);
    EXPECT_EQ(
      countWarningsMentioning(silent.GetWarnings(), "USD_FILEFORMATS_WRITE_USDPREVIEWSURFACE"), 0);
}

/// InputTranslator tests //////////////////////////////////////////////////////////////////////////

// Verify translateMax with constant scalar inputs returns the per-element maximum.
TEST(InputTranslatorTests, TranslateMaxConstantScalar)
{
    std::vector<ImageAsset> images;
    InputTranslator translator(/*exportImages=*/false, images, "test");

    Input a, b, out;
    a.value = VtValue(0.3f);
    b.value = VtValue(0.7f);

    ASSERT_TRUE(translator.translateMax("ab", a, b, out));
    ASSERT_TRUE(out.value.IsHolding<float>());
    EXPECT_FLOAT_EQ(out.value.UncheckedGet<float>(), 0.7f);

    // Swap inputs — result is the same
    ASSERT_TRUE(translator.translateMax("ba", b, a, out));
    ASSERT_TRUE(out.value.IsHolding<float>());
    EXPECT_FLOAT_EQ(out.value.UncheckedGet<float>(), 0.7f);

    // Equal inputs
    ASSERT_TRUE(translator.translateMax("aa", a, a, out));
    EXPECT_FLOAT_EQ(out.value.UncheckedGet<float>(), 0.3f);

    // Empty input must return false
    Input empty;
    EXPECT_FALSE(translator.translateMax("emptyLeft", empty, a, out));
    EXPECT_FALSE(translator.translateMax("emptyRight", a, empty, out));
}

// Verify translateMax with constant GfVec3f inputs operates per-channel.
TEST(InputTranslatorTests, TranslateMaxConstantVec3)
{
    std::vector<ImageAsset> images;
    InputTranslator translator(/*exportImages=*/false, images, "test");

    Input a, b, out;
    a.value = VtValue(GfVec3f(0.1f, 0.8f, 0.3f));
    b.value = VtValue(GfVec3f(0.5f, 0.2f, 0.9f));

    ASSERT_TRUE(translator.translateMax("vec3", a, b, out));
    ASSERT_TRUE(out.value.IsHolding<GfVec3f>());
    GfVec3f result = out.value.UncheckedGet<GfVec3f>();
    EXPECT_FLOAT_EQ(result[0], 0.5f); // max(0.1, 0.5)
    EXPECT_FLOAT_EQ(result[1], 0.8f); // max(0.8, 0.2)
    EXPECT_FLOAT_EQ(result[2], 0.9f); // max(0.3, 0.9)
}

// Verify translateMax with image inputs produces the correct per-pixel per-channel maximum.
// Images are injected as intermediates (decoded pixels in mImagesSrc) so no PNG encoding
// is needed on the input side; only the output is encoded.
TEST(InputTranslatorTests, TranslateMaxImages)
{
    std::vector<ImageAsset> images;
    InputTranslator translator(/*exportImages=*/true, images, "test");

    // Image A: 2×2 RGB, every pixel = (0.2, 0.8, 0.4)
    // Image B: 2×2 RGB, every pixel = (0.6, 0.3, 0.7)
    // Expected max:                   (0.6, 0.8, 0.7) at every pixel
    Image imgA, imgB;
    imgA.allocate(2, 2, 3);
    imgB.allocate(2, 2, 3);
    for (int i = 0; i < 4; ++i) {
        imgA.pixels[i * 3 + 0] = 0.2f;
        imgA.pixels[i * 3 + 1] = 0.8f;
        imgA.pixels[i * 3 + 2] = 0.4f;
        imgB.pixels[i * 3 + 0] = 0.6f;
        imgB.pixels[i * 3 + 1] = 0.3f;
        imgB.pixels[i * 3 + 2] = 0.7f;
    }

    Input inA, inB;
    inA.image = translator.addImage(
      std::move(imgA), "imgA", "imgA.png", ImageFormatPng, /*intermediate=*/true);
    inA.channel = AdobeTokens->rgb;
    inB.image = translator.addImage(
      std::move(imgB), "imgB", "imgB.png", ImageFormatPng, /*intermediate=*/true);
    inB.channel = AdobeTokens->rgb;

    Input out;
    ASSERT_TRUE(translator.translateMax("maxAB", inA, inB, out));
    ASSERT_GE(out.image, 0);

    // Only the final output image should appear in getImages() — no intermediates
    const std::vector<ImageAsset>& outImages = translator.getImages();
    ASSERT_EQ(outImages.size(), 1u);

    // Decode the output PNG and verify per-pixel values (PNG round-trip introduces small error)
    Image decoded;
    ASSERT_TRUE(decoded.read(outImages[0]));
    ASSERT_EQ(decoded.width, 2);
    ASSERT_EQ(decoded.height, 2);
    ASSERT_EQ(decoded.channels, 3);

    constexpr float kTol = 0.01f; // PNG is 8-bit, so ~0.004 quantization error
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(decoded.pixels[i * 3 + 0], 0.6f, kTol) << "pixel " << i << " R";
        EXPECT_NEAR(decoded.pixels[i * 3 + 1], 0.8f, kTol) << "pixel " << i << " G";
        EXPECT_NEAR(decoded.pixels[i * 3 + 2], 0.7f, kTol) << "pixel " << i << " B";
    }
}

// Verify that intermediate=true results are stored inside the translator and can be
// consumed by a subsequent operation without appearing in getImages().
//
// Pipeline under test:
//   src (0.5, 0.5, 0.5)  ─── translateProduct(×0.8) [intermediate] ──► (0.4, 0.4, 0.4)
//                                                                              │
//                                                            translateMax(0.5) ▼
//                                                                       (0.5, 0.5, 0.5)  [final]
TEST(InputTranslatorTests, TranslateIntermediateChaining)
{
    std::vector<ImageAsset> images;
    InputTranslator translator(/*exportImages=*/true, images, "test");

    // Source image: 1×1 RGB, all channels = 0.5
    Image srcImg;
    srcImg.allocate(1, 1, 3);
    srcImg.pixels = { 0.5f, 0.5f, 0.5f };

    Input src;
    src.image = translator.addImage(
      std::move(srcImg), "src", "src.png", ImageFormatPng, /*intermediate=*/true);
    src.channel = AdobeTokens->rgb;

    // Step 1 (intermediate): multiply by 0.8 → (0.4, 0.4, 0.4), stored in mImagesSrc only
    Input factor;
    factor.value = VtValue(GfVec3f(0.8f, 0.8f, 0.8f));
    Input step1;
    ASSERT_TRUE(translator.translateProduct("step1", src, factor, step1, /*intermediate=*/true));
    EXPECT_GE(step1.image, 0);

    // Step 2 (final): max(step1, 0.5) → per-channel max(0.4, 0.5) = 0.5, stored in mImagesDst
    Input floor;
    floor.value = VtValue(GfVec3f(0.5f, 0.5f, 0.5f));
    Input finalOut;
    ASSERT_TRUE(translator.translateMax("step2", step1, floor, finalOut));
    EXPECT_GE(finalOut.image, 0);

    // Only the non-intermediate final image should be in getImages()
    const std::vector<ImageAsset>& outImages = translator.getImages();
    ASSERT_EQ(outImages.size(), 1u) << "Intermediate image must not appear in getImages()";

    // Verify pixel values: max(0.4, 0.5) = 0.5 per channel
    Image decoded;
    ASSERT_TRUE(decoded.read(outImages[0]));
    constexpr float kTol = 0.01f;
    EXPECT_NEAR(decoded.pixels[0], 0.5f, kTol) << "R";
    EXPECT_NEAR(decoded.pixels[1], 0.5f, kTol) << "G";
    EXPECT_NEAR(decoded.pixels[2], 0.5f, kTol) << "B";
}

// Encode an Image into a source ImageAsset (with real encoded bytes) so that the phong-to-PBR
// bake path, which re-reads source textures from the encoded byte stream, can decode it.
static ImageAsset
makeSourceImage(const Image& image, const std::string& name)
{
    ImageAsset asset;
    asset.name = name;
    asset.uri = name;
    asset.format = ImageFormatPng;
    image.write(asset);
    return asset;
}

// A Phong material with a specular and/or glossiness texture but no diffuse texture must
// convert to PBR without aborting. The empty diffuse component is substituted with a default,
// and the generated diffuse/metallic/roughness textures inherit the specular/gloss dimensions.
TEST(InputTranslatorTests, Phong2PBRNoDiffuseTexture)
{
    // Specular: 2×2 RGB, glossiness: 2×2 single-channel. No diffuse texture.
    Image specularImg, glossImg;
    specularImg.allocate(2, 2, 3);
    glossImg.allocate(2, 2, 1);
    for (int i = 0; i < 4; ++i) {
        specularImg.pixels[i * 3 + 0] = 0.4f;
        specularImg.pixels[i * 3 + 1] = 0.4f;
        specularImg.pixels[i * 3 + 2] = 0.4f;
        glossImg.pixels[i] = 0.6f;
    }

    std::vector<ImageAsset> images;
    images.push_back(makeSourceImage(specularImg, "spec.png"));
    images.push_back(makeSourceImage(glossImg, "gloss.png"));
    InputTranslator translator(/*exportImages=*/true, images, "test");

    Input diffuseIn; // absent: image == -1, value empty
    Input specularIn;
    specularIn.image = 0;
    specularIn.channel = AdobeTokens->rgb;
    Input glossIn;
    glossIn.image = 1;
    glossIn.channel = AdobeTokens->r;

    Input diffuseOut, metallicOut, roughnessOut;
    ASSERT_TRUE(translator.translatePhong2PBR(
      diffuseIn, specularIn, glossIn, diffuseOut, metallicOut, roughnessOut));

    // All three outputs are baked textures dimensioned from the specular/gloss inputs.
    ASSERT_GE(diffuseOut.image, 0);
    ASSERT_GE(metallicOut.image, 0);
    ASSERT_GE(roughnessOut.image, 0);

    const std::vector<ImageAsset>& outImages = translator.getImages();
    Image decodedDiffuse;
    ASSERT_TRUE(decodedDiffuse.read(outImages[diffuseOut.image]));
    EXPECT_EQ(decodedDiffuse.width, 2);
    EXPECT_EQ(decodedDiffuse.height, 2);
    EXPECT_FALSE(decodedDiffuse.pixels.empty());
}

// Same conversion must succeed when only a glossiness texture is present (both diffuse and
// specular source textures absent), exercising the absent-specular handling.
TEST(InputTranslatorTests, Phong2PBROnlyGlossTexture)
{
    Image glossImg;
    glossImg.allocate(2, 2, 1);
    for (int i = 0; i < 4; ++i)
        glossImg.pixels[i] = 0.7f;

    std::vector<ImageAsset> images;
    images.push_back(makeSourceImage(glossImg, "gloss.png"));
    InputTranslator translator(/*exportImages=*/true, images, "test");

    Input diffuseIn;  // absent
    Input specularIn; // absent
    Input glossIn;
    glossIn.image = 0;
    glossIn.channel = AdobeTokens->r;

    Input diffuseOut, metallicOut, roughnessOut;
    ASSERT_TRUE(translator.translatePhong2PBR(
      diffuseIn, specularIn, glossIn, diffuseOut, metallicOut, roughnessOut));

    ASSERT_GE(roughnessOut.image, 0);
    const std::vector<ImageAsset>& outImages = translator.getImages();
    Image decodedRoughness;
    ASSERT_TRUE(decodedRoughness.read(outImages[roughnessOut.image]));
    EXPECT_EQ(decodedRoughness.width, 2);
    EXPECT_EQ(decodedRoughness.height, 2);
    EXPECT_FALSE(decodedRoughness.pixels.empty());
}

TEST(NamingTest, MakeValidUsdIdentifier)
{
    using adobe::usd::MakeValidUsdIdentifier;

    // Punctuation, mixed script, diacritics, leading digit, emoji, and empty input.
    EXPECT_EQ(MakeValidUsdIdentifier("Object_n3d#"), "Object_n3d_");
    EXPECT_EQ(MakeValidUsdIdentifier("京都 Building"), "京都_Building");
    EXPECT_EQ(MakeValidUsdIdentifier("Müller café"), "Müller_café");
    EXPECT_EQ(MakeValidUsdIdentifier("2024_render"), "_2024_render");
    EXPECT_EQ(MakeValidUsdIdentifier("🎨 art"), "__art");
    EXPECT_EQ(MakeValidUsdIdentifier(""), "_");

    // Source-name pattern catalog (foreign-format imports, locale-baked
    // _display_name defaults, rename-UI input) — single-script cases pass
    // through unchanged because every codepoint is XID_Start/Continue.
    EXPECT_EQ(MakeValidUsdIdentifier("カメラ"), "カメラ");                     // ja-JP
    EXPECT_EQ(MakeValidUsdIdentifier("카메라"), "카메라");                     // ko-KR
    EXPECT_EQ(MakeValidUsdIdentifier("相机"), "相机");                         // zh-CN
    EXPECT_EQ(MakeValidUsdIdentifier("Москва"), "Москва");                     // ru-RU Cyrillic
    EXPECT_EQ(MakeValidUsdIdentifier("Δέλτα"), "Δέλτα");                       // Greek
    EXPECT_EQ(MakeValidUsdIdentifier("Окружающая среда"), "Окружающая_среда"); // space -> _
    EXPECT_EQ(MakeValidUsdIdentifier("Standardkamera 1"), "Standardkamera_1"); // de-DE

    // ASCII edge cases: separators, punctuation, parentheses, leading digit.
    EXPECT_EQ(MakeValidUsdIdentifier("MyObject"), "MyObject");
    EXPECT_EQ(MakeValidUsdIdentifier("My Object"), "My_Object");
    EXPECT_EQ(MakeValidUsdIdentifier("Object-1"), "Object_1");
    EXPECT_EQ(MakeValidUsdIdentifier("Object.1"), "Object_1");
    EXPECT_EQ(MakeValidUsdIdentifier("Object (copy)"), "Object__copy_");
    EXPECT_EQ(MakeValidUsdIdentifier("2024_render-2"), "_2024_render_2");

    // Output of every call is itself a valid USD identifier.
    EXPECT_TRUE(pxr::SdfPath::IsValidIdentifier(MakeValidUsdIdentifier("🎨 art")));
    EXPECT_TRUE(pxr::SdfPath::IsValidIdentifier(MakeValidUsdIdentifier("Müller café")));
}

// Verify that uniquifyNames resolves prim-name collisions across all sibling groups that land
// under the same parent Xform in USD: camera, light, meshes (including instanceable), curves,
// and child nodes. Prior to the fix, each geometry group was uniquified in its own private
// namespace, and camera/light were not uniquified against geometry at all, so cross-group
// name collisions could trigger TypeName-overwrite corruption at write time.
TEST(NamingTest, UniquifyNodeSharedSiblingNamespace)
{
    using adobe::usd::uniquifyNames;

    UsdData data;

    // Root node that owns all the siblings under test.
    // addNode(-1) automatically pushes to rootNodes, so no manual push needed.
    auto [rootIdx, root] = data.addNode(-1);

    // Camera and light named "Shape" — written before geometry in _writeNode, so they are
    // seeded into the shared namespace first.
    auto [camIdx, cam] = data.addCamera();
    cam.name = "Shape";
    root.camera = camIdx;

    auto [lightIdx, light] = data.addLight();
    light.name = "Shape";
    root.light = lightIdx;

    // Mesh, curve, child node, and instanceable mesh all named "Shape" — must each resolve
    // to a distinct prim name that doesn't collide with camera or light either.
    auto [meshIdx, mesh] = data.addMesh();
    mesh.name = "Shape";
    root.staticMeshes.push_back(meshIdx);

    auto [curveIdx, curve] = data.addCurve();
    curve.name = "Shape";
    root.curves.push_back(curveIdx);

    // addNode may resize data.nodes, invalidating `root`. Use data.nodes[rootIdx] after this
    // point. addNode also automatically adds childIdx to nodes[rootIdx].children internally.
    auto [childIdx, child] = data.addNode(rootIdx);
    child.name = "Shape";
    child.displayName = "Shape";

    auto [instMeshIdx, instMesh] = data.addMesh();
    instMesh.name = "Shape";
    instMesh.instanceable = true;
    data.nodes[rootIdx].staticMeshes.push_back(instMeshIdx);

    uniquifyNames(data);

    const std::string& camName = data.cameras[camIdx].name;
    const std::string& lightName = data.lights[lightIdx].name;
    const std::string& meshName = data.meshes[meshIdx].name;
    const std::string& curveName = data.curves[curveIdx].name;
    const std::string& childName = data.nodes[childIdx].name;
    const std::string& instMeshName = data.meshes[instMeshIdx].name;

    // All six siblings must have distinct prim names.
    std::vector<std::string> names = { camName,   lightName, meshName,
                                       curveName, childName, instMeshName };
    for (size_t i = 0; i < names.size(); ++i) {
        for (size_t j = i + 1; j < names.size(); ++j) {
            EXPECT_NE(names[i], names[j]) << "collision between sibling " << i << " and " << j;
        }
    }

    // All names must be valid USD identifiers.
    for (const std::string& name : names) {
        EXPECT_TRUE(pxr::SdfPath::IsValidIdentifier(name)) << "invalid identifier: " << name;
    }

    // The child node originally had a displayName — verify it is updated consistently with
    // the renamed prim name (either cleared because names match, or preserved as original).
    const std::string& childDisplayName = data.nodes[childIdx].displayName;
    EXPECT_TRUE(childDisplayName.empty() || childDisplayName != childName);
}

// A node named "Materials" must not collide with the "Materials" scope the writer synthesizes
// under the root when materials exist. A duplicate child name is not validated when writing USDA
// (the two defs merge), but USDC rejects it (crateData enforces unique primChildren). This can
// arise whenever an imported scene already contains a direct child of the root named "Materials".
TEST(NamingTest, MaterialsScopeNodeCollision)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("Scene.usda");
    SdfAbstractDataRefPtr sdfData(new SdfData());

    UsdData data;
    // Root node whose name collides with the synthesized "Materials" scope. addNode(-1) pushes
    // to rootNodes automatically.
    auto& root = data.addNode(-1).second;
    root.name = "Materials";
    root.displayName = "Materials";
    // Any material makes usdData.materials non-empty, so the writer creates the "Materials" scope.
    data.addMaterial().second.name = "SomeMaterial";

    WriteLayerOptions options;
    writeLayer(
      options, data, &*layer, sdfData, "Test Data", "Testing", TestFileFormat::SetLayerData);

    // Find the single root prim without hardcoding its name (derived from the layer stem).
    auto roots = layer->GetRootPrims();
    ASSERT_EQ(roots.size(), 1u);
    const SdfPath rootPath = roots[0]->GetPath();

    // The synthesized scope and the colliding node must be distinct prims.
    EXPECT_TRUE(layer->GetPrimAtPath(rootPath.AppendChild(TfToken("Materials"))))
      << "synthesized Materials scope missing";
    EXPECT_TRUE(layer->GetPrimAtPath(rootPath.AppendChild(TfToken("Materials1"))))
      << "colliding node was not uniquified to Materials1";

    // End-to-end: the layer must serialize to USDC, whose crate format enforces unique
    // primChildren. ArchMakeTmpFileName gives a pid-unique path so concurrent test runs don't
    // collide; cleanup is best-effort via the non-throwing std::error_code overload.
    const std::string usdcPath = ArchMakeTmpFileName("materials_collision_test", ".usdc");
    EXPECT_TRUE(layer->Export(usdcPath)) << "USDC export failed -- duplicate prim children";
    std::error_code ec;
    std::filesystem::remove(usdcPath, ec);
}

// A duplicate child name under one parent isn't caught when writing USDA, but USDC rejects it
// (duplicate primChildren). Authoring one must post a coding error.
TEST(SdfUtilsTest, DuplicateChildPrimPostsCodingError)
{
    SdfAbstractDataRefPtr data(new SdfData());
    createPseudoRootSpec(&*data);
    const SdfPath root = SdfPath::AbsoluteRootPath();

    // First child under the root is fine, no error.
    createPrimSpec(&*data, root, TfToken("Materials"));

    // Adding the same name via the batched node path must be flagged.
    {
        TfErrorMark mark;
        appendToChildList(&*data, root, { TfToken("Materials") });
        EXPECT_FALSE(mark.IsClean()) << "duplicate child via appendToChildList was not flagged";
        mark.Clear();
    }

    // Adding the same name via the single-append path (createPrimSpec, append=true) must be too.
    {
        TfErrorMark mark;
        createPrimSpec(&*data, root, TfToken("Materials"));
        EXPECT_FALSE(mark.IsClean()) << "duplicate child via createPrimSpec was not flagged";
        mark.Clear();
    }
}

// Maps each supported image MIME spelling (including format aliases) to its
// ImageFormat, and confirms unmapped/empty spellings fall back to Unknown.
TEST(FileFormatUtilsTests, getFormatFromMimeType)
{
    // Already-mapped MIME types stay correct.
    EXPECT_EQ(getFormatFromMimeType("image/png"), ImageFormatPng);
    EXPECT_EQ(getFormatFromMimeType("image/jpeg"), ImageFormatJpg);
    EXPECT_EQ(getFormatFromMimeType("image/jpg"), ImageFormatJpg);
    EXPECT_EQ(getFormatFromMimeType("image/x-exr"), ImageFormatExr);
    EXPECT_EQ(getFormatFromMimeType("image/exr"), ImageFormatExr);
    EXPECT_EQ(getFormatFromMimeType("image/vnd.radiance"), ImageFormatHdr);
    EXPECT_EQ(getFormatFromMimeType("image/vnd.adobe.photoshop"), ImageFormatPsd);
    EXPECT_EQ(getFormatFromMimeType("image/tiff"), ImageFormatTiff);

    // Newly-mapped primaries.
    EXPECT_EQ(getFormatFromMimeType("image/bmp"), ImageFormatBmp);
    EXPECT_EQ(getFormatFromMimeType("image/tga"), ImageFormatTga);
    EXPECT_EQ(getFormatFromMimeType("image/webp"), ImageFormatWebp);
    EXPECT_EQ(getFormatFromMimeType("image/tif"), ImageFormatTiff);
    EXPECT_EQ(getFormatFromMimeType("image/hdr"), ImageFormatHdr);

    // Alias MIME types, including application/* for representable formats.
    EXPECT_EQ(getFormatFromMimeType("image/x-windows-bmp"), ImageFormatBmp);
    EXPECT_EQ(getFormatFromMimeType("image/x-ms-bmp"), ImageFormatBmp);
    EXPECT_EQ(getFormatFromMimeType("image/x-targa"), ImageFormatTga);
    EXPECT_EQ(getFormatFromMimeType("application/x-targa"), ImageFormatTga);
    EXPECT_EQ(getFormatFromMimeType("application/x-tga"), ImageFormatTga);
    EXPECT_EQ(getFormatFromMimeType("image/x-photoshop"), ImageFormatPsd);
    EXPECT_EQ(getFormatFromMimeType("application/x-photoshop"), ImageFormatPsd);

    // Noted gaps and unknown input degrade to Unknown (no ImageFormat; the resolver
    // still preserves image/* bytes via its prefix gate).
    EXPECT_EQ(getFormatFromMimeType("application/postscript"), ImageFormatUnknown);
    EXPECT_EQ(getFormatFromMimeType("application/vnd.adobe.illustrator"), ImageFormatUnknown);
    EXPECT_EQ(getFormatFromMimeType("image/gif"), ImageFormatUnknown);
    EXPECT_EQ(getFormatFromMimeType("image/svg+xml"), ImageFormatUnknown);
    EXPECT_EQ(getFormatFromMimeType("application/octet-stream"), ImageFormatUnknown);
    EXPECT_EQ(getFormatFromMimeType("model/gltf-binary"), ImageFormatUnknown);
    EXPECT_EQ(getFormatFromMimeType(""), ImageFormatUnknown);

    // Radiance HDR is new to the enum; confirm the extension pair round-trips.
    EXPECT_EQ(getFormat("hdr"), ImageFormatHdr);
    EXPECT_EQ(getFormatExtension(ImageFormatHdr), "hdr");
}

/// Image tests ////////////////////////////////////////////////////////////////////////////////

// Image::allocate() must reject a width*height*channels product that overflows a
// signed 32-bit int instead of silently wrapping and under-allocating pixels.
TEST(ImageTests, AllocateRejectsOverflowingDimensions)
{
    Image image;
    EXPECT_FALSE(image.allocate(65536, 65536, 4));
    EXPECT_EQ(image.width, 0);
    EXPECT_EQ(image.height, 0);
    EXPECT_EQ(image.channels, 0);
    EXPECT_TRUE(image.pixels.empty());
}

TEST(ImageTests, AllocateRejectsInvalidDimensions)
{
    Image negativeWidth;
    EXPECT_FALSE(negativeWidth.allocate(-1, 4, 4));
    EXPECT_TRUE(negativeWidth.pixels.empty());

    Image zeroHeight;
    EXPECT_FALSE(zeroHeight.allocate(4, 0, 4));
    EXPECT_TRUE(zeroHeight.pixels.empty());

    Image zeroChannels;
    EXPECT_FALSE(zeroChannels.allocate(4, 4, 0));
    EXPECT_TRUE(zeroChannels.pixels.empty());
}

// Regression: a normal small image still allocates, transforms, and round-trips through
// write()/read() correctly. read() shares allocate()'s validation helper, so a valid decode here
// also exercises the same guard on the read() path.
TEST(ImageTests, AllocateAndTransformValidDimensions)
{
    Image src;
    ASSERT_TRUE(src.allocate(4, 4, 4));
    EXPECT_EQ(src.pixels.size(), 4u * 4u * 4u);
    src.set(0.1f, 0.2f, 0.3f, 0.4f);

    Image dst;
    ASSERT_TRUE(dst.allocate(4, 4, 4));
    EXPECT_TRUE(dst.copyChannel(src, 0, 0));
    EXPECT_FLOAT_EQ(dst.pixels[0], 0.1f);

    ImageAsset asset;
    asset.format = ImageFormatPng;
    ASSERT_TRUE(src.write(asset));
    Image decoded;
    ASSERT_TRUE(decoded.read(asset));
    EXPECT_EQ(decoded.width, 4);
    EXPECT_EQ(decoded.height, 4);
    EXPECT_EQ(decoded.channels, 4);
}

// A rejected allocate() must leave dimensions/pixels in a consistent empty state, so that
// downstream calls which don't check the bool return (transformChannel/set) stay no-ops instead
// of indexing into an empty buffer using stale, oversized dimensions.
TEST(ImageTests, RejectedAllocateStaysSafeForDownstreamOps)
{
    Image src;
    ASSERT_TRUE(src.allocate(4, 4, 4));
    src.set(1.0f, 1.0f, 1.0f, 1.0f);

    Image dst;
    EXPECT_FALSE(dst.allocate(65536, 65536, 4));

    EXPECT_FALSE(dst.transformChannel(src, 0, 1.0f, 0.0f, 0));
    dst.set(1.0f, 1.0f, 1.0f, 1.0f); // must not crash despite the earlier oversized request
}
