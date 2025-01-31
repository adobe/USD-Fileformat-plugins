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
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fileformatutils/gsplatHelper.h>
#include <sh/spherical_harmonics.h>

using namespace PXR_NS;

namespace adobe::usd {
size_t
numSHDegreesFromGsplat(size_t numGsplatCoefficients)
{
    // Gsplat has R, G, B channels for each SH band, so we need to divide by 3.
    const size_t numNonZeroSHBands = numGsplatCoefficients / 3;
    return static_cast<std::size_t>(
      std::floor(std::sqrt(static_cast<float>(numNonZeroSHBands) + 1.0f)) - 1.0f);
}

size_t
numNonZeroSHBandsFromDegree(size_t numDegrees)
{
    // This is the number of bands without the zeroth order.
    return numDegrees * (numDegrees + 2);
}

void
rotatePointRotations(const Primvar<GfQuatf>& pointRotations,
                     const GfQuatf& rotation,
                     size_t numPoints,
                     VtQuatfArray& outPointRotations)
{
    size_t rotationsOffset = outPointRotations.size();
    outPointRotations.resize(rotationsOffset + numPoints, GfQuatf::GetIdentity());

    // We need to find the minimum number of points that have rotations. This acts as a safe guard
    // in case some input data is missing.
    const size_t numPointsWithRotations = std::min(numPoints, pointRotations.values.size());

    std::transform(pointRotations.values.begin(),
                   pointRotations.values.begin() + numPointsWithRotations,
                   outPointRotations.begin() + rotationsOffset,
                   [&rotation](const GfQuatf& quat) { return rotation * quat; });
}

void
rotatePointSphericalHarmonics(const std::vector<Primvar<float>>& inSH,
                              const PXR_NS::GfQuatf& rotation,
                              size_t numPoints,
                              std::vector<VtFloatArray>& outSH)
{
    // We need to find the minimum number of points that have SH coefficients in all channels.
    // This acts as a safe guard in case some input data is missing.
    size_t numPointsCompleteSH = numPoints;
    for (size_t shIndex = 0; shIndex < inSH.size(); shIndex++) {
        numPointsCompleteSH = std::min(numPointsCompleteSH, inSH[shIndex].values.size());
    }

    if (1.0f - std::abs(rotation.GetReal()) > 1e-6f) {
        // The rotation is not an identity, so we need to rotate the SH coefficients.
        std::vector<size_t> shDataOffset(outSH.size());

        // If the input SH has less channels than the output, we need to fill these output SH
        // with zeros.
        for (size_t shIndex = 0; shIndex < outSH.size(); shIndex++) {
            size_t shCoeffOffset = outSH[shIndex].size();
            shDataOffset[shIndex] = shCoeffOffset;
            outSH[shIndex].resize(shCoeffOffset + numPoints, 0.0f);
        }

        const size_t pointSHDegrees = numSHDegreesFromGsplat(std::min(inSH.size(), outSH.size()));
        if (pointSHDegrees > 0) {
            const size_t numSHCoeffsPerChannel = numNonZeroSHBandsFromDegree(pointSHDegrees);
            Eigen::Quaterniond quatRot(rotation.GetReal(),
                                       rotation.GetImaginary()[0],
                                       rotation.GetImaginary()[1],
                                       rotation.GetImaginary()[2]);

            // Need to renormalize due to floating precision differences.
            quatRot.normalize();
            const auto shRot = sh::Rotation::Create(static_cast<int>(pointSHDegrees), quatRot);

            std::vector<float> shCoeffsBuffer(numSHCoeffsPerChannel + 1);
            // We do not need to rotate the zeroth-order coefficient, while the
            // SphericalHarmonics library needs coefficients from the zeroth to the highest
            // order. Thus we just put a zero there.
            shCoeffsBuffer[0] = 0.0f;

            for (size_t i = 0; i < numPointsCompleteSH; ++i) {
                for (size_t iChannel = 0; iChannel < 3; ++iChannel) {
                    for (size_t iCoeff = 0; iCoeff < numSHCoeffsPerChannel; ++iCoeff) {
                        shCoeffsBuffer[iCoeff + 1] =
                          inSH[iCoeff + iChannel * numSHCoeffsPerChannel].values[i];
                    }
                    shRot->Apply(shCoeffsBuffer, &shCoeffsBuffer);
                    for (size_t iCoeff = 0; iCoeff < numSHCoeffsPerChannel; ++iCoeff) {
                        const size_t coeffIndex = iCoeff + iChannel * numSHCoeffsPerChannel;
                        const size_t shOffset = shDataOffset[coeffIndex];
                        outSH[coeffIndex][shOffset + i] = shCoeffsBuffer[iCoeff + 1];
                    }
                }
            }
        }
    } else {
        // The rotation is an identity, so we can skip the rotation and just copy the SH
        // coefficients.
        for (size_t shIndex = 0; shIndex < outSH.size(); shIndex++) {
            size_t shCoeffOffset = outSH[shIndex].size();
            outSH[shIndex].resize(shCoeffOffset + numPoints, 0.0f);
            if (shIndex < inSH.size()) {
                memcpy(outSH[shIndex].data() + shCoeffOffset,
                       inSH[shIndex].values.data(),
                       numPointsCompleteSH * sizeof(inSH[shIndex].values[0]));
            }
        }
    }
}

void
scalePointWidths(const VtFloatArray& inWidths,
                 const std::vector<Primvar<float>>& inExtraWidths,
                 size_t numPoints,
                 float widthScale,
                 VtFloatArray& outWidths,
                 VtFloatArray& outWidths1,
                 VtFloatArray& outWidths2)
{
    size_t widthsOffset = outWidths.size();
    size_t widths1Offset = outWidths1.size();
    size_t widths2Offset = outWidths2.size();

    // We need to use the number of points as the new size (and fill with default values)
    // in case there's a mix of regular point cloud and Gsplats.
    outWidths.resize(widthsOffset + numPoints, 0.0f);
    outWidths1.resize(widths1Offset + numPoints, 0.0f);
    outWidths2.resize(widths2Offset + numPoints, 0.0f);

    const size_t numPointWidths = std::min(numPoints, inWidths.size());
    std::transform(inWidths.begin(),
                   inWidths.begin() + numPointWidths,
                   outWidths.begin() + widthsOffset,
                   [widthScale](float width) { return width * widthScale; });
    if (inExtraWidths.size() >= 2) {
        const size_t numPointWidths1 = std::min(numPoints, inExtraWidths[0].values.size());
        const size_t numPointWidths2 = std::min(numPoints, inExtraWidths[1].values.size());
        std::transform(inExtraWidths[0].values.begin(),
                       inExtraWidths[0].values.begin() + numPointWidths1,
                       outWidths1.begin() + widths1Offset,
                       [widthScale](float width) { return width * widthScale; });
        std::transform(inExtraWidths[1].values.begin(),
                       inExtraWidths[1].values.begin() + numPointWidths2,
                       outWidths2.begin() + widths2Offset,
                       [widthScale](float width) { return width * widthScale; });
    }
}
}
