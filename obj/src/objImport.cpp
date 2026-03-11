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
#include "objImport.h"
#include "debugCodes.h"
#include <fileformatutils/common.h>
#include <fileformatutils/images.h>
#include <fileformatutils/materials.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <string>

using namespace PXR_NS;

PXR_NAMESPACE_OPEN_SCOPE
// clang-format off
TF_DEFINE_PRIVATE_TOKENS(_groupOptionsTokens,
    (combineGroups)
    (separateGroupsAsSubsets)
    (separateGroupsAsMeshes)
);
// clang-format on
PXR_NAMESPACE_CLOSE_SCOPE

namespace adobe::usd {

void
SetExtent(VtVec3fArray vertexValues, UsdGeomMesh mesh)
{
    GfRange3f extent;
    for (const GfVec3f& pt : vertexValues) {
        extent.UnionWith(pt);
    }
    VtVec3fArray extentArray(2);
    extentArray[0] = extent.GetMin();
    extentArray[1] = extent.GetMax();
    mesh.GetExtentAttr().Set(extentArray);
}

const TfToken&
importChannel(ObjMapChannel channel)
{
    switch (channel) {
        case ObjMapChannelR:
            return AdobeTokens->r;
        case ObjMapChannelG:
            return AdobeTokens->g;
        case ObjMapChannelB:
            return AdobeTokens->b;
        // Note, these channels do not actually exists in the USD space and most modern image
        // formats. These are therefore somewhat arbitrary. The -imfchan option in an mtl file can
        // force the use of a specific single channel
        case ObjMapChannelM:
            return AdobeTokens->a;
        // Luminance is usually computed as a weighted average of RGB, which is not supported in
        // the current USD shading world. So we assume we're dealing with a gray scale texture and
        // just use the first channel.
        case ObjMapChannelL:
            return AdobeTokens->r;
        case ObjMapChannelZ:
            return AdobeTokens->a;
    }

    return AdobeTokens->r;
}

GfVec4f
makeVec4f(float value)
{
    return GfVec4f(value);
}

GfVec4f
makeVec4f(const GfVec3f value)
{
    return GfVec4f(value[0], value[1], value[2], 1);
}

template<typename T>
bool
importMaterialProperty(const ObjMap& map,
                       Input& input,
                       const TfToken& channel,
                       const T& value,
                       const T& defaultValue = T(0.0f))
{
    if (map.defined) {
        // If the value is zero we don't need a texture, since we know the result will be zero
        if (value == T(0.0f)) {
            input.value = value;
            return true;
        }
        input.image = map.image;
        input.uvIndex = 0;
        input.channel = channel;
        input.wrapS = AdobeTokens->repeat;
        input.wrapT = AdobeTokens->repeat;
        if (value != T(-1) && value != defaultValue) { // different than the default value
            input.scale = makeVec4f(value);
        }
        if (map.origin != GfVec3f(0.0f)) {
            input.uvTranslation = GfVec2f(map.origin[0], map.origin[1]);
        }
        if (map.scale != GfVec3f(1.0f)) {
            input.uvScale = GfVec2f(map.scale[0], map.scale[1]);
        }
        return true;
    } else if (value != T(-1)) { // different than the default value
        // Only emit a value if it differs from the default
        if (value != defaultValue) {
            input.value = value;
        }
        return true;
    }
    return false;
}

void
importEmissive(const ObjMaterial& m,
               InputTranslator& inputTranslator,
               const Input& diffuse,
               Input& emissiveColor)
{
    // Emissive is a bit more complicated, since we have to potentially combine two sources
    // of information:
    // * Ke is material emission and is a color value
    // * glow is a multiplier float value (Adobe extension)
    // If both are present they should be multiplied together, but if only glow is present we
    // multiply it by the base color
    Input Ke;
    importMaterialProperty(m.mapKe, Ke, AdobeTokens->rgb, m.ke);
    Input glow;
    importMaterialProperty(m.mapGlow, glow, AdobeTokens->r, m.glow, -1.0f);
    if (!glow.isEmpty()) {
        if (!glow.isZeroInput()) {
            if (!Ke.isEmpty()) {
                TF_DEBUG_MSG(FILE_FORMAT_OBJ, "  Multiply Ke with glow\n");
                inputTranslator.translateFactor(Ke, glow, emissiveColor);
            } else {
                TF_DEBUG_MSG(FILE_FORMAT_OBJ, "  Multiply diffuse with glow\n");
                inputTranslator.translateFactor(diffuse, glow, emissiveColor);
            }
        } else {
            // If glow is zero we get zero emission
        }
    } else {
        inputTranslator.translateDirect(Ke, emissiveColor);
    }
}

bool
importObj(const ImportObjOptions& options, Obj& obj, UsdData& usd)
{
    // The obj importer collects filenames in the Obj object- add these files to UsdData so that it
    // will be incorporated in the metadata
    for (const std::string filename : obj.importedFilenames) {
        usd.importedFileNames.insert(filename);
    }

    usd.metadata.SetValueAtPath("hasAdobeProperties", VtValue(obj.hasAdobeProperties));
    if (!obj.originalColorSpace.IsEmpty()) {
        usd.metadata.SetValueAtPath(AdobeTokens->originalColorSpace,
                                    VtValue(obj.originalColorSpace));
    }
    if (options.importMaterials) {
        InputTranslator inputTranslator(options.importImages, obj.images, DEBUG_TAG);
        usd.materials.resize(obj.materials.size());
        for (size_t i = 0; i < obj.materials.size(); i++) {
            const ObjMaterial& m = obj.materials[i];
            Material& um = usd.materials[i];
            TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Import material: %s\n", m.name.c_str());
            um.name = m.name;
            Input diffuse;
            Input roughness;
            Input metallic;
            Input specular;
            Input glosiness;
            Input normal;
            Input bump;
            Input opacity;
            Input ior;
            Input transmission;
            Input absorptionColor;
            importMaterialProperty(m.mapKd, diffuse, AdobeTokens->rgb, m.kd);
            bool hasRoughness =
              importMaterialProperty(m.mapRoughness, roughness, AdobeTokens->r, m.roughness);
            bool hasMetallic =
              importMaterialProperty(m.mapMetallic, metallic, AdobeTokens->r, m.metallic);
            if (hasRoughness || hasMetallic) {
                inputTranslator.translateDirect(diffuse, um.diffuseColor);
                inputTranslator.translateDirect(metallic, um.metallic);
                inputTranslator.translateDirect(roughness, um.roughness);
            } else {
                importMaterialProperty(m.mapKs, specular, AdobeTokens->rgb, m.ks);
                importMaterialProperty(m.mapNs, glosiness, importChannel(m.mapNs.channel), m.ns);
                if (options.importPhong) {
                    inputTranslator.translatePhong2PBR(
                      diffuse, specular, glosiness, um.diffuseColor, um.metallic, um.roughness);
                } else {
                    inputTranslator.translateDirect(diffuse, um.diffuseColor);
                }
            }
            importMaterialProperty(m.norm, normal, AdobeTokens->rgb, GfVec3f(-1));
            importMaterialProperty(m.bump, bump, importChannel(m.bump.channel), -1);
            importMaterialProperty(ObjMap(), ior, TfToken(), m.ni, 1.5f);

            importEmissive(m, inputTranslator, diffuse, um.emissiveColor);

            // mapOpacity is a mdl driven map and is a gray scale texture, it can always be read via
            // the red channel
            if (!importMaterialProperty(m.mapOpacity, opacity, AdobeTokens->r, m.d, 1.0f)) {
                importMaterialProperty(m.mapD, opacity, importChannel(m.mapD.channel), m.d, 1.0f);
            }

            inputTranslator.translateNormals(bump, normal, um.normal);
            inputTranslator.translateDirect(opacity, um.opacity);
            inputTranslator.translateDirect(ior, um.ior);

            if (importMaterialProperty(
                  m.mapTranslucence, transmission, AdobeTokens->r, m.translucence)) {
                inputTranslator.translateDirect(transmission, um.transmission);
                // Setup tinting of the translucent parts by the diffuse/base color
                um.absorptionColor = um.diffuseColor;
            }
        }
        usd.images = std::move(inputTranslator.getImages());
    }
    if (options.importGeometry) {
        int current_material = -1;
        bool convertToLinear = (obj.originalColorSpace == AdobeTokens->sRGB);

        // Determine group handling mode
        // Default is "separateGroupsAsMeshes" if empty or unspecified (backwards compatible)
        bool useCombineGroups = options.groupOptions == _groupOptionsTokens->combineGroups;
        bool useSeparateGroupsAsSubsets =
          options.groupOptions == _groupOptionsTokens->separateGroupsAsSubsets;
        bool useSeparateGroupsAsMeshes =
          options.groupOptions.IsEmpty() ||
          options.groupOptions == _groupOptionsTokens->separateGroupsAsMeshes;

        // Warn if an unrecognized groupOptions value was provided and default to
        // separateGroupsAsMeshes
        if (!options.groupOptions.IsEmpty() && !useCombineGroups && !useSeparateGroupsAsSubsets &&
            !useSeparateGroupsAsMeshes) {
            TF_WARN("Unrecognized groupOptions value '%s'. Expected 'combineGroups', "
                    "'separateGroupsAsSubsets', or 'separateGroupsAsMeshes'. Defaulting to "
                    "'separateGroupsAsMeshes'.",
                    options.groupOptions.GetText());
            useSeparateGroupsAsMeshes = true;
        }

        for (const ObjObject& o : obj.objects) {
            auto [nodeIndex, node] = usd.addNode(-1);
            node.name = o.name;

            if (useCombineGroups || useSeparateGroupsAsSubsets) {
                // Combine all groups into a single mesh
                // With separateSubgroups, also create GeomSubsets for each group
                // First, count total sizes to pre-allocate
                size_t totalVertices = 0;
                size_t totalFaces = 0;
                size_t totalIndices = 0;
                size_t totalUvs = 0;
                size_t totalUvIndices = 0;
                size_t totalNormals = 0;
                size_t totalNormalIndices = 0;
                size_t totalColors = 0;
                // Track if any group has these attributes (groups without them get placeholder
                // values)
                bool hasUvs = false;
                bool hasNormals = false;
                bool hasColors = false;

                for (const ObjGroup& g : o.groups) {
                    if (g.faces.empty())
                        continue;
                    totalVertices += g.vertices.size();
                    totalFaces += g.faces.size();
                    totalIndices += g.indices.size();
                    if (g.uvs.size()) {
                        hasUvs = true;
                        totalUvs += g.uvs.size();
                        totalUvIndices += g.uvIndices.size();
                    }
                    if (g.normals.size()) {
                        hasNormals = true;
                        totalNormals += g.normals.size();
                        totalNormalIndices += g.normalIndices.size();
                    }
                    if (g.colors.size()) {
                        hasColors = true;
                        totalColors += g.colors.size();
                    }
                }

                // Skip if no geometry
                if (totalFaces == 0) {
                    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                                 "Skipping object %s - no faces after combining groups\n",
                                 o.name.c_str());
                    continue;
                }

                auto [meshIndex, mesh] = usd.addMesh();
                node.staticMeshes.push_back(meshIndex);

                // Leave name empty - uniquifyNames() will set it to "Mesh" as the default, since
                // all meshes are combined into a single mesh
                mesh.doubleSided = true;

                // Pre-allocate
                mesh.points.reserve(totalVertices);
                mesh.faces.reserve(totalFaces);
                mesh.indices.reserve(totalIndices);
                if (hasUvs) {
                    // Reserve +1 for placeholder UV at index 0 (used by groups without UVs)
                    mesh.uvs.values.reserve(totalUvs + 1);
                    mesh.uvs.indices.reserve(totalUvIndices);
                    // Add placeholder UV at index 0 for groups that don't have UVs
                    mesh.uvs.values.push_back(GfVec2f(0.0f, 0.0f));
                }
                if (hasNormals) {
                    // Reserve +1 for placeholder normal at index 0 (used by groups without normals)
                    mesh.normals.values.reserve(totalNormals + 1);
                    mesh.normals.indices.reserve(totalNormalIndices);
                    // Add placeholder normal at index 0 for groups that don't have normals
                    mesh.normals.values.push_back(GfVec3f(0.0f, 1.0f, 0.0f));
                }

                VtVec3fArray combinedColors;
                if (hasColors) {
                    combinedColors.reserve(totalColors);
                }

                // Track offsets for index remapping
                int vertexOffset = 0;
                int uvOffset = hasUvs ? 1 : 0; // Start at 1 if we added a placeholder UV
                int normalOffset =
                  hasNormals ? 1 : 0; // Start at 1 if we added a placeholder normal
                int faceOffset = 0;

                // Track which faces belong to each group (for creating GeomSubsets)
                struct GroupFaceRange
                {
                    int startFace;
                    int faceCount;
                    int material;
                };
                std::vector<GroupFaceRange> groupFaceRanges;

                // Combine all groups
                for (const ObjGroup& g : o.groups) {
                    if (g.faces.empty())
                        continue;

                    // Track group face range for subset creation
                    if (useSeparateGroupsAsSubsets) {
                        groupFaceRanges.push_back(
                          { faceOffset, static_cast<int>(g.faces.size()), g.material });
                    }

                    // Append vertices (using push_back for USD version compatibility)
                    for (const auto& v : g.vertices) {
                        mesh.points.push_back(v);
                    }

                    // Append faces
                    for (const auto& f : g.faces) {
                        mesh.faces.push_back(f);
                    }

                    // Append indices with offset
                    for (int idx : g.indices) {
                        mesh.indices.push_back(idx + vertexOffset);
                    }

                    // Append UVs if present
                    if (hasUvs) {
                        if (g.uvs.size()) {
                            for (const auto& uv : g.uvs) {
                                mesh.uvs.values.push_back(uv);
                            }
                            for (int idx : g.uvIndices) {
                                mesh.uvs.indices.push_back(idx + uvOffset);
                            }
                            uvOffset += g.uvs.size();
                        } else {
                            // Group has no UVs but others do - add placeholder indices
                            for (size_t i = 0; i < g.indices.size(); i++) {
                                mesh.uvs.indices.push_back(0);
                            }
                        }
                    }

                    // Append normals if present
                    if (hasNormals) {
                        if (g.normals.size()) {
                            for (const auto& n : g.normals) {
                                mesh.normals.values.push_back(n);
                            }
                            for (int idx : g.normalIndices) {
                                mesh.normals.indices.push_back(idx + normalOffset);
                            }
                            normalOffset += g.normals.size();
                        } else {
                            // Group has no normals but others do - add placeholder indices
                            for (size_t i = 0; i < g.indices.size(); i++) {
                                mesh.normals.indices.push_back(0);
                            }
                        }
                    }

                    // Append colors if present
                    if (hasColors) {
                        if (g.colors.size()) {
                            if (convertToLinear) {
                                for (const auto& c : g.colors) {
                                    combinedColors.push_back(GfVec3f(
                                      srgbToLinear(c[0]), srgbToLinear(c[1]), srgbToLinear(c[2])));
                                }
                            } else {
                                for (const auto& c : g.colors) {
                                    combinedColors.push_back(c);
                                }
                            }
                        } else {
                            // Group has no colors but others do - add default white
                            for (size_t i = 0; i < g.vertices.size(); i++) {
                                combinedColors.push_back(GfVec3f(1.0f, 1.0f, 1.0f));
                            }
                        }
                    }

                    // Track material for the combined mesh
                    if (g.material >= 0) {
                        current_material = g.material;
                    }

                    vertexOffset += g.vertices.size();
                    faceOffset += g.faces.size();
                }

                // Set interpolation modes
                if (hasUvs) {
                    mesh.uvs.interpolation = UsdGeomTokens->faceVarying;
                }
                if (hasNormals) {
                    mesh.normals.interpolation = UsdGeomTokens->faceVarying;
                }
                if (hasColors) {
                    auto [colorSetIndex, color] = usd.addColorSet(meshIndex);
                    color.values = std::move(combinedColors);
                    color.interpolation = UsdGeomTokens->vertex;
                }

                // Create GeomSubsets for separateGroups mode
                if (useSeparateGroupsAsSubsets && groupFaceRanges.size() > 1) {
                    for (const auto& range : groupFaceRanges) {
                        auto [subsetIndex, subset] = usd.addSubset(meshIndex);
                        subset.material = range.material;
                        // Create face indices for this subset
                        subset.faces.resize(range.faceCount);
                        for (int i = 0; i < range.faceCount; i++) {
                            subset.faces[i] = range.startFace + i;
                        }
                    }
                    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                                 "Created %zu subsets for mesh '%s': %zu verts, %zu faces\n",
                                 groupFaceRanges.size(),
                                 mesh.name.c_str(),
                                 mesh.points.size(),
                                 mesh.faces.size());
                } else {
                    // Set material (use the last encountered material for the combined mesh)
                    mesh.material = current_material;
                    TF_DEBUG_MSG(
                      FILE_FORMAT_OBJ,
                      "Combined %zu groups into single mesh '%s': %zu verts, %zu faces\n",
                      o.groups.size(),
                      mesh.name.c_str(),
                      mesh.points.size(),
                      mesh.faces.size());
                }

            } else if (useSeparateGroupsAsMeshes) {
                // Legacy behavior: create a mesh per group
                for (const ObjGroup& g : o.groups) {
                    // Skip empty groups
                    if (g.faces.empty()) {
                        TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                                     "Skipping empty group %s on node %s - %zu verts, %zu faces, "
                                     "%zu indices\n",
                                     g.name.c_str(),
                                     node.name.c_str(),
                                     g.vertices.size(),
                                     g.faces.size(),
                                     g.indices.size());
                        continue;
                    }
                    auto [meshIndex, mesh] = usd.addMesh();
                    node.staticMeshes.push_back(meshIndex);

                    mesh.name = g.name;
                    mesh.doubleSided = true;
                    mesh.faces = g.faces;
                    mesh.indices = g.indices;
                    mesh.points = g.vertices;
                    if (g.uvs.size()) {
                        mesh.uvs.indices = g.uvIndices;
                        mesh.uvs.values = g.uvs;
                        mesh.uvs.interpolation = UsdGeomTokens->faceVarying;
                    }
                    if (g.normals.size()) {
                        mesh.normals.indices = g.normalIndices;
                        mesh.normals.values = g.normals;
                        mesh.normals.interpolation = UsdGeomTokens->faceVarying;
                    }
                    if (g.colors.size()) {
                        auto [colorSetIndex, color] = usd.addColorSet(meshIndex);
                        if (convertToLinear) {
                            VtVec3fArray mutableColors = g.colors;
                            for (auto& c : mutableColors) {
                                c[0] = srgbToLinear(c[0]);
                                c[1] = srgbToLinear(c[1]);
                                c[2] = srgbToLinear(c[2]);
                            }
                            color.values = mutableColors;
                        } else {
                            color.values = g.colors;
                        }
                        color.interpolation = UsdGeomTokens->vertex;
                    }
                    // Set extent.
                    // SetExtent(vertexValues, mesh);
                    if (g.subsets.size() == 0) {
                        mesh.material = g.material;
                    } else if (g.subsets.size() == 1 &&
                               g.faces.size() == g.subsets[0].faces.size()) {
                        mesh.material = g.subsets.back().material;
                    } else {
                        for (const ObjSubset& s : g.subsets) {
                            //    const auto& material = obj.materials[s.material];
                            auto [subsetIndex, subset] = usd.addSubset(meshIndex);
                            subset.material = s.material;
                            subset.faces = s.faces;
                        }
                    }
                    if (mesh.material < 0) {
                        mesh.material = current_material;
                    }
                    current_material = mesh.material;
                }
            }
        }
    }
    return true;
}

}