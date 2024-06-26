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

#pragma once

#include <api.h>

#include "pxr/base/tf/declarePtrs.h"
#include <pxr/base/tf/refBase.h>
#include <pxr/base/tf/weakBase.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_WEAK_AND_REF_PTRS(SbsarConfig);

class USDSBSAR_API SbsarConfigRegistry
{
    SbsarConfigRegistry(const SbsarConfigRegistry&) = delete;
    SbsarConfigRegistry& operator=(const SbsarConfigRegistry&) = delete;

  public:
    SbsarConfigRegistry();
    SbsarConfigRefPtr getSbsarConfig();

  private:
    SbsarConfigRefPtr m_sbsarConfig;
};

PXR_NAMESPACE_CLOSE_SCOPE
