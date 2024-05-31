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

#include <config/sbsarConfigRegistry.h>

#include <config/sbsarConfig.h>
#include <config/sbsarConfigFactory.h>

#include <pxr/base/tf/type.h>

#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/type.h>

PXR_NAMESPACE_OPEN_SCOPE

SbsarConfigRegistry::SbsarConfigRegistry()
  : m_sbsarConfig()
{
    TfType t = TfType::Find<SbsarConfig>();
    if (SbsarConfigFactory* factory = t.GetFactory<SbsarConfigFactory>()) {
        m_sbsarConfig = factory->New();
    }
}

SbsarConfigRefPtr
SbsarConfigRegistry::getSbsarConfig()
{
    return m_sbsarConfig;
}

PXR_NAMESPACE_CLOSE_SCOPE
