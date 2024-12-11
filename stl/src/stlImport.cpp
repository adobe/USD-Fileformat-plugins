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
#include "stlImport.h"
#include "stlModel.h"
#include <fileformatutils/common.h>

#include <pxr/base/gf/range3f.h>
#include <pxr/base/gf/vec3f.h>
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
using namespace adobe;
using namespace adobe::usd;

namespace usdStl {

bool
importStl(UsdData& usd, const StlModel& stl)
{
    auto [nodeIndex, node] = usd.addNode(-1);
    auto [meshIndex, mesh] = usd.addMesh();
    node.staticMeshes.push_back(meshIndex);

    // Apply rotation to node's worldTransform if Y-up
    std::string upAxis = usd.upAxis.GetString();
    GfMatrix4d rotationMatrix(1.0f);
    if (!upAxis.empty() && std::toupper(upAxis[0]) == 'Y') {
        rotationMatrix = GfMatrix4d(GfRotation(GfVec3d(1.0f, 0.0f, 0.0f), -90.0f), GfVec3d(0.0f));
    }
    node.worldTransform = node.worldTransform * rotationMatrix;

    // Resize mesh data structures based on the number of facets
    size_t facetCount = stl.FacetCount();
    mesh.faces.resize(facetCount);
    mesh.indices.resize(facetCount * 3);
    mesh.points.resize(facetCount * 3);
    mesh.normals.values.resize(facetCount);
    mesh.normals.interpolation = UsdGeomTokens->uniform;

    for (size_t i = 0; i < facetCount; ++i) {
        StlFacet facet = stl.GetFacet(i);
        StlVec3f v0 = facet.vertices[0];
        StlVec3f v1 = facet.vertices[1];
        StlVec3f v2 = facet.vertices[2];
        mesh.faces[i] = 3;
        mesh.indices[3 * i] = 3 * i;
        mesh.indices[3 * i + 1] = 3 * i + 1;
        mesh.indices[3 * i + 2] = 3 * i + 2;

        // Store STL vertices and normals
        mesh.points[3 * i] = GfVec3f(v0.x, v0.y, v0.z);
        mesh.points[3 * i + 1] = GfVec3f(v1.x, v1.y, v1.z);
        mesh.points[3 * i + 2] = GfVec3f(v2.x, v2.y, v2.z);
        GfVec3f usdNormal = GfVec3f(facet.normal.x, facet.normal.y, facet.normal.z);
        usdNormal.Normalize();

        // Handle degenerate normals
        if (usdNormal.GetLengthSq() < 1e-3f) {
            usdNormal = GfVec3f(0.0f, 1.0f, 0.0f); // Synthesize a valid normal
        }

        mesh.normals.values[i] = usdNormal;
    }

    return true;
}

}