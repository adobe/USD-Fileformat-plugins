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
            input.transformTranslation = GfVec2f(map.origin[0], map.origin[1]);
        }
        if (map.scale != GfVec3f(1.0f)) {
            input.transformScale = GfVec2f(map.scale[0], map.scale[1]);
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
        for (const ObjObject& o : obj.objects) {
            auto [nodeIndex, node] = usd.addNode(-1);
            node.name = o.name;

            for (const ObjGroup& g : o.groups) {
                // Skip empty groups
                if (g.faces.empty()) {
                    TF_DEBUG_MSG(
                      FILE_FORMAT_OBJ,
                      "Skipping empty group %s on node %s - %zu verts, %zu faces, %zu indices\n",
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
                } else if (g.subsets.size() == 1 && g.faces.size() == g.subsets[0].faces.size()) {
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
    return true;
}

}