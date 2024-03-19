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
#include <usdData.h>
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

    for (const Node& node : usd.nodes) {
        for (int meshIndex : node.staticMeshes) {
            const Mesh& mesh = usd.meshes[meshIndex];
            PXR_NS::VtArray<int> meshIndices;
            if (mesh.indices.empty()) {
                meshIndices.resize(mesh.points.size());
                std::iota(std::begin(meshIndices), std::end(meshIndices), 0);
            } else {
                meshIndices = mesh.indices;
            }

            for (size_t i = 0; i < meshIndices.size(); i += 3) {
                StlFacet facet;
                for (int j = 0; j < 3; j++) {
                    StlVec3f vertex;
                    const int vertex_index = meshIndices[i + j];
                    const PXR_NS::GfVec3f& vertex_data = mesh.points[vertex_index];
                    const PXR_NS::GfVec3f transformedPoint =
                      node.worldTransform.Transform(vertex_data);
                    vertex.x = transformedPoint[0];
                    vertex.y = transformedPoint[1];
                    vertex.z = transformedPoint[2];
                    facet.vertices[j] = vertex;
                }

                if (mesh.normals.values.size() > 0) {
                    StlNormal normal;
                    if (!mesh.normals.indices.empty()) {
                        const int normal_index = mesh.normals.indices[((i + 3) / 3) - 1];
                        const PXR_NS::GfVec3f& normal_data = mesh.normals.values[normal_index];
                        const PXR_NS::GfVec3f transformedNormal =
                          node.worldTransform.Transform(normal_data);
                        normal.x = transformedNormal[0];
                        normal.y = transformedNormal[1];
                        normal.z = transformedNormal[2];
                    } else {
                        if (mesh.normals.values.size() == mesh.points.size()) {
                            for (int j = 0; j < 3; j++) {
                                const int vertex_index = meshIndices[i + j];
                                const PXR_NS::GfVec3f& vertex_normal_data =
                                  mesh.normals.values[vertex_index];
                                const PXR_NS::GfVec3f transformedNormal =
                                  node.worldTransform.Transform(vertex_normal_data);
                                normal.x += transformedNormal[0];
                                normal.y += transformedNormal[1];
                                normal.z += transformedNormal[2];
                            }

                            normal.x /= 3;
                            normal.y /= 3;
                            normal.z /= 3;
                        } else {
                            const int normal_index = ((i + 3) / 3) - 1;
                            const PXR_NS::GfVec3f& normal_data = mesh.normals.values[normal_index];
                            const PXR_NS::GfVec3f transformedNormal =
                              node.worldTransform.Transform(normal_data);
                            normal.x = transformedNormal[0];
                            normal.y = transformedNormal[1];
                            normal.z = transformedNormal[2];
                        }
                    }

                    normal.normalize();
                    facet.normal = normal;
                } else {
                    StlVec3f faceEdge1;
                    faceEdge1.x = facet.vertices[2].x - facet.vertices[0].x;
                    faceEdge1.y = facet.vertices[2].y - facet.vertices[0].y;
                    faceEdge1.z = facet.vertices[2].z - facet.vertices[0].z;

                    StlVec3f faceEdge2;
                    faceEdge2.x = facet.vertices[1].x - facet.vertices[0].x;
                    faceEdge2.y = facet.vertices[1].y - facet.vertices[0].y;
                    faceEdge2.z = facet.vertices[1].z - facet.vertices[0].z;

                    facet.normal = crossProduct(faceEdge1, faceEdge2);
                }

                stl.AddFacet(facet);
            }
        }
    }

    return true;
}
}
