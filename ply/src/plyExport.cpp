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
#include "plyExport.h"
#include "debugCodes.h"
#include <algorithm>
#include <cmath>
#include <fileformatutils/common.h>
#include <fileformatutils/geometry.h>
#include <fileformatutils/gsplatHelper.h>
#include <fileformatutils/images.h>
#include <fileformatutils/transforms.h>
#include <limits>
#include <numeric>
#include <pxr/usd/usdGeom/tokens.h>

using namespace PXR_NS;

namespace adobe::usd {

bool
meshesRequireExpansion(std::vector<Mesh>& meshes)
{
    for (const Mesh& m : meshes) {
        if (m.asPoints)
            continue;

        if (m.uvs.indices.size() || m.normals.indices.size() ||
            m.uvs.values.size() != m.points.size() || m.normals.values.size() != m.points.size() ||
            (m.colors.size() && m.colors[0].indices.size()) ||
            (m.opacities.size() && m.opacities[0].indices.size())) {
            return true;
        }
    }
    return false;
}

struct PlyTotalMesh
{
    std::vector<std::vector<int32_t>> indices;
    VtVec3fArray points;
    VtVec3fArray normals;
    VtVec2fArray uvs;
    VtVec3fArray color;
    VtFloatArray opacity;

    VtFloatArray widths;
    VtFloatArray widths1;
    VtFloatArray widths2;
    VtQuatfArray rotations;
    std::vector<VtFloatArray> shCoeffs;

    bool asGsplats = false;
};

void
aggregateMeshInstance(PlyTotalMesh& totalMesh,
                      const Mesh& mesh,
                      const GfMatrix4d& modelMatrix,
                      const GfMatrix4d& normalMatrix,
                      bool shouldExpand,
                      bool subMeshHasNormals,
                      bool subMeshHasUvs,
                      bool subMeshHasColor,
                      bool subMeshHasOpacity)
{
    size_t currentMeshPointsSize = mesh.points.size();

    size_t indicesOffset = totalMesh.indices.size();
    size_t pointsOffset = totalMesh.points.size();
    size_t normalsOffset = totalMesh.normals.size();
    size_t uvsOffset = totalMesh.uvs.size();
    size_t colorOffset = totalMesh.color.size();
    size_t opacityOffset = totalMesh.opacity.size();

    totalMesh.indices.resize(indicesOffset + mesh.faces.size());
    totalMesh.points.resize(pointsOffset + currentMeshPointsSize);

    if (subMeshHasOpacity) {
        totalMesh.opacity.resize(opacityOffset + currentMeshPointsSize);
        if (mesh.opacities.size()) {
            // need to check if the information is per vertex or per face
            if (mesh.opacities[0].values.size() == currentMeshPointsSize) {
                for (size_t i = 0; i < currentMeshPointsSize; i++) {
                    totalMesh.opacity[opacityOffset + i] = mesh.opacities[0].values[i];
                }
            } else if (mesh.opacities[0].values.size() == mesh.faces.size()) {
                // in a case which we have colors or opacity per face, we need to add per vertex
                // values since ply format needs per vertex color and opacity
                for (size_t i = 0, k = 0; i < mesh.faces.size(); i++) {
                    const float opacityValue = mesh.opacities[0].values[i];
                    for (int j = 0; j < mesh.faces[i]; j++) {
                        totalMesh.opacity[mesh.indices[k + j]] = opacityValue;
                    }
                    k += mesh.faces[i];
                }
            } else {
                TF_WARN("Mesh has opacity property which is not per vertex nor per face.");
            }
        } else {
            std::fill(totalMesh.opacity.begin() + opacityOffset, totalMesh.opacity.end(), 1.0f);
        }
    }

    if (subMeshHasColor) {
        totalMesh.color.resize(colorOffset + currentMeshPointsSize);
        if (mesh.colors.size()) {
            // need to check if the information is per vertex or per face
            if (mesh.colors[0].values.size() == currentMeshPointsSize) {
                for (size_t i = 0; i < currentMeshPointsSize; i++) {
                    totalMesh.color[colorOffset + i] = mesh.colors[0].values[i];
                }
            } else if (mesh.colors[0].values.size() == mesh.faces.size()) {
                // in a case which we have colors or opacity per face, we need to add per vertex
                // values since ply format needs per vertex color and opacity
                for (size_t i = 0, k = 0; i < mesh.faces.size(); i++) {
                    GfVec3f colorValue = mesh.colors[0].values[i];
                    for (int j = 0; j < mesh.faces[i]; j++) {
                        totalMesh.color[mesh.indices[k + j]] = colorValue;
                    }
                    k += mesh.faces[i];
                }
            } else {
                TF_WARN("Mesh has color property which is not per vertex nor per face.");
            }
        } else {
            std::fill(
              totalMesh.color.begin() + colorOffset, totalMesh.color.end(), GfVec3f(1.0, 1.0, 1.0));
        }
    }

    // Special aggregation for indices. They are stored in a vector of vector order.
    for (size_t i = 0, k = 0; i < mesh.faces.size(); i++) {
        int faceCount = mesh.faces[i];
        totalMesh.indices[indicesOffset + i].resize(faceCount);
        if (shouldExpand) { // These are dummy indices
            for (int j = 0; j < faceCount; j++) {
                totalMesh.indices[indicesOffset + i][j] = k + j + pointsOffset;
            }
        } else {
            for (int j = 0; j < faceCount; j++) {
                totalMesh.indices[indicesOffset + i][j] = mesh.indices[k + j] + pointsOffset;
            }
        }
        k += faceCount;
    }

    for (size_t i = 0; i < currentMeshPointsSize; i++) {
        totalMesh.points[pointsOffset + i] = GfVec3f(modelMatrix.Transform(mesh.points[i]));
    }

    if (subMeshHasNormals) {
        totalMesh.normals.resize(normalsOffset + currentMeshPointsSize);

        bool generateNormalsFromFaces = false;
        if (mesh.normals.values.size()) {
            // Need to check that currentMeshPointsSize is the same as the number of normals
            if (currentMeshPointsSize == mesh.normals.values.size()) {
                for (size_t i = 0; i < currentMeshPointsSize; i++) {
                    GfVec3f normal(normalMatrix.TransformDir(mesh.normals.values[i]));
                    normal.Normalize();
                    totalMesh.normals[normalsOffset + i] = normal;
                }
            } else {
                // This is an unexpected situation. The number of normals should be made the same
                // when we expand them using the indices in exportPly().  We will ignore the
                // provided normals and generate normals from the faces (below).
                generateNormalsFromFaces = true;
                TF_WARN("Number of normals in mesh does not match the number of vertices.");
            }
        } else {
            // There are no normals but they are needed so set the flag to trigger their generation
            generateNormalsFromFaces = true;
        }
        if (generateNormalsFromFaces) {
            // we need to compute vertex normals and so we'll just use face normals
            for (size_t i = 0, k = 0; i < mesh.faces.size(); i++) {
                int nverts = mesh.faces[i];
                if (nverts >= 3) {
                    GfVec3f v0 = mesh.points[k];
                    GfVec3f v1 = mesh.points[k + 1];
                    GfVec3f v2 = mesh.points[k + 2];
                    GfVec3f normal = GfCross(v1 - v0, v2 - v0);
                    GfVec3f xfNormal(normalMatrix.TransformDir(normal));
                    xfNormal.Normalize();
                    for (size_t j = 0; j < nverts; j++) {
                        totalMesh.normals[normalsOffset + k + j] = xfNormal;
                    }
                } else {
                    // The faces is degenerate, so we just assign a default value
                    for (size_t j = 0; j < nverts; j++) {
                        totalMesh.normals[normalsOffset + k + j] = GfVec3f(0, 0, 1);
                    }
                }
                k += nverts;
            }
        }
    }

    if (subMeshHasUvs) {
        totalMesh.uvs.resize(uvsOffset + currentMeshPointsSize);

        bool filled = false;
        if (mesh.uvs.values.size()) {
            // Need to check that currentMeshPointsSize is the same as the number of UVs
            if (currentMeshPointsSize == mesh.uvs.values.size()) {
                for (size_t i = 0; i < currentMeshPointsSize; i++) {
                    totalMesh.uvs[uvsOffset + i] = mesh.uvs.values[i];
                }
                filled = true;
            } else {
                TF_WARN("Number of uvs in mesh does not match the number of vertices.");
                // As mentioned above for the similar situation for normals, this case is
                // unexpected and so we just fill the array with a default value for uvs (below)
            }
        }
        if (!filled) {
            for (size_t i = 0; i < currentMeshPointsSize; i++) {
                totalMesh.uvs[uvsOffset + i] = GfVec2f(0, 0);
            }
        }
    }

    if (totalMesh.asGsplats) {
        // Aggregate Gsplat attributes
        GfMatrix4f modelMatrixFloat(modelMatrix);

        // An individual splat cannot be sheared and thus we extract a uniform scaling factor.
        const float modelScaling = std::cbrt(std::abs(modelMatrixFloat.GetDeterminant()));
        const GfQuatf modelRotation = modelMatrixFloat.ExtractRotationQuat().GetNormalized();

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
    }

    TF_DEBUG_MSG(FILE_FORMAT_PLY,
                 "ply::export aggregated mesh %s { faces: %lu, vIdx: %lu, v: %lu }\n",
                 mesh.name.c_str(),
                 mesh.faces.size(),
                 mesh.indices.size(),
                 currentMeshPointsSize);
}

void
traverseNodesAndFindGsplats(UsdData& usd, PlyTotalMesh& totalMesh, int nodeIndex)
{
    const Node& node = usd.nodes[nodeIndex];
    for (int meshIndex : node.staticMeshes) {
        Mesh& mesh = usd.meshes[meshIndex];
        if (mesh.asGsplats) {
            totalMesh.asGsplats = true;
            return;
        }
    }

    for (size_t i = 0; i < node.children.size(); i++) {
        traverseNodesAndFindGsplats(usd, totalMesh, node.children[i]);
        if (!totalMesh.asGsplats)
            return;
    }
}

std::size_t
traverseNodesAndFindMaxNumSHCoeffs(UsdData& usd, int nodeIndex)
{
    const Node& node = usd.nodes[nodeIndex];
    std::size_t maxNumSHCoeffs = 0;
    for (int meshIndex : node.staticMeshes) {
        Mesh& mesh = usd.meshes[meshIndex];
        if (mesh.asGsplats)
            maxNumSHCoeffs = std::max(maxNumSHCoeffs, mesh.pointSHCoeffs.size());
    }

    for (size_t i = 0; i < node.children.size(); i++) {
        maxNumSHCoeffs =
          std::max(maxNumSHCoeffs, traverseNodesAndFindMaxNumSHCoeffs(usd, node.children[i]));
    }
    return maxNumSHCoeffs;
}

void
aggregateMeshDataRequirements(std::vector<Mesh>& meshes,
                              bool& subMeshHasNormals,
                              bool& subMeshHasUvs,
                              bool& subMeshHasOpacity,
                              bool& subMeshHasColor)
{
    for (const Mesh& m : meshes) {
        subMeshHasNormals |= !m.normals.values.empty();
        subMeshHasUvs |= !m.uvs.values.empty();
        subMeshHasColor |= !m.colors.empty();
        subMeshHasOpacity |= !m.opacities.empty();
    }
}

void
traverseNodesAndAggregateMeshes(UsdData& usd,
                                PlyTotalMesh& totalMesh,
                                const GfMatrix4d& correctionTransform,
                                bool shouldExpand,
                                int nodeIndex,
                                bool subMeshHasNormals,
                                bool subMeshHasUvs,
                                bool subMeshHasOpacity,
                                bool subMeshHasColor)
{
    const Node& node = usd.nodes[nodeIndex];
    GfMatrix4d modelMatrix = node.worldTransform * correctionTransform;
    GfMatrix4d normalMatrix = modelMatrix.GetInverse().GetTranspose();

    for (int meshIndex : node.staticMeshes) {
        Mesh& mesh = usd.meshes[meshIndex];
        aggregateMeshInstance(totalMesh,
                              mesh,
                              modelMatrix,
                              normalMatrix,
                              shouldExpand,
                              subMeshHasNormals,
                              subMeshHasUvs,
                              subMeshHasColor,
                              subMeshHasOpacity);
    }

    for (const auto& [skeletonIndex, meshIndices] : node.skinnedMeshes) {
        for (int meshIndex : meshIndices) {
            Mesh& mesh = usd.meshes[meshIndex];
            aggregateMeshInstance(totalMesh,
                                  mesh,
                                  modelMatrix,
                                  normalMatrix,
                                  shouldExpand,
                                  subMeshHasNormals,
                                  subMeshHasUvs,
                                  subMeshHasColor,
                                  subMeshHasOpacity);
        }
    }
    for (size_t i = 0; i < node.children.size(); i++) {
        traverseNodesAndAggregateMeshes(usd,
                                        totalMesh,
                                        correctionTransform,
                                        shouldExpand,
                                        node.children[i],
                                        subMeshHasNormals,
                                        subMeshHasUvs,
                                        subMeshHasColor,
                                        subMeshHasOpacity);
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
exportPly(UsdData& usd, happly::PLYData& ply)
{
    if (usd.meshes.size() <= 0) {
        TF_DEBUG_MSG(FILE_FORMAT_PLY,
                     "ply::export no instances of UsdGeomMesh, nothing will be exported\n");
        return true;
    }

    // Unfortunately there lacks documentation on how to set indices for uvs and normals.
    // So we need to make sure all properties have the same interpolation so they can share
    // the position indices.
    // Otherwise we need to expand all. But for that case, the documentation doesn't say how to set
    // positions without indices, so we still fabricate dummy indices.
    bool shouldExpand = meshesRequireExpansion(usd.meshes);
    if (shouldExpand) {
        for (Mesh& m : usd.meshes) {
            if (m.asPoints)
                continue;

            TF_DEBUG_MSG(FILE_FORMAT_PLY,
                         "mesh: faces:%d indices:%d pts:%d norInd:%d normals:%d uvInd:%d uvs:%d\n",
                         m.faces.size(),
                         m.indices.size(),
                         m.points.size(),
                         m.normals.indices.size(),
                         m.normals.values.size(),
                         m.uvs.indices.size(),
                         m.uvs.values.size());

            expandIndexedValues(m.indices, m.points);
            if (m.uvs.indices.size()) {
                if (m.uvs.indices.size() == m.indices.size()) {
                    expandIndexedValues(m.uvs.indices, m.uvs.values);
                } else {
                    expandIndexedValuesIndirect(m.indices, m.uvs.indices, m.uvs.values);
                }
            } else if (m.indices.size() != m.uvs.values.size()) {
                expandIndexedValues(m.indices, m.uvs.values);
            }
            if (m.normals.indices.size()) {
                if (m.normals.indices.size() == m.indices.size()) {
                    expandIndexedValues(m.normals.indices, m.normals.values);
                } else {
                    expandIndexedValuesIndirect(m.indices, m.normals.indices, m.normals.values);
                }
            } else if (m.indices.size() != m.normals.values.size()) {
                expandIndexedValues(m.indices, m.normals.values);
            }

            if (m.colors.size()) { // translate only first set of colors
                Primvar<GfVec3f>& colorSet = m.colors[0];

                if (colorSet.indices.size()) {
                    if (colorSet.indices.size() == m.indices.size()) {
                        expandIndexedValues(colorSet.indices, colorSet.values);
                    } else {
                        expandIndexedValuesIndirect(m.indices, colorSet.indices, colorSet.values);
                    }
                } else {
                    expandIndexedValues(m.indices, colorSet.values);
                }
            }
            if (m.opacities.size()) { // translate only first set of opacities
                Primvar<float>& opacitySet = m.opacities[0];

                if (opacitySet.indices.size()) {
                    if (opacitySet.indices.size() == m.indices.size()) {
                        expandIndexedValues(opacitySet.indices, opacitySet.values);
                    } else {
                        expandIndexedValuesIndirect(
                          m.indices, opacitySet.indices, opacitySet.values);
                    }
                } else {
                    expandIndexedValues(m.indices, opacitySet.values);
                }
            }
        }
    }

    // Because Ply does not support multiple individual meshes, we need to aggregate all meshes into
    // a single mesh and apply their local to world transforms, together with the system's
    // correction transform.
    PlyTotalMesh totalMesh;
    GfMatrix4d correctionTransform;
    // First check if the Ply should be a Gsplat. It is considered as a Gsplat as long as one
    // sub-point-cloud is a Gsplat since a Gsplat is an extension of a regular point cloud.
    for (size_t i = 0; i < usd.rootNodes.size(); i++) {
        traverseNodesAndFindGsplats(usd, totalMesh, usd.rootNodes[i]);
        if (totalMesh.asGsplats)
            break;
    }

    std::size_t numGsplatsSHCoeffs = 0;
    for (size_t i = 0; i < usd.rootNodes.size(); i++) {
        numGsplatsSHCoeffs =
          std::max(numGsplatsSHCoeffs, traverseNodesAndFindMaxNumSHCoeffs(usd, usd.rootNodes[i]));
    }
    if (totalMesh.asGsplats) {
        totalMesh.shCoeffs.resize(numGsplatsSHCoeffs);
        ply.comments.push_back("Gaussian Splats with Y-axis up");
    }
    correctionTransform = getTransformToMetersPositiveY(usd.metersPerUnit, usd.upAxis);

    bool subMeshHasNormals = false;
    bool subMeshHasUvs = false;
    bool subMeshHasOpacity = false;
    bool subMeshHasColor = false;
    aggregateMeshDataRequirements(
      usd.meshes, subMeshHasNormals, subMeshHasUvs, subMeshHasOpacity, subMeshHasColor);

    for (size_t i = 0; i < usd.rootNodes.size(); i++) {
        traverseNodesAndAggregateMeshes(usd,
                                        totalMesh,
                                        correctionTransform,
                                        shouldExpand,
                                        usd.rootNodes[i],
                                        subMeshHasNormals,
                                        subMeshHasUvs,
                                        subMeshHasOpacity,
                                        subMeshHasColor);
    }

    TF_DEBUG_MSG(FILE_FORMAT_PLY,
                 "totalMesh: points=%d indices=%d normals=%d uvs=%d\n",
                 totalMesh.points.size(),
                 totalMesh.indices.size(),
                 totalMesh.normals.size(),
                 totalMesh.uvs.size());

    if (totalMesh.points.size()) {
        std::string faceName = "face";
        std::string vertexName = "vertex";
        if (totalMesh.indices.size()) {
            ply.addElement(faceName, totalMesh.indices.size());
            ply.getElement(faceName).addListProperty<int32_t>("vertex_indices", totalMesh.indices);
        }
        if (totalMesh.points.size()) {
            std::vector<float> positionsX(totalMesh.points.size());
            std::vector<float> positionsY(totalMesh.points.size());
            std::vector<float> positionsZ(totalMesh.points.size());
            for (size_t i = 0; i < totalMesh.points.size(); i++) {
                positionsX[i] = totalMesh.points[i][0];
                positionsY[i] = totalMesh.points[i][1];
                positionsZ[i] = totalMesh.points[i][2];
            }
            ply.addElement(vertexName, totalMesh.points.size());
            happly::Element& vertexElement = ply.getElement(vertexName);
            vertexElement.addProperty<float>("x", positionsX);
            vertexElement.addProperty<float>("y", positionsY);
            vertexElement.addProperty<float>("z", positionsZ);
        }
        if (totalMesh.normals.size()) {
            std::vector<float> nx(totalMesh.normals.size());
            std::vector<float> ny(totalMesh.normals.size());
            std::vector<float> nz(totalMesh.normals.size());
            for (size_t i = 0; i < totalMesh.normals.size(); i++) {
                nx[i] = totalMesh.normals[i][0];
                ny[i] = totalMesh.normals[i][1];
                nz[i] = totalMesh.normals[i][2];
            }
            happly::Element& vertexElement = ply.getElement(vertexName);
            vertexElement.addProperty<float>("nx", nx);
            vertexElement.addProperty<float>("ny", ny);
            vertexElement.addProperty<float>("nz", nz);
        }
        if (totalMesh.uvs.size()) {
            std::vector<float> u(totalMesh.uvs.size());
            std::vector<float> v(totalMesh.uvs.size());
            for (size_t i = 0; i < totalMesh.uvs.size(); i++) {
                u[i] = totalMesh.uvs[i][0];
                v[i] = totalMesh.uvs[i][1];
            }
            happly::Element& vertexElement = ply.getElement(vertexName);
            vertexElement.addProperty<float>("texture_u", u);
            vertexElement.addProperty<float>("texture_v", v);
        }
        if (totalMesh.asGsplats) {
            // Gsplat attributes.
            // Zeroth coefficient of SH, inversed as 2sqrt(pi)
            constexpr float invShC0 = 3.5449077018f;
            if (totalMesh.color.size()) {
                std::vector<float> r(totalMesh.color.size());
                std::vector<float> g(totalMesh.color.size());
                std::vector<float> b(totalMesh.color.size());
                for (size_t i = 0; i < totalMesh.color.size(); i++) {
                    r[i] = (totalMesh.color[i][0] - 0.5f) * invShC0;
                    g[i] = (totalMesh.color[i][1] - 0.5f) * invShC0;
                    b[i] = (totalMesh.color[i][2] - 0.5f) * invShC0;
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<float>("f_dc_0", r);
                vertexElement.addProperty<float>("f_dc_1", g);
                vertexElement.addProperty<float>("f_dc_2", b);
            }
            if (totalMesh.opacity.size()) {
                std::vector<float> a(totalMesh.opacity.size());
                for (size_t i = 0; i < totalMesh.opacity.size(); i++) {
                    a[i] = encodeGsplatOpacity(totalMesh.opacity[i]);
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<float>("opacity", a);
            }
            if (totalMesh.widths.size()) {
                std::vector<float> scale0(totalMesh.widths.size());
                for (size_t i = 0; i < totalMesh.widths.size(); i++) {
                    scale0[i] = encodeGsplatWidth(totalMesh.widths[i]);
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<float>("scale_0", scale0);
            }
            if (totalMesh.widths1.size()) {
                std::vector<float> scale1(totalMesh.widths1.size());
                for (size_t i = 0; i < totalMesh.widths1.size(); i++) {
                    scale1[i] = encodeGsplatWidth(totalMesh.widths1[i]);
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<float>("scale_1", scale1);
            }
            if (totalMesh.widths2.size()) {
                std::vector<float> scale2(totalMesh.widths2.size());
                for (size_t i = 0; i < totalMesh.widths2.size(); i++) {
                    scale2[i] = encodeGsplatWidth(totalMesh.widths2[i]);
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<float>("scale_2", scale2);
            }
            if (totalMesh.rotations.size()) {
                std::vector<float> rot0(totalMesh.rotations.size());
                std::vector<float> rot1(totalMesh.rotations.size());
                std::vector<float> rot2(totalMesh.rotations.size());
                std::vector<float> rot3(totalMesh.rotations.size());
                for (size_t i = 0; i < totalMesh.rotations.size(); i++) {
                    rot0[i] = totalMesh.rotations[i].GetReal();
                    rot1[i] = totalMesh.rotations[i].GetImaginary()[0];
                    rot2[i] = totalMesh.rotations[i].GetImaginary()[1];
                    rot3[i] = totalMesh.rotations[i].GetImaginary()[2];
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<float>("rot_0", rot0);
                vertexElement.addProperty<float>("rot_1", rot1);
                vertexElement.addProperty<float>("rot_2", rot2);
                vertexElement.addProperty<float>("rot_3", rot3);
            }
            for (size_t shIndex = 0; shIndex < totalMesh.shCoeffs.size(); ++shIndex) {
                std::vector<float> shCoeff(totalMesh.shCoeffs[shIndex].size());
                for (size_t i = 0; i < totalMesh.shCoeffs[shIndex].size(); i++) {
                    shCoeff[i] = totalMesh.shCoeffs[shIndex][i];
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                const std::string propName = std::string("f_rest_") + std::to_string(shIndex);
                vertexElement.addProperty<float>(propName, shCoeff);
            }
        } else {
            // Mesh or regular point cloud.
            if (totalMesh.color.size()) {
                std::vector<uint8_t> r(totalMesh.color.size());
                std::vector<uint8_t> g(totalMesh.color.size());
                std::vector<uint8_t> b(totalMesh.color.size());
                for (size_t i = 0; i < totalMesh.color.size(); i++) {
                    r[i] = static_cast<uint8_t>(
                      std::clamp(totalMesh.color[i][0] * 255.0f, 0.0f, 255.0f));
                    g[i] = static_cast<uint8_t>(
                      std::clamp(totalMesh.color[i][1] * 255.0f, 0.0f, 255.0f));
                    b[i] = static_cast<uint8_t>(
                      std::clamp(totalMesh.color[i][2] * 255.0f, 0.0f, 255.0f));
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<uint8_t>("red", r);
                vertexElement.addProperty<uint8_t>("green", g);
                vertexElement.addProperty<uint8_t>("blue", b);
            }
            if (totalMesh.opacity.size()) {
                std::vector<uint8_t> a(totalMesh.opacity.size());
                for (size_t i = 0; i < totalMesh.opacity.size(); i++) {
                    a[i] =
                      static_cast<uint8_t>(std::clamp(totalMesh.opacity[i] * 255.0f, 0.0f, 255.0f));
                }
                happly::Element& vertexElement = ply.getElement(vertexName);
                vertexElement.addProperty<uint8_t>("alpha", a);
            }
        }
    }
    return true;
}

}
