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

#include <pxr/base/gf/vec3f.h>
#include <pxr/pxr.h>

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

        /*
        // TODO: preserve original normals on import, once the same is done on export

        StlNormal normal = facet.normal;

        // Handle degenerate or missing normals
        if (normal.lengthSq() < 1e-6f) {
            normal = calculateNormalOfFacet(facet);
        }
        */

        StlNormal normal = calculateNormalOfFacet(facet);

        GfVec3f usdNormal = GfVec3f(normal.x, normal.y, normal.z);
        usdNormal.Normalize();

        mesh.normals.values[i] = usdNormal;
    }

    return true;
}

}