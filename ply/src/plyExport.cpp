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
#include <common.h>
#include <geometry.h>
#include <images.h>
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
#include <transforms.h>

using namespace PXR_NS;

namespace adobe::usd {

bool
meshesRequireExpansion(std::vector<Mesh>& meshes)
{
    for (const Mesh& m : meshes) {
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
};

void
aggregateMeshInstance(PlyTotalMesh& totalMesh,
                      const Mesh& mesh,
                      const GfMatrix4d& modelMatrix,
                      const GfMatrix4d& normalMatrix,
                      bool shouldExpand)
{
    // These references are grabbed just for easier handling later.
    const VtVec3fArray& colorValues = mesh.colors.size() ? mesh.colors[0].values : VtVec3fArray();
    const VtFloatArray& opacityValues =
      mesh.opacities.size() ? mesh.opacities[0].values : VtFloatArray();

    size_t indicesOffset = totalMesh.indices.size();
    size_t pointsOffset = totalMesh.points.size();
    size_t normalsOffset = totalMesh.normals.size();
    size_t uvsOffset = totalMesh.uvs.size();
    size_t colorOffset = totalMesh.color.size();
    size_t opacityOffset = totalMesh.opacity.size();
    totalMesh.indices.resize(indicesOffset + mesh.faces.size());
    totalMesh.points.resize(pointsOffset + mesh.points.size());
    totalMesh.uvs.resize(uvsOffset + mesh.uvs.values.size());
    totalMesh.normals.resize(normalsOffset + mesh.normals.values.size());
    totalMesh.color.resize(colorOffset + colorValues.size());
    totalMesh.opacity.resize(opacityOffset + opacityValues.size());

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
    for (size_t i = 0; i < mesh.points.size(); i++) {
        totalMesh.points[pointsOffset + i] = modelMatrix.Transform(mesh.points[i]);
    }
    for (size_t i = 0; i < mesh.normals.values.size(); i++) {
        totalMesh.normals[normalsOffset + i] = normalMatrix.TransformDir(mesh.normals.values[i]);
        totalMesh.normals[normalsOffset + i].Normalize();
    }
    for (size_t i = 0; i < mesh.uvs.values.size(); i++) {
        totalMesh.uvs[uvsOffset + i] = mesh.uvs.values[i];
    }
    for (size_t i = 0; i < colorValues.size(); i++) {
        totalMesh.color[colorOffset + i] = colorValues[i];
    }
    for (size_t i = 0; i < opacityValues.size(); i++) {
        totalMesh.opacity[opacityOffset + i] = opacityValues[i];
    }
    TF_DEBUG_MSG(FILE_FORMAT_PLY,
                 "ply::export aggregated mesh %s { faces: %lu, vIdx: %lu, v: %lu }\n",
                 mesh.name.c_str(),
                 mesh.faces.size(),
                 mesh.indices.size(),
                 mesh.points.size());
}

void
traverseNodesAndAggregateMeshes(UsdData& usd,
                                PlyTotalMesh& totalMesh,
                                const GfMatrix4d& correctionTransform,
                                bool shouldExpand,
                                int nodeIndex)
{
    const Node& node = usd.nodes[nodeIndex];
    GfMatrix4d modelMatrix = node.worldTransform * correctionTransform;
    GfMatrix4d normalMatrix = modelMatrix.GetInverse().GetTranspose();
    for (int meshIndex : node.staticMeshes) {
        Mesh& mesh = usd.meshes[meshIndex];
        aggregateMeshInstance(totalMesh, mesh, modelMatrix, normalMatrix, shouldExpand);
    }
    for (const auto& [skeletonIndex, meshIndices] : node.skinnedMeshes) {
        for (int meshIndex : meshIndices) {
            Mesh& mesh = usd.meshes[meshIndex];
            aggregateMeshInstance(totalMesh, mesh, modelMatrix, normalMatrix, shouldExpand);
        }
    }
    for (size_t i = 0; i < node.children.size(); i++) {
        traverseNodesAndAggregateMeshes(
          usd, totalMesh, correctionTransform, shouldExpand, node.children[i]);
    }
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
            expandIndexedValues(m.indices, m.points);
            expandIndexedValues(m.uvs.indices.size() ? m.uvs.indices : m.indices, m.uvs.values);
            expandIndexedValues(m.normals.indices.size() ? m.normals.indices : m.indices,
                                m.normals.values);
            if (m.colors.size()) { // translate only first set of colors
                Primvar<PXR_NS::GfVec3f>& colorSet = m.colors[0];
                expandIndexedValues(colorSet.indices.size() ? colorSet.indices : m.indices,
                                    colorSet.values);
            }
            if (m.opacities.size()) { // translate only first set of opacities
                Primvar<float>& opaciySet = m.opacities[0];
                expandIndexedValues(opaciySet.indices.size() ? opaciySet.indices : m.indices,
                                    opaciySet.values);
            }
        }
    }

    // Because Ply does not support multiple individual meshes, we need to aggregate all meshes into
    // a single mesh and apply their local to world transforms, together with the system's
    // correction transform.
    GfMatrix4d correctionTransform = getTransformToMetersPositiveY(usd.metersPerUnit, usd.upAxis);
    PlyTotalMesh totalMesh;
    for (size_t i = 0; i < usd.rootNodes.size(); i++) {
        traverseNodesAndAggregateMeshes(
          usd, totalMesh, correctionTransform, shouldExpand, usd.rootNodes[i]);
    }

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
        if (totalMesh.color.size()) {
            std::vector<uint8_t> r(totalMesh.color.size());
            std::vector<uint8_t> g(totalMesh.color.size());
            std::vector<uint8_t> b(totalMesh.color.size());
            for (size_t i = 0; i < totalMesh.color.size(); i++) {
                r[i] = totalMesh.color[i][0] * 255.0f;
                g[i] = totalMesh.color[i][1] * 255.0f;
                b[i] = totalMesh.color[i][2] * 255.0f;
            }
            happly::Element& vertexElement = ply.getElement(vertexName);
            vertexElement.addProperty<uint8_t>("red", r);
            vertexElement.addProperty<uint8_t>("green", g);
            vertexElement.addProperty<uint8_t>("blue", b);
        }
        if (totalMesh.opacity.size()) {
            std::vector<uint8_t> a(totalMesh.opacity.size());
            for (size_t i = 0; i < totalMesh.opacity.size(); i++) {
                a[i] = totalMesh.opacity[i] * 255.0f;
            }
            happly::Element& vertexElement = ply.getElement(vertexName);
            vertexElement.addProperty<uint8_t>("alpha", a);
        }
    }
    return true;
}

}
