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
#include <fileformatutils/transforms.h>
#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>
#include <pxr/usd/usdGeom/tokens.h>

using namespace PXR_NS;

namespace adobe::usd {

GfMatrix4d
getTransformToMetersPositiveY(double metersPerUnit, const TfToken& upAxis)
{
    GfMatrix4d transform(1);
    if (upAxis == UsdGeomTokens->z) {
        transform.SetRotate(GfQuatd(0.7071068, -0.7071068, 0, 0)); // rotate -90 deg in x
        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "system transform rotation { rotX: -90deg }\n");
    }
    if (metersPerUnit != 1 && metersPerUnit > 0) {
        transform *= GfMatrix4d(metersPerUnit);
        TF_DEBUG_MSG(
          FILE_FORMAT_UTIL, "system transform scale { metersPerUnit: %f }\n", metersPerUnit);
    }
    return transform;
}

GfMatrix4d
getTransformToMetersPositiveZ(double metersPerUnit, const TfToken& upAxis)
{
    GfMatrix4d transform(1);
    if (upAxis == UsdGeomTokens->y) {
        transform.SetRotate(GfQuatd(-0.7071068, -0.7071068, 0, 0)); // rotate 90 deg in x
        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "system transform rotation { rotX: 90deg }\n");
    }
    if (metersPerUnit != 1 && metersPerUnit > 0) {
        transform *= GfMatrix4d(metersPerUnit);
        TF_DEBUG_MSG(
          FILE_FORMAT_UTIL, "system transform scale { metersPerUnit: %f }\n", metersPerUnit);
    }
    return transform;
}

}