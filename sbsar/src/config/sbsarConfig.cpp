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
#include <config/sbsarConfig.h>
#include <config/sbsarConfigFactory.h>

#include <pxr/base/plug/registry.h>
#include <sbsarEngine/sbsarRenderThread.h>
#include <sbsarfileformat.h>

#include <optional>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    TfType t = TfType::Define<SbsarConfig>();
    t.SetFactory<SbsarConfigFactory>();
}

static TfStaticData<SbsarConfigRegistry> _SbsarConfigRegistry;

template<typename T>
std::optional<T>
getConfigValue(const PXR_NS::PlugRegistry& reg,
               const PXR_NS::TfType& sbsarConfig,
               const std::string& key)
{
    PXR_NS::JsValue value = reg.GetDataFromPluginMetaData(sbsarConfig, key);
    if (!value.Is<T>()) {
        TF_WARN("SbsarConfig: %s is not a valid value in SbsarConfig in plugInfo.json",
                key.c_str());
        return {};
    }
    return value.Get<T>();
}

SbsarConfig::SbsarConfig()
{
    init();
    PXR_NS::PlugRegistry& reg = PXR_NS::PlugRegistry::GetInstance();
    PXR_NS::TfType sbsarFileFormat = PXR_NS::TfType::Find<PXR_NS::SbsarConfig>();
    if (std::optional<std::uint64_t> size =
          getConfigValue<std::uint64_t>(reg, sbsarFileFormat, "assetCacheSize"))
        setAssetCacheSize(*size);
    if (std::optional<std::uint64_t> size =
          getConfigValue<std::uint64_t>(reg, sbsarFileFormat, "inputImageCacheSize"))
        setInputImageCacheSize(*size);
    if (std::optional<std::uint64_t> size =
          getConfigValue<std::uint64_t>(reg, sbsarFileFormat, "packageCacheSize"))
        setPackageCacheSize(*size);
}

SbsarConfig::~SbsarConfig() = default;

void
SbsarConfig::init()
{
    m_assetCacheSize = 1'000'000'000;
    m_inputImageCacheSize = 1'000'000'000;
    m_packageCacheSize = 10;
}

void
SbsarConfig::setAssetCacheSize(std::size_t size)
{
    if (size == 0) {
        TF_WARN("SbsarConfig: Asset cache size cannot be 0");
        return;
    }
    m_assetCacheSize = size;
}

void
SbsarConfig::setInputImageCacheSize(std::size_t size)
{
    if (size == 0) {
        TF_WARN("SbsarConfig: Input image cache size cannot be 0");
        return;
    }
    m_inputImageCacheSize = size;
}

void
SbsarConfig::setPackageCacheSize(std::size_t size)
{
    if (size == 0) {
        TF_WARN("SbsarConfig: Package cache size cannot be 0");
        return;
    }
    m_packageCacheSize = size;
}

std::size_t
SbsarConfig::getAssetCacheSize() const
{
    return m_assetCacheSize;
}

std::size_t
SbsarConfig::getInputImageCacheSize() const
{
    return m_inputImageCacheSize;
}

std::size_t
SbsarConfig::getPackageCacheSize() const
{
    return m_packageCacheSize;
}

SbsarConfigRefPtr
getSbsarConfig()
{
    return _SbsarConfigRegistry->getSbsarConfig();
}

PXR_NAMESPACE_CLOSE_SCOPE
