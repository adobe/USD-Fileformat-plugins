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
#include "api.h"
#include <pxr/base/gf/matrix4d.h>
#include <pxr/pxr.h>
#include <pxr/usd/usd/common.h>

namespace adobe::usd {

/// \ingroup utils_nodes
/// Gets a transform to convert to a system with 1 meters per unit and +y up axis.
USDFFUTILS_API PXR_NS::GfMatrix4d
getTransformToMetersPositiveY(double metersPerUnit, const PXR_NS::TfToken& upAxis);

/// \ingroup utils_nodes
/// Gets a transform to convert to a system with 1 meters per unit and +z up axis.
USDFFUTILS_API PXR_NS::GfMatrix4d
getTransformToMetersPositiveZ(double metersPerUnit, const PXR_NS::TfToken& upAxis);

}