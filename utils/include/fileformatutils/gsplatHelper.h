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
#include "api.h"

#include "usdData.h"
#include <vector>

namespace adobe::usd {
USDFFUTILS_API size_t
numSHDegreesFromGsplat(size_t numCoefficients);

USDFFUTILS_API size_t
numNonZeroSHBandsFromDegree(size_t numDegrees);

USDFFUTILS_API void
rotatePointRotations(const Primvar<PXR_NS::GfQuatf>& pointRotations,
                     const PXR_NS::GfQuatf& rotation,
                     size_t numPoints,
                     PXR_NS::VtQuatfArray& outPointRotations);

USDFFUTILS_API void
rotatePointSphericalHarmonics(const std::vector<Primvar<float>>& inSH,
                              const PXR_NS::GfQuatf& rotation,
                              size_t numPoints,
                              std::vector<PXR_NS::VtFloatArray>& outSH);

USDFFUTILS_API void
scalePointWidths(const PXR_NS::VtFloatArray& inWidths,
                 const std::vector<Primvar<float>>& inExtraWidths,
                 size_t numPoints,
                 float widthScale,
                 PXR_NS::VtFloatArray& outWidths,
                 PXR_NS::VtFloatArray& outWidths1,
                 PXR_NS::VtFloatArray& outWidths2);
}
