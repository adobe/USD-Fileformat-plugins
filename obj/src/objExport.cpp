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
#include "objExport.h"
#include "debugCodes.h"
#include <fileformatutils/common.h>
#include <fileformatutils/images.h>
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

using namespace PXR_NS;

namespace adobe::usd {

struct ExportCache
{
    std::vector<std::string> materialNames;
};

template<typename T>
void
writeObjMaterialValue(T& objValue, const Input& input)
{
    if (input.value.IsHolding<T>()) {
        objValue = input.value.Get<T>();
    }
}

void
writeObjMap(const UsdData& usd, ObjMap& map, const Input& input)
{
    if (input.image >= 0) {
        const ImageAsset& image = usd.images[input.image];
        map.defined = true;
        map.filename = image.uri;
        map.image = input.image;

        // XXX note that mtl doesn't support uv rotation so we only handle translation and scale
        if (input.transformScale.IsHolding<GfVec2f>()) {
            GfVec2f scale = input.transformScale.UncheckedGet<GfVec2f>();
            map.scale = GfVec3f(scale[0], scale[1], 1.0f);
        }
        if (input.transformTranslation.IsHolding<GfVec2f>()) {
            GfVec2f trans = input.transformTranslation.UncheckedGet<GfVec2f>();
            map.origin = GfVec3f(trans[0], trans[1], 0.0f);
        }
    }
}

void
exportMesh(Obj& obj,
           const UsdData& usd,
           const GfMatrix4d& worldTransform,
           int nodeIndex,
           int meshIndex)
{
    ObjObject& o = obj.objects.back();
    o.groups.push_back(ObjGroup());
    ObjGroup& g = o.groups.back();
    const Mesh& m = usd.meshes[meshIndex];
    bool doSRGBConversion = shouldConvertToSRGB(usd, obj.outputColorSpace);
    g.name = m.name.empty()
               ? "Node_" + std::to_string(nodeIndex) + "_Mesh_" + std::to_string(meshIndex)
               : m.name;
    g.material = m.material;
    g.faces = m.faces;
    g.indices = m.indices;
    if (m.uvs.indices.size()) {
        g.uvIndices = m.uvs.indices;
    } else if (m.uvs.values.size() == m.points.size()) {
        g.uvIndices = g.indices;
    } else if (m.uvs.values.size() == m.indices.size()) {
        g.uvIndices.resize(m.indices.size());
        std::iota(g.uvIndices.begin(), g.uvIndices.end(), 0);
    }
    if (m.normals.indices.size()) {
        g.normalIndices = m.normals.indices;
    } else if (m.normals.values.size() == m.points.size()) {
        g.normalIndices = g.indices;
    } else if (m.normals.values.size() == m.indices.size()) {
        g.normalIndices.resize(m.indices.size());
        std::iota(g.normalIndices.begin(), g.normalIndices.end(), 0);
    }
    g.vertices = m.points;
    for (GfVec3f& v : g.vertices) {
        v = GfVec3f(worldTransform.Transform(v));
    }
    if (m.colors.size()) {
        const Primvar<GfVec3f>& color = m.colors[0]; // only export first color set
        if (color.indices.size() == 0 && color.values.size() == m.points.size()) {
            g.colors = color.values;
            // Perform color space conversion if necessary
            if (doSRGBConversion) {
                for (GfVec3f& c : g.colors) {
                    c[0] = linearToSRGB(c[0]);
                    c[1] = linearToSRGB(c[1]);
                    c[2] = linearToSRGB(c[2]);
                }
            }
        } else {
            TF_DEBUG_MSG(FILE_FORMAT_OBJ, "obj::write color indexing unsupported\n");
        }
        if (m.colors.size() > 1) {
            TF_WARN(FILE_FORMAT_OBJ,
                    "obj::write more than 1 color set found, exporting only the first.\n");
        }
    }
    g.uvs = m.uvs.values;
    g.normals = m.normals.values;
    auto normalTransform = worldTransform.GetInverse().GetTranspose();
    for (GfVec3f& v : g.normals) {
        v = GfVec3f(normalTransform.TransformDir(v));
        v.Normalize();
    }
    if (m.subsets.size()) {
        for (const Subset& usdSubset : m.subsets) {
            g.subsets.push_back(ObjSubset());
            ObjSubset& s = g.subsets.back();
            s.material = usdSubset.material;
            s.faces = usdSubset.faces;
        }
    } else {
        g.subsets.push_back(ObjSubset());
        ObjSubset& s = g.subsets.back();
        s.material = m.material;
        s.faces.resize(m.faces.size());
        std::iota(s.faces.begin(), s.faces.end(), 0);
    }
    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                 "obj::write group %s { faces: %zu, vIdx: %zu, vtIdx: %zu, vnIdx: %zu, v: %zu, vt: "
                 "%zu, vn: %zu, mat: %d }\n",
                 g.name.c_str(),
                 g.faces.size(),
                 g.indices.size(),
                 g.uvIndices.size(),
                 g.normalIndices.size(),
                 g.vertices.size(),
                 g.uvs.size(),
                 g.normals.size(),
                 g.material);
}

void
writeNode(Obj& obj, const UsdData& usd, size_t nodeIndex, const GfMatrix4d& correctionTransform)
{
    const Node& n = usd.nodes[nodeIndex];
    GfMatrix4d finalWorldTransform = n.worldTransform * correctionTransform;
    for (const auto& [skeletonIndex, meshIndices] : n.skinnedMeshes) {
        for (int meshIndex : meshIndices) {
            exportMesh(obj, usd, finalWorldTransform, nodeIndex, meshIndex);
        }
    }
    for (int meshIndex : n.staticMeshes) {
        exportMesh(obj, usd, finalWorldTransform, nodeIndex, meshIndex);
    }
    for (size_t i = 0; i < n.children.size(); i++) {
        writeNode(obj, usd, n.children[i], correctionTransform);
    }
}

bool
exportObj(const ExportObjOptions& options, const UsdData& usd, Obj& obj)
{
    GfMatrix4d correctionTransform(1);
    if (usd.upAxis == UsdGeomTokens->z) {
        correctionTransform.SetRotate(GfQuatd(0.7071068, -0.7071068, 0, 0)); // rotate -90 deg in x
        TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                     "obj::write correct rotation { rotX: %s }\n",
                     usd.upAxis == UsdGeomTokens->z ? "-90deg" : "0deg");
    }

    obj.comments.push_back("# Meters per unit: " + TfStringify(usd.metersPerUnit));

    const std::string name = TfStringGetBeforeSuffix(TfGetBaseName(options.filename));

    obj.images.resize(usd.images.size());
    for (size_t i = 0; i < usd.images.size(); i++) {
        const ImageAsset& usdImage = usd.images[i];
        ImageAsset& image = obj.images[i];
        image.name = usdImage.name;
        image.uri = usdImage.uri;
        image.format = usdImage.format;
        image.image = usdImage.image;
    }

    if (!usd.materials.empty()) {
        obj.libraries.push_back(ObjMaterialLibrary());
        ObjMaterialLibrary& library = obj.libraries.back();
        library.filename = name + ".mtl";

        obj.materials.resize(usd.materials.size());
        library.materials.resize(usd.materials.size());

        // Use the UniqueNameEnforcer to create a new list of unique material names.
        UniqueNameEnforcer uniqueMaterialNameEnforcer;
        std::vector<std::string> uniqueNames;
        uniqueNames.reserve(usd.materials.size());
        for (auto& m : usd.materials) {
            uniqueNames.push_back(m.name);
            uniqueMaterialNameEnforcer.enforceUniqueness(uniqueNames.back());
        }

        for (size_t i = 0; i < usd.materials.size(); i++) {
            const Material& m = usd.materials[i];
            ObjMaterial& om = obj.materials[i];
            library.materials[i] = i;
            om.name = uniqueNames[i];
            writeObjMaterialValue(om.kd, m.diffuseColor);
            writeObjMaterialValue(om.ni, m.ior);
            writeObjMaterialValue(om.d, m.opacity);
            writeObjMap(usd, om.mapKd, m.diffuseColor);
            writeObjMap(usd, om.norm, m.normal);
            writeObjMap(usd, om.mapD, m.opacity);
            writeObjMap(usd, om.disp, m.displacement);
            // Note, the code above is only writing a small subset of the overall material model
            // to the MTL library. This should be expanded.
            // We'll keep the snippet below in case we want to use it again with a reworked material
            // export.
            // if (m.useSpecularWorkflow.valued && m.useSpecularWorkflow.value) {
            //     writeObjMaterialValue(om.ks, m.specularColor);
            //     writeObjMap(usd, om.mapKs, m.specularColor);
            // } else {
            //     writeObjMap(usd, om.mapKs, m.metallic);
            // }
        }
    }

    if (usd.meshes.size()) {
        obj.objects.push_back(ObjObject()); // only 1 object, each mesh corresponds to a group
        ObjObject& o = obj.objects.back();
        o.name = "Object_0";
        for (size_t i = 0; i < usd.rootNodes.size(); i++) {
            writeNode(
              obj, usd, usd.rootNodes[i], correctionTransform); // we find meshes in leaf nodes
        }
    }
    return true;
}

}
