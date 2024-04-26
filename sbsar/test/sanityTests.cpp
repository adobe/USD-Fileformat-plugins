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
#include <gtest/gtest.h>
#include <iterator>
#include <pxr/imaging/hio/image.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/inMemoryAsset.h>
#include <pxr/usd/ar/filesystemAsset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <assetResolver/sbsarImage.h>

const std::string tmpDir = "./tmp";

const std::string assetPath = 
  "../../../test/assets/sbsar/CardBoard.sbsar[graphs/CardBoard/"
  "images?usage=baseColor#packageHash=b427747e86441362#params={\"$outputsize\":[4,4],"
  "\"$randomseed\":0,\"cardboard_color\":[0.58890700340271,0."
  "46410301327705383,0.3237049877643585],\"tearing\":0.7099999785423279}.sbsarimage]";

const std::string rdAssetPath = 
  "../../../test/assets/sbsar/CardBoard.sbsar[graphs/CardBoard/"
  "images?usage=baseColor#packageHash=b427747e86441362#params={\"$outputsize\":[4,4],"
  "\"$randomseed\":42,\"cardboard_color\":[0.58890700340271,0."
  "46410301327705383,0.3237049877643585],\"tearing\":0.7099999785423279}.sbsarimage]";

const std::string thumbnailPath = "../../../test/assets/sbsar/natural_lambskin_leather.sbsar";

void
cleanTempFiles()
{
    std::filesystem::remove_all(tmpDir);
}

std::string
saveAssetToTempFile(const PXR_NS::ArAsset& asset)
{
    // Create a tmp directory
    std::filesystem::create_directories(tmpDir);

    // Open and save the file
    std::string tmpFile = tmpDir + "/test.sbsarimage";
    std::ofstream file(tmpFile, std::ios::binary);
    EXPECT_TRUE(file.is_open());
    auto buffer = asset.GetBuffer();
    file.write(buffer.get(), asset.GetSize());
    EXPECT_NE(asset.GetSize(), 0);
    file.close();
    return tmpFile;
}

TEST(SbsarSanityTests, HasSBSARFormat)
{
    PXR_NAMESPACE_USING_DIRECTIVE
    std::set<std::string> allFileFormats = SdfFileFormat::FindAllFileFormatExtensions();
    std::stringstream format_dump;
    std::copy(allFileFormats.begin(),
              allFileFormats.end(),
              std::ostream_iterator<std::string>(format_dump, ", "));
    ASSERT_TRUE(allFileFormats.find("sbsar") != allFileFormats.end())
      << "File formats: " << format_dump.str();
    ASSERT_TRUE(true);
}

std::vector<char>
readImage(const std::string& assetPath)
{
    auto img = PXR_NS::HioImage::OpenForReading(assetPath);
    int imgSize = img->GetWidth() * img->GetHeight() * img->GetBytesPerPixel();
    std::vector<char> imgData(imgSize);
    EXPECT_TRUE(img);
    PXR_NS::HioImage::StorageSpec spec;
    spec.height = img->GetHeight();
    spec.width = img->GetWidth();
    spec.format = img->GetFormat();
    spec.flipped = false;
    spec.data = imgData.data();
    img->Read(spec);
    return imgData;
}

TEST(SbsarSanityTests, fromSbsar)
{
    auto img = PXR_NS::HioImage::OpenForReading(assetPath);
    ASSERT_TRUE(img);
    ASSERT_EQ(img->GetWidth(), 16);
    ASSERT_EQ(img->GetHeight(), 16);
    ASSERT_EQ(img->GetFormat(), PXR_NS::HioFormatUNorm8Vec4srgb);
    ASSERT_EQ(img->GetBytesPerPixel(), 4);
}

TEST(SbsarSanityTests, fromFile)
{
    auto asset = PXR_NS::ArGetResolver().OpenAsset(PXR_NS::ArResolvedPath(assetPath));
    ASSERT_TRUE(asset);

    // Save asset to a temporary file
    std::string tmpFile = saveAssetToTempFile(*asset);
    std::vector<char> imgData = readImage(tmpFile);

    auto buffer = asset->GetBuffer();
    auto bufferData = buffer.get() + sizeof(SbsarImage::ImageHeader);
    for (int i = 0; i < imgData.size(); ++i) {
        ASSERT_EQ(imgData[i], bufferData[i]);
    }

    cleanTempFiles();
}

TEST(SbsarSanityTests, randomSeed)
{
    std::vector<char> imgData1 = readImage(assetPath);
    std::vector<char> imgData2 = readImage(rdAssetPath);
    ASSERT_NE(imgData1, imgData2);
}

TEST(SbsarSanityTests, thumbnail)
{
    {
        auto assetPath =
          PXR_NS::ArResolvedPath(thumbnailPath + "[thumbnails/natural_lambskin_leather.png]");
        auto asset = PXR_NS::ArGetResolver().OpenAsset(PXR_NS::ArResolvedPath(assetPath));
        ASSERT_TRUE(asset);
        ASSERT_FALSE(std::dynamic_pointer_cast<PXR_NS::ArFilesystemAsset>(asset));
        ASSERT_TRUE(std::dynamic_pointer_cast<PXR_NS::ArInMemoryAsset>(asset));
        auto img = PXR_NS::HioImage::OpenForReading(assetPath);
        ASSERT_TRUE(img);
        EXPECT_EQ(img->GetHeight(), 512);
        EXPECT_EQ(img->GetWidth(), 512);
    }

    {
        auto assetPath = PXR_NS::ArResolvedPath(thumbnailPath + "[thumbnail.png]");
        auto asset = PXR_NS::ArGetResolver().OpenAsset(PXR_NS::ArResolvedPath(assetPath));
        ASSERT_TRUE(asset);
        ASSERT_FALSE(std::dynamic_pointer_cast<PXR_NS::ArFilesystemAsset>(asset));
        ASSERT_TRUE(std::dynamic_pointer_cast<PXR_NS::ArInMemoryAsset>(asset));
        auto img = PXR_NS::HioImage::OpenForReading(assetPath);
        ASSERT_TRUE(img);
        EXPECT_EQ(img->GetHeight(), 512);
        EXPECT_EQ(img->GetWidth(), 512);
    }

    {
        // invalid thumbnail path
        auto assetPath = PXR_NS::ArResolvedPath(thumbnailPath + "[thumbnails.png]");
        auto asset = PXR_NS::ArGetResolver().OpenAsset(PXR_NS::ArResolvedPath(assetPath));
        ASSERT_FALSE(asset);
    }

    {
        // sbsar file with no thumbnail
        auto assetPath = PXR_NS::ArResolvedPath(
          std::string("./CardBoard.sbsar") + "[thumbnail.png]");
        auto asset = PXR_NS::ArGetResolver().OpenAsset(PXR_NS::ArResolvedPath(assetPath));
        ASSERT_FALSE(asset);
    }
}