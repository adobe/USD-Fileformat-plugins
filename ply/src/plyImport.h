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
#pragma once
#include <happly.h>
#include <fileformatutils/usdData.h>

namespace adobe::usd {

struct ImportPlyOptions
{
    bool importAsPoints = false;
    bool importWithUpAxisCorrection = true;
    PXR_NS::VtFloatArray importGsplatClippingBox = { -2, -2, -2, 2, 2, 2 };
    float pointWidth = 0.01f;
};

/// \ingroup usdply
/// \brief Import ply data into a USD data cache.
bool
importPly(const ImportPlyOptions& options, happly::PLYData& ply, UsdData& data);

}