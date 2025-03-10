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
#include "spzImport.h"
#include "debugCodes.h"
#include <algorithm>
#include <array>
#include <fileformatutils/common.h>
#include <fileformatutils/geometry.h>
#include <fileformatutils/images.h>
#include <fileformatutils/neuralAssetsHelper.h>
#include <limits>
#include <pxr/base/gf/range3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/tokens.h>
#include <regex>
#include <string>

using namespace PXR_NS;
using namespace spz;

namespace adobe::usd {
bool
importSpz(const ImportSpzOptions& options, const spz::GaussianCloud& gaussianCloud, UsdData& usd)
{
    auto [meshIndex, mesh] = usd.addMesh();

    // SPZ always stores Gsplat only.
    mesh.asPoints = true;
    mesh.asGsplats = true;

    try {
        mesh.points.resize(gaussianCloud.numPoints);
        if (gaussianCloud.positions.size() < static_cast<size_t>(gaussianCloud.numPoints * 3))
            throw std::runtime_error("Invalid position data size");
        for (size_t i = 0; i < mesh.points.size(); i++) {
            mesh.points[i][0] = gaussianCloud.positions[i * 3 + 0];
            mesh.points[i][1] = gaussianCloud.positions[i * 3 + 1];
            mesh.points[i][2] = gaussianCloud.positions[i * 3 + 2];
        }

        // The 0th-order coefficient of spherical harmonics, which
        // is 1/sqrt(4*pi).
        constexpr float shC0 = 0.28209479177387814f;
        auto [colorIndex, colors] = usd.addColorSet(meshIndex);
        colors.interpolation = UsdGeomTokens->vertex;
        colors.values.resize(gaussianCloud.numPoints);
        if (gaussianCloud.colors.size() < static_cast<size_t>(gaussianCloud.numPoints * 3))
            throw std::runtime_error("Invalid color data size");
        for (size_t i = 0; i < colors.values.size(); i++) {
            colors.values[i][0] =
              std::clamp(gaussianCloud.colors[i * 3 + 0] * shC0 + 0.5f, 0.0f, 1.0f);
            colors.values[i][1] =
              std::clamp(gaussianCloud.colors[i * 3 + 1] * shC0 + 0.5f, 0.0f, 1.0f);
            colors.values[i][2] =
              std::clamp(gaussianCloud.colors[i * 3 + 2] * shC0 + 0.5f, 0.0f, 1.0f);
        }

        auto [opacityIndex, opacity] = usd.addOpacitySet(meshIndex);
        opacity.interpolation = UsdGeomTokens->vertex;
        opacity.values.resize(gaussianCloud.numPoints);
        if (gaussianCloud.alphas.size() < static_cast<size_t>(gaussianCloud.numPoints))
            throw std::runtime_error("Invalid opacity data size");
        for (size_t i = 0; i < opacity.values.size(); i++) {
            opacity.values[i] = 1.0f / (1.0f + std::exp(-gaussianCloud.alphas[i]));
        }

        if (gaussianCloud.scales.size() < static_cast<size_t>(gaussianCloud.numPoints * 3))
            throw std::runtime_error("Invalid scale data size");
        mesh.pointWidths.resize(gaussianCloud.numPoints);
        for (size_t i = 0; i < mesh.pointWidths.size(); i++)
            mesh.pointWidths[i] = std::exp(gaussianCloud.scales[i * 3 + 0]) * 2.0f;

        auto [width1Index, widths1] = usd.addExtraPointWidthSet(meshIndex);
        widths1.interpolation = UsdGeomTokens->vertex;
        widths1.values.resize(gaussianCloud.numPoints);
        for (size_t i = 0; i < mesh.pointWidths.size(); i++)
            widths1.values[i] = std::exp(gaussianCloud.scales[i * 3 + 1]) * 2.0f;

        auto [width2Index, widths2] = usd.addExtraPointWidthSet(meshIndex);
        widths2.interpolation = UsdGeomTokens->vertex;
        widths2.values.resize(gaussianCloud.numPoints);
        for (size_t i = 0; i < mesh.pointWidths.size(); i++)
            widths2.values[i] = std::exp(gaussianCloud.scales[i * 3 + 2]) * 2.0f;

        mesh.pointRotations.interpolation = UsdGeomTokens->vertex;
        mesh.pointRotations.values.resize(gaussianCloud.numPoints);
        if (gaussianCloud.rotations.size() < static_cast<size_t>(gaussianCloud.numPoints * 4))
            throw std::runtime_error("Invalid rotation data size");
        for (size_t i = 0; i < mesh.pointRotations.values.size(); i++) {
            mesh.pointRotations.values[i].SetReal(gaussianCloud.rotations[i * 4 + 3]);
            mesh.pointRotations.values[i].SetImaginary(gaussianCloud.rotations[i * 4 + 0],
                                                       gaussianCloud.rotations[i * 4 + 1],
                                                       gaussianCloud.rotations[i * 4 + 2]);
            mesh.pointRotations.values[i] = mesh.pointRotations.values[i].GetNormalized();
        }

        const size_t shDim = gaussianCloud.shDegree * (gaussianCloud.shDegree + 2);
        if (gaussianCloud.sh.size() < static_cast<size_t>(gaussianCloud.numPoints * shDim * 3))
            throw std::runtime_error("Invalid SH coefficient data size");
        for (std::size_t shColIndex = 0; shColIndex < 3; ++shColIndex) {
            for (std::size_t shRowIndex = 0; shRowIndex < shDim; ++shRowIndex) {
                auto [shCoeffIndex, shCoeffs] = usd.addPointSHCoeffSet(meshIndex);
                shCoeffs.interpolation = UsdGeomTokens->vertex;
                shCoeffs.values.resize(gaussianCloud.numPoints);

                // SPZ stores SH coefficients in a row-major order, where
                // we need to convert it to a column-major order that we
                // use for USD.
                const size_t spzShIndex = shRowIndex * 3 + shColIndex;
                const size_t spzShBase = spzShIndex * gaussianCloud.numPoints;
                for (size_t i = 0; i < shCoeffs.values.size(); i++) {
                    shCoeffs.values[i] = gaussianCloud.sh[spzShBase + i];
                }
            }
        }
    } catch (std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_SPZ, "Cannot load SPZ: %s\n", e.what());
        return false;
    }

    auto [nodeIndex, node] = usd.addNode(-1);
    node.staticMeshes.push_back(meshIndex);

    usd.metersPerUnit = 1.0f;
    usd.upAxis = options.importGsplatWithZup ? UsdGeomTokens->z : UsdGeomTokens->y;

    if (options.importGsplatClippingBox.size() >= 6) {
        PXR_NS::GfVec3f minPos(std::numeric_limits<float>::max());
        PXR_NS::GfVec3f maxPos(-std::numeric_limits<float>::max());
        for (size_t i = 0; i < mesh.points.size(); i++) {
            minPos[0] = std::min(mesh.points[i][0], minPos[0]);
            minPos[1] = std::min(mesh.points[i][1], minPos[1]);
            minPos[2] = std::min(mesh.points[i][2], minPos[2]);
            maxPos[0] = std::max(mesh.points[i][0], maxPos[0]);
            maxPos[1] = std::max(mesh.points[i][1], maxPos[1]);
            maxPos[2] = std::max(mesh.points[i][2], maxPos[2]);
        }
        if (maxPos[0] < minPos[0] || maxPos[1] < minPos[1] || maxPos[2] < minPos[2]) {
            TF_DEBUG_MSG(FILE_FORMAT_SPZ,
                         "Invalid bounding box: (%f, %f, %f) - (%f, %f, %f)\n",
                         minPos[0],
                         minPos[1],
                         minPos[2],
                         maxPos[0],
                         maxPos[1],
                         maxPos[2]);
            return false;
        }

        // We apply a clipping box for Gsplat and limit its maximal size, to avoid rendering the low
        // quality splats far from the reconstruction center. This range will be part of the USD
        // asset and can be adjusted on-the-fly.
        mesh.clippingBox.values.resize(2);
        mesh.clippingBox.values[0] =
          PXR_NS::GfVec3f(std::max(options.importGsplatClippingBox[0], minPos[0]),
                          std::max(options.importGsplatClippingBox[1], minPos[1]),
                          std::max(options.importGsplatClippingBox[2], minPos[2]));
        mesh.clippingBox.values[1] =
          PXR_NS::GfVec3f(std::min(options.importGsplatClippingBox[3], maxPos[0]),
                          std::min(options.importGsplatClippingBox[4], maxPos[1]),
                          std::min(options.importGsplatClippingBox[5], maxPos[2]));
        mesh.clippingBox.interpolation = UsdGeomTokens->constant;
    }
    return true;
}

}