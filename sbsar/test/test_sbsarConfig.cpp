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

#include <config/sbsarConfig.h>
#include <sbsarEngine/sbsarRenderThread.h>

using namespace adobe::usd::sbsar;

class SbsarConfigFixure : public ::testing::Test
{

  protected:
    virtual void SetUp() {}
    virtual void TearDown() { PXR_NS::getSbsarConfig()->init(); }
};

TEST(SbsarConfig, getCacheSize)
{
    PXR_NS::SbsarConfigRefPtr sbsarConfig = PXR_NS::getSbsarConfig();
    ASSERT_TRUE(sbsarConfig);

    EXPECT_EQ(sbsarConfig->getAssetCacheSize(), 1'000'000'000);
    EXPECT_EQ(sbsarConfig->getInputImageCacheSize(), 1'000'000'000);
    EXPECT_EQ(sbsarConfig->getPackageCacheSize(), 10);
}

TEST_F(SbsarConfigFixure, setAssetCacheSize)
{
    PXR_NS::SbsarConfigRefPtr sbsarConfig = PXR_NS::getSbsarConfig();
    ASSERT_TRUE(sbsarConfig);
    sbsarConfig->setAssetCacheSize(1);
    EXPECT_EQ(sbsarConfig->getAssetCacheSize(), 1);
}

TEST_F(SbsarConfigFixure, setInputImageCacheSize)
{
    PXR_NS::SbsarConfigRefPtr sbsarConfig = PXR_NS::getSbsarConfig();
    ASSERT_TRUE(sbsarConfig);
    sbsarConfig->setInputImageCacheSize(1);
    EXPECT_EQ(sbsarConfig->getInputImageCacheSize(), 1);
}

TEST_F(SbsarConfigFixure, setPackageCacheSize)
{
    PXR_NS::SbsarConfigRefPtr sbsarConfig = PXR_NS::getSbsarConfig();
    ASSERT_TRUE(sbsarConfig);
    sbsarConfig->setPackageCacheSize(1);
    EXPECT_EQ(sbsarConfig->getPackageCacheSize(), 1);
}
