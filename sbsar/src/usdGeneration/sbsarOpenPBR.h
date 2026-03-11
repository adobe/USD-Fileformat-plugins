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

#include <pxr/pxr.h>
#include <pxr/usd/sdf/abstractData.h>

#include <substance/framework/framework.h>

#include "usdGenerationHelpers.h"

namespace adobe::usd::sbsar {

/// @brief Adds an OpenPBR/MaterialX material network to the material
///
/// The network is created from the provided Substance graph description and connected to the
/// material as the 'mtlx' surface.
///
/// @param sdfData      SDF data container to store the material in
/// @param materialPath Path of the parent material
/// @param graphDesc    Description of the current SBSAR graph
/// @return true if the material was successfully added, false otherwise
bool
addOpenPbrShader(PXR_NS::SdfAbstractData* sdfData,
                 const PXR_NS::SdfPath& materialPath,
                 const SubstanceAir::GraphDesc& graphDesc,
                 const NormalFormat& initialNormalFormat);

}
