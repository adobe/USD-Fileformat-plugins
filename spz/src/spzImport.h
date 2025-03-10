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
#pragma once
#include <splat-types.h>
#include <fileformatutils/usdData.h>

namespace adobe::usd {

struct ImportSpzOptions
{
    bool importGsplatWithZup = false;
    PXR_NS::VtFloatArray importGsplatClippingBox = { -2.0, -2.0, -2.0, 2.0, 2.0, 2.0 };
};

/// \ingroup usdspz
/// \brief Import spz data into a USD data cache.
bool
importSpz(const ImportSpzOptions& options, const spz::GaussianCloud& spz, UsdData& data);

}