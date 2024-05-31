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

#include <pxr/base/tf/refPtr.h>
#include <pxr/base/tf/type.h>
#include <pxr/pxr.h>

#include <config/sbsarConfig.h>

PXR_NAMESPACE_OPEN_SCOPE

class USDSBSAR_API SbsarConfigFactory : public TfType::FactoryBase
{
  public:
    virtual ~SbsarConfigFactory();
    SbsarConfigRefPtr New() { return TfCreateRefPtr(new SbsarConfig); }
};

PXR_NAMESPACE_CLOSE_SCOPE
