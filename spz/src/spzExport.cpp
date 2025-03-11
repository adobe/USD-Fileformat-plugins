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
#include "spzExport.h"
#include "debugCodes.h"
#include <fileformatutils/common.h>
#include <fileformatutils/geometry.h>
#include <fileformatutils/gsplatHelper.h>
#include <fileformatutils/images.h>
#include <fileformatutils/transforms.h>
#include <numeric>
#include <pxr/base/tf/token.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/pcp/cache.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/typed.h>
#include <pxr/usd/usd/zipFile.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/tokens.h>

using namespace PXR_NS;

namespace adobe::usd {

struct SpzTotalMesh
{
    VtVec3fArray points;
    VtVec3fArray color;
    VtFloatArray opacity;

    VtFloatArray widths;
    VtFloatArray widths1;
    VtFloatArray widths2;
    VtQuatfArray rotations;
    std::vector<VtFloatArray> shCoeffs;
};

std::size_t
findMaxSHCoeffSize(const UsdData& usd, int nodeIndex)
{
    std::size_t maxSHCoeffSize = 0;
    const Node& node = usd.nodes[nodeIndex];
    for (int meshIndex : node.staticMeshes) {
        const Mesh& mesh = usd.meshes[meshIndex];
        if (!mesh.asGsplats)
            continue;
        maxSHCoeffSize = std::max(maxSHCoeffSize, mesh.pointSHCoeffs.size());
    }
    for (size_t i = 0; i < node.children.size(); ++i) {
        maxSHCoeffSize = std::max(maxSHCoeffSize, findMaxSHCoeffSize(usd, node.children[i]));
    }
    return maxSHCoeffSize;
}

void
aggregateMeshInstance(SpzTotalMesh& totalMesh,
                      const Mesh& mesh,
                      const GfMatrix4d& modelMatrix)
{
    size_t currentMeshPointsSize = mesh.points.size();
    size_t offset = totalMesh.points.size();
    totalMesh.points.resize(offset + currentMeshPointsSize);
    totalMesh.opacity.resize(offset + mesh.points.size(), 1.0f);
    totalMesh.color.resize(offset + mesh.points.size(), GfVec3f(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i < currentMeshPointsSize; ++i) {
        totalMesh.points[offset + i] = GfVec3f(modelMatrix.Transform(mesh.points[i]));
    }

    const size_t numPointOpacities = std::min(currentMeshPointsSize, mesh.opacities[0].values.size());
    memcpy(totalMesh.opacity.data() + offset,
           mesh.opacities[0].values.data(),
           numPointOpacities * sizeof(mesh.opacities[0].values[0]));

    const size_t numPointColors = std::min(currentMeshPointsSize, mesh.colors[0].values.size());
    memcpy(totalMesh.color.data() + offset,
           mesh.colors[0].values.data(),
           numPointColors * sizeof(mesh.colors[0].values[0]));

    GfMatrix4f modelMatrixFloat(modelMatrix);
    const float modelScaling = std::cbrt(std::abs(modelMatrixFloat.GetDeterminant()));
    GfQuatf modelRotation = modelMatrixFloat.ExtractRotationQuat().GetNormalized();

    scalePointWidths(mesh.pointWidths,
                     mesh.pointExtraWidths,
                     currentMeshPointsSize,
                     modelScaling,
                     totalMesh.widths,
                     totalMesh.widths1,
                     totalMesh.widths2);
    rotatePointRotations(
      mesh.pointRotations, modelRotation, currentMeshPointsSize, totalMesh.rotations);
    rotatePointSphericalHarmonics(
      mesh.pointSHCoeffs, modelRotation, currentMeshPointsSize, totalMesh.shCoeffs);

    TF_DEBUG_MSG(FILE_FORMAT_SPZ,
                 "spz::export aggregated mesh %s { v: %lu }\n",
                 mesh.name.c_str(),
                 currentMeshPointsSize);
}

void
traverseNodesAndAggregateMeshes(const UsdData& usd,
                                SpzTotalMesh& totalMesh,
                                const GfMatrix4d& correctionTransform,
                                int nodeIndex)
{
    const Node& node = usd.nodes[nodeIndex];
    GfMatrix4d modelMatrix = node.worldTransform * correctionTransform;
    
    for (int meshIndex : node.staticMeshes) {
        const Mesh& mesh = usd.meshes[meshIndex];
        if (!mesh.asGsplats)
            continue;
        aggregateMeshInstance(totalMesh, mesh, modelMatrix);
    }
    
    for (size_t i = 0; i < node.children.size(); ++i) {
        traverseNodesAndAggregateMeshes(
          usd, totalMesh, correctionTransform, node.children[i]);
    }
}

float
encodeGsplatOpacity(float opacity)
{
    // Make sure the inversed sigmoid function doesn't cause
    // infinite result.
    const float clampedOpacity = std::clamp(
      opacity, std::numeric_limits<float>::min(), 1.0f - std::numeric_limits<float>::epsilon());
    return -log(1.0f / clampedOpacity - 1.0f);
}

float
encodeGsplatWidth(float width)
{
    // Make sure the log function doesn't cause
    // infinite result.
    const float clamped_half_width = std::max(std::numeric_limits<float>::min(), width * 0.5f);
    return log(clamped_half_width);
}

bool
exportSpz(const UsdData& usd, spz::GaussianCloud& gaussianCloud)
{
    if (usd.meshes.size() <= 0) {
        TF_DEBUG_MSG(FILE_FORMAT_SPZ,
                     "spz::export no instances of UsdGeomMesh, nothing will be exported\n");
        return true;
    }

    // Because Spz does not support multiple individual meshes, we need to aggregate all meshes into
    // a single mesh and apply their local to world transforms, together with the system's
    // correction transform.
    SpzTotalMesh totalMesh;
    GfMatrix4d correctionTransform = getTransformToMetersPositiveY(usd.metersPerUnit, usd.upAxis);

    std::size_t numGsplatsSHCoeffs = 0;
    for (size_t i = 0; i < usd.rootNodes.size(); ++i) {
        numGsplatsSHCoeffs = std::max(numGsplatsSHCoeffs, findMaxSHCoeffSize(usd, usd.rootNodes[i]));
    }

    // We only store SH coefficients up to the degree with complete bands (i.e., 0, 9, 24, or 45
    // coefficients).
    const std::size_t numSHDegrees = numSHDegreesFromGsplat(numGsplatsSHCoeffs);
    const std::size_t numNonZeroSHBands = numNonZeroSHBandsFromDegree(numSHDegrees);
    numGsplatsSHCoeffs = numNonZeroSHBands * 3;

    totalMesh.shCoeffs.resize(numGsplatsSHCoeffs);

    for (size_t i = 0; i < usd.rootNodes.size(); ++i) {
        traverseNodesAndAggregateMeshes(
          usd, totalMesh, correctionTransform, usd.rootNodes[i]);
    }

    gaussianCloud.numPoints = totalMesh.points.size();
    gaussianCloud.shDegree = numSHDegrees;
    gaussianCloud.positions.resize(totalMesh.points.size() * 3);
    memcpy(gaussianCloud.positions.data(),
           totalMesh.points.data(),
           totalMesh.points.size() * sizeof(totalMesh.points[0]));

    // Zeroth coefficient of SH, inversed as 2sqrt(pi)
    constexpr float invShC0 = 3.5449077018f;
    gaussianCloud.colors.resize(totalMesh.color.size() * 3);
    for (size_t i = 0; i < totalMesh.color.size(); ++i) {
        gaussianCloud.colors[i * 3 + 0] = (totalMesh.color[i][0] - 0.5f) * invShC0;
        gaussianCloud.colors[i * 3 + 1] = (totalMesh.color[i][1] - 0.5f) * invShC0;
        gaussianCloud.colors[i * 3 + 2] = (totalMesh.color[i][2] - 0.5f) * invShC0;
    }

    gaussianCloud.alphas.resize(totalMesh.opacity.size());
    for (size_t i = 0; i < totalMesh.opacity.size(); ++i) {
        gaussianCloud.alphas[i] = encodeGsplatOpacity(totalMesh.opacity[i]);
    }

    gaussianCloud.scales.resize(totalMesh.widths.size() * 3);
    for (size_t i = 0; i < totalMesh.widths.size(); ++i) {
        gaussianCloud.scales[i * 3 + 0] = encodeGsplatWidth(totalMesh.widths[i]);
        gaussianCloud.scales[i * 3 + 1] = encodeGsplatWidth(totalMesh.widths1[i]);
        gaussianCloud.scales[i * 3 + 2] = encodeGsplatWidth(totalMesh.widths2[i]);
    }

    gaussianCloud.rotations.resize(totalMesh.rotations.size() * 4);
    memcpy(gaussianCloud.rotations.data(),
           totalMesh.rotations.data(),
           totalMesh.rotations.size() * sizeof(totalMesh.rotations[0]));

    gaussianCloud.sh.resize(numGsplatsSHCoeffs * totalMesh.points.size());
    // SPZ stores SH coefficients in row-major order, different than USD's column-major order.
    for (size_t shRowIndex = 0; shRowIndex < numNonZeroSHBands; ++shRowIndex) {
        for (size_t shColIndex = 0; shColIndex < 3; ++shColIndex) {
            const std::size_t spzSHIndex = shRowIndex * 3 + shColIndex;
            const std::size_t usdSHIndex = shColIndex * numNonZeroSHBands + shRowIndex;
            const std::size_t spzShCoeffOffset = spzSHIndex * totalMesh.points.size();

            for (size_t i = 0; i < totalMesh.points.size(); ++i) {
                gaussianCloud.sh[spzShCoeffOffset + i] = totalMesh.shCoeffs[usdSHIndex][i];
            }
        }
    }

    return true;
}

}
