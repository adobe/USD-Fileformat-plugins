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
#include "stlExport.h"

#include <fileformatutils/common.h>
#include <fileformatutils/usdData.h>

#include <string>

using namespace PXR_NS;
using namespace adobe::usd;

namespace usdStl {

StlFormat
readStlExportFormat(const UsdData& data)
{
    StlFormat format = StlFormat::Binary;
    const auto formatValueIt = data.metadata.find("exportASCII");
    if (formatValueIt != data.metadata.end()) {
        const auto exportAscii = formatValueIt->second.GetWithDefault<bool>();
        if (exportAscii) {
            format = StlFormat::Ascii;
        }
    }
    return format;
}

bool
exportStl(const ExportStlOptions& options, const UsdData& usd, StlModel& stl)
{
    if (usd.nodes.empty())
        return false;

    GfMatrix4d upAxisTransform(1.0f);
    std::string upAxis = usd.upAxis.GetString();
    if (!upAxis.empty() && std::toupper(upAxis[0]) == 'Y') {
        upAxisTransform = GfMatrix4d(GfRotation(GfVec3d(1.0f, 0.0f, 0.0f), 90.0f), GfVec3d(0.0f));
    }

    for (const Node& node : usd.nodes) {
        GfMatrix4d worldTransform = node.worldTransform * upAxisTransform;
        for (int meshIndex : node.staticMeshes) {
            if (meshIndex < 0 || meshIndex >= usd.meshes.size()) {
                TF_WARN("Invalid mesh index %d -- Skipping", meshIndex);
                continue;
            }
            const Mesh& mesh = usd.meshes[meshIndex];
            VtArray<int> meshIndices;
            if (mesh.indices.empty()) {
                meshIndices.resize(mesh.points.size());
                std::iota(meshIndices.begin(), meshIndices.end(), 0);
            } else {
                meshIndices = mesh.indices;
            }

            for (size_t i = 0; i + 2 < meshIndices.size(); i += 3) {
                StlFacet facet;
                for (int j = 0; j < 3; j++) {
                    StlVec3f vertex;
                    const int vertex_index = meshIndices[i + j];
                    const GfVec3f& vertex_data = mesh.points[vertex_index];
                    const GfVec3f transformedPoint = GfVec3f(worldTransform.Transform(vertex_data));
                    vertex = { transformedPoint[0], transformedPoint[1], transformedPoint[2] };
                    facet.vertices[j] = vertex;
                }

                StlNormal normal;
                // compute facet normals from topology
                StlVec3f faceEdge1;
                faceEdge1.x = facet.vertices[2].x - facet.vertices[0].x;
                faceEdge1.y = facet.vertices[2].y - facet.vertices[0].y;
                faceEdge1.z = facet.vertices[2].z - facet.vertices[0].z;

                StlVec3f faceEdge2;
                faceEdge2.x = facet.vertices[1].x - facet.vertices[0].x;
                faceEdge2.y = facet.vertices[1].y - facet.vertices[0].y;
                faceEdge2.z = facet.vertices[1].z - facet.vertices[0].z;

                normal = crossProduct(faceEdge1, faceEdge2);
                // handle degenerate normals
                if (normal.x == 0.f && normal.y == 0.f && normal.z == 0.f)
                    normal.y = 1.f; // Synthesize a valid normal. Actual value is irrelevant because
                                    // the triangle won't be visible

                normal.normalize();
                facet.normal = normal;

                stl.AddFacet(facet);
            }
        }
    }

    return true;
}
}
