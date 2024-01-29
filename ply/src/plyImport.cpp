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
#include "plyImport.h"
#include "debugCodes.h"
#include <common.h>
#include <geometry.h>
#include <happly.h>
#include <images.h>
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
using namespace happly;

namespace adobe::usd {

bool
importPly(const ImportPlyOptions& options, PLYData& ply, UsdData& usd)
{
    // It would be nice to print the ply data format and version but they are private.
    for (const std::string& comment : ply.comments) {
        TF_DEBUG_MSG(FILE_FORMAT_PLY, "Comment: %s\n", comment.c_str());
    }

    std::vector<float> positionsX;
    std::vector<float> positionsY;
    std::vector<float> positionsZ;
    std::vector<float> nx;
    std::vector<float> ny;
    std::vector<float> nz;
    std::vector<float> u;
    std::vector<float> v;
    std::vector<unsigned char> r;
    std::vector<unsigned char> g;
    std::vector<unsigned char> b;
    std::vector<unsigned char> a;
    try {
        Element& element = ply.getElement("vertex");
        // happly provides plyIn.getVertexPositions(), but it uses double, so avoid it to avoid
        // extra work.
        try {
            positionsX = element.getProperty<float>("x");
            positionsY = element.getProperty<float>("y");
            positionsZ = element.getProperty<float>("z");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid position data: %s\n", e.what());
            return false;
        }
        try {
            nx = element.getProperty<float>("nx");
            ny = element.getProperty<float>("ny");
            nz = element.getProperty<float>("nz");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid normal data: %s\n", e.what());
        }
        try {
            u = element.getProperty<float>("texture_u");
            v = element.getProperty<float>("texture_v");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid uv data: %s\n", e.what());
        }
        try {
            r = element.getProperty<unsigned char>("red");
            g = element.getProperty<unsigned char>("green");
            b = element.getProperty<unsigned char>("blue");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid color data: %s\n", e.what());
        }
        try {
            a = element.getProperty<unsigned char>("alpha");
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid alpha color data: %s\n", e.what());
        }
    } catch (std::exception& e) {
        TF_DEBUG_MSG(FILE_FORMAT_PLY, "Could not find vertex element %s\n", e.what());
    }

    TF_DEBUG_MSG(FILE_FORMAT_PLY,
                 "Importing as points: %s, width: %f\n",
                 options.importAsPoints ? "true" : "false",
                 options.pointWidth);

    auto [meshIndex, mesh] = usd.addMesh();
    mesh.asPoints = options.importAsPoints;
    mesh.pointWidth = options.pointWidth;

    mesh.points.resize(positionsX.size());
    for (size_t i = 0; i < mesh.points.size(); i++) {
        mesh.points[i][0] = positionsX[i];
        mesh.points[i][1] = positionsY[i];
        mesh.points[i][2] = positionsZ[i];
    }

    if (nx.size()) {
        mesh.normals.values.resize(nx.size());
        for (size_t i = 0; i < mesh.normals.values.size(); i++) {
            mesh.normals.values[i][0] = nx[i];
            mesh.normals.values[i][1] = ny[i];
            mesh.normals.values[i][2] = nz[i];
        }
        mesh.normals.interpolation = UsdGeomTokens->vertex;
    }

    if (u.size()) {
        mesh.uvs.values.resize(u.size());
        for (size_t i = 0; i < mesh.uvs.values.size(); i++) {
            mesh.uvs.values[i][0] = u[i];
            mesh.uvs.values[i][1] = v[i];
        }
        mesh.uvs.interpolation = UsdGeomTokens->vertex;
    }

    if (r.size()) {
        auto [colorIndex, colors] = usd.addColorSet(meshIndex);
        colors.interpolation = UsdGeomTokens->vertex;
        colors.values.resize(r.size());
        for (size_t i = 0; i < colors.values.size(); i++) {
            colors.values[i][0] = static_cast<float>(r[i]) / 255.0f;
            colors.values[i][1] = static_cast<float>(g[i]) / 255.0f;
            colors.values[i][2] = static_cast<float>(b[i]) / 255.0f;
        }
    }

    if (a.size()) {
        auto [opacityIndex, opacity] = usd.addOpacitySet(meshIndex);
        opacity.interpolation = UsdGeomTokens->vertex;
        opacity.values.resize(a.size());
        for (size_t i = 0; i < opacity.values.size(); i++) {
            opacity.values[i] = static_cast<float>(a[i]) / 255.0f;
        }
    }

    if (!options.importAsPoints) {
        try {
            std::vector<std::vector<size_t>> indices = ply.getFaceIndices<size_t>();
            mesh.faces.resize(indices.size());
            for (size_t i = 0; i < indices.size(); i++) {
                mesh.faces[i] = indices[i].size();
                for (size_t j = 0; j < indices[i].size(); j++) {
                    mesh.indices.push_back(indices[i][j]);
                }
            }
        } catch (std::exception& e) {
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Invalid index data: %s\n", e.what());
            TF_DEBUG_MSG(FILE_FORMAT_PLY, "Creating triangulation indices\n");
            createTriangulationIndices(mesh);
        }
    }

    auto [nodeIndex, node] = usd.addNode(-1);
    node.staticMeshes.push_back(meshIndex);
    return true;
}

}