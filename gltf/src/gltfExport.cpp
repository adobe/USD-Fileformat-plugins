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
#include "gltfExport.h"
#include "debugCodes.h"
#include "neuralAssetsHelper.h"
#include <common.h>
#include <geometry.h>
#include <images.h>
#include <materials.h>
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
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/utils.h>

// TODO refine this description
/**
 * Export usd to gltf.
 *
 * Scene settings:
 * glTF always has upAxis = +y and units are in meters, and there is no specific property to change
 * that, in contrast to USD's upAxis and metersPerUnit tokens. So we instead add a correction node
 * at the root of the glTF node hierarchy to adjust for that.
 *
 * Materials:
 * Cameras:
 * Meshes:
 *
 *
 *
 *
 *
 *
 */

using namespace PXR_NS;

namespace adobe::usd {

struct ExportGltfContext
{
    ExportGltfOptions options;
    UsdData* usd;
    tinygltf::Model* gltf;
    // Any GLTF extensions used should be added here and marked as required if needed
    // These will be written to the GLTF model in the end, but the set is more efficient for adding
    // things only once.
    std::unordered_set<std::string> extensionsUsed;
    std::unordered_set<std::string> extensionsRequired;
    // Maps USD meshes to 1 or more glTF primitives.
    // If a USD mesh has no subsets, the USD mesh is mapped to a single glTF primitive.
    // If a USD mesh has subsets, each subset maps to a glTF primitive.
    std::vector<std::vector<tinygltf::Primitive>> primitiveMap;

    // Map used to detect mesh instancing
    std::unordered_map<int, int> usdMeshIndexToGltfMeshIndexMap;

    // Maps skeleton index to a list of node indexes that are roots (ie. nodes with skinned meshes).
    // This is used to map skeletons to multiple meshes
    std::unordered_map<int, std::vector<int>> skeletonsToSkelRootsMap;
};

using ExtMap = tinygltf::ExtensionMap;

void
addExtension(ExportGltfContext& ctx,
             tinygltf::ExtensionMap& extensionMap,
             const std::string& extensionName,
             const ExtMap& ext,
             bool addToRequired = false)
{
    extensionMap[extensionName] = tinygltf::Value(ext);
    ctx.extensionsUsed.insert(extensionName);
    if (addToRequired) {
        ctx.extensionsRequired.insert(extensionName);
    }
}

void
exportMetadata(ExportGltfContext ctx)
{
    std::set<std::string> ignoredProperties = { "filenames", "hasAdobeProperties" };
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "glTF::write metadata: {\n");
    std::map<std::string, tinygltf::Value> extras;
    for (auto it = ctx.usd->metadata.begin(); it != ctx.usd->metadata.end(); it++) {
        if (ignoredProperties.find(it->first) != ignoredProperties.end()) {
            continue;
        }
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "   %s: ", it->first.c_str());
        if (it->second.IsHolding<bool>()) {
            bool x = it->second.UncheckedGet<bool>();
            extras[it->first] = tinygltf::Value(x);
            TF_DEBUG_MSG(FILE_FORMAT_GLTF, "%s\n", x ? "true" : "false");
        } else if (it->second.IsHolding<int>()) {
            int x = it->second.UncheckedGet<int>();
            extras[it->first] = tinygltf::Value(x);
            TF_DEBUG_MSG(FILE_FORMAT_GLTF, "%s\n", std::to_string(x).c_str());
        } else if (it->second.IsHolding<float>()) {
            float x = it->second.UncheckedGet<float>();
            extras[it->first] = tinygltf::Value(x);
            TF_DEBUG_MSG(FILE_FORMAT_GLTF, "%s\n", std::to_string(x).c_str());
        } else if (it->second.IsHolding<std::string>()) {
            const std::string& x = it->second.UncheckedGet<std::string>();
            extras[it->first] = tinygltf::Value(x);
            TF_DEBUG_MSG(FILE_FORMAT_GLTF, "%s\n", x.c_str());
        } else {
            TF_DEBUG_MSG(FILE_FORMAT_GLTF, "unsupported type not exported");
        }
    }
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "}\n");
    ctx.gltf->asset.extras = tinygltf::Value(extras);
}

// Returns the index of the offset node, otherwise -1
int
exportOffsetNode(tinygltf::Model& model, TfToken upAxis, float metersPerUnit)
{

    if (upAxis == UsdGeomTokens->z || (metersPerUnit != 1 && metersPerUnit > 0)) {
        int nodeIndex = model.nodes.size();
        model.nodes.push_back(tinygltf::Node());
        tinygltf::Node& node = model.nodes[nodeIndex];
        node.name = "correctionNode";
        if (upAxis == UsdGeomTokens->z) {
            node.rotation = { -0.7071068, 0, 0, 0.7071068 }; // rotate -90 deg in x
        }
        // If metersPerUnit is not initialized (ie. equals 0), we don't want to apply
        // a scale factor
        if (metersPerUnit != 1 && metersPerUnit > 0) {
            float scale = metersPerUnit;
            node.scale = std::vector<double>{ scale, scale, scale };
        }
        TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                     "gltf::write node { %s, rotX: %s, metersPerUnit: %f }\n",
                     node.name.c_str(),
                     upAxis == UsdGeomTokens->z ? "-90deg" : "0deg",
                     metersPerUnit);
        return nodeIndex;
    }

    return -1;
}

bool
nodeContainsNgp(ExportGltfContext& ctx, std::int32_t index)
{
    const Node& n = ctx.usd->nodes[index];
    if (n.ngp >= 0)
        return true;

    for (std::int32_t childIndex : n.children) {
        if (nodeContainsNgp(ctx, childIndex))
            return true;
    }

    return false;
}

int
exportCamera(ExportGltfContext& ctx, int camera)
{
    const Camera& usdCamera = ctx.usd->cameras[camera];
    int cameraIndex = ctx.gltf->cameras.size();
    ctx.gltf->cameras.push_back(tinygltf::Camera());
    tinygltf::Camera& gCamera = ctx.gltf->cameras[cameraIndex];
    gCamera.name = usdCamera.name;
    const GfCamera& uCamera = usdCamera.camera;
    float znear = usdCamera.nearZ;
    float zfar = usdCamera.farZ;
    if (usdCamera.projection == GfCamera::Projection::Perspective) {
        gCamera.type = "perspective";
        gCamera.perspective.znear = znear;
        gCamera.perspective.zfar = zfar;
        gCamera.perspective.aspectRatio = usdCamera.horizontalAperture / usdCamera.verticalAperture;
        gCamera.perspective.yfov = uCamera.GetFieldOfView(GfCamera::FOVVertical) * deg2rad;
    } else {
        gCamera.type = "orthographic";
        gCamera.orthographic.xmag = usdCamera.horizontalAperture * GfCamera::APERTURE_UNIT;
        gCamera.orthographic.ymag = usdCamera.verticalAperture * GfCamera::APERTURE_UNIT;
        gCamera.orthographic.znear = znear;
        gCamera.orthographic.zfar = zfar;
    }
    return cameraIndex;
}

void
exportNgpExtension(ExportGltfContext& ctx,
                   int ngpIndex,
                   tinygltf::Value::Object& gngpObj,
                   std::vector<double>& restTransform)
{
    // Refer to README_NGP.md for documentation.

    auto exportUncompressedFloatArray = [&gngpObj](const char* name,
                                                   const PXR_NS::VtFloatArray& src,
                                                   std::size_t d1 = 0,
                                                   std::size_t d2 = 0) {
        std::string b64Str;
        if (d1 == 0 || d2 == 0) {
            packBase64String(reinterpret_cast<const uint8_t*>(src.data()),
                             src.size() * sizeof(float),
                             false,
                             b64Str);
        } else {
            std::vector<uint8_t> data(src.size() * sizeof(float));
            packMLPWeight(src.data(), reinterpret_cast<float*>(data.data()), d1, d2);
            packBase64String(data.data(), data.size(), false, b64Str);
        }
        gngpObj[name] = tinygltf::Value(b64Str);
        tinygltf::Value::Array shapeArray = { tinygltf::Value(static_cast<int>(src.size())) };
        gngpObj[std::string(name) + "_shape"] = tinygltf::Value(shapeArray);
    };

    const NgpData& ngpData = ctx.usd->ngps[ngpIndex];
    // The numbers below indicate the shapes of the multilayer perceptron (MLP).
    exportUncompressedFloatArray("spatial_mlp_l0_weight", ngpData.densityMlpLayer0Weight, 24, 32);
    exportUncompressedFloatArray("spatial_mlp_l0_bias", ngpData.densityMlpLayer0Bias);
    exportUncompressedFloatArray("spatial_mlp_l1_weight", ngpData.densityMlpLayer1Weight, 16, 24);
    exportUncompressedFloatArray("spatial_mlp_l1_bias", ngpData.densityMlpLayer1Bias);
    exportUncompressedFloatArray("vdep_mlp_l0_weight", ngpData.colorMlpLayer0Weight, 24, 36);
    exportUncompressedFloatArray("vdep_mlp_l0_bias", ngpData.colorMlpLayer0Bias);
    exportUncompressedFloatArray("vdep_mlp_l1_weight", ngpData.colorMlpLayer1Weight, 24, 24);
    exportUncompressedFloatArray("vdep_mlp_l1_bias", ngpData.colorMlpLayer1Bias);
    exportUncompressedFloatArray("vdep_mlp_l2_weight", ngpData.colorMlpLayer2Weight, 4, 24);
    exportUncompressedFloatArray("vdep_mlp_l2_bias", ngpData.colorMlpLayer2Bias);

    std::vector<uint8_t> bufHashGrid(ngpData.hashGrid.size() * sizeof(std::uint16_t));
    float32ToFloat16(ngpData.hashGrid.data(),
                     reinterpret_cast<std::uint16_t*>(bufHashGrid.data()),
                     ngpData.hashGrid.size());
    std::string b64StrHashGrid;
    packBase64String(bufHashGrid.data(), bufHashGrid.size(), true, b64StrHashGrid);
    gngpObj["hash_grid"] = tinygltf::Value(b64StrHashGrid);

    tinygltf::Value::Array hashGridResArray(ngpData.hashGridResolution.size());
    for (size_t i = 0; i < hashGridResArray.size(); ++i) {
        hashGridResArray[i] = tinygltf::Value(static_cast<int>(ngpData.hashGridResolution[i]));
    }
    gngpObj["hash_grid_res"] = tinygltf::Value(hashGridResArray);

    // The numbers indicate the shape of the hash grid, meaning there are 8 levels, 524288 entrys
    // per level, and 4 channels per value.
    tinygltf::Value::Array hashGridShapeArray = { tinygltf::Value(8),
                                                  tinygltf::Value(524288),
                                                  tinygltf::Value(4) };
    gngpObj["hash_grid_shape"] = tinygltf::Value(hashGridShapeArray);

    std::vector<std::uint8_t> bufDistanceGrid(ngpData.distanceGrid.size());
    float maxDistance = maxOfFloatArray(ngpData.distanceGrid.data(), ngpData.distanceGrid.size());
    for (size_t i = 0; i < bufDistanceGrid.size(); ++i) {
        bufDistanceGrid[i] = static_cast<std::uint8_t>(
          std::clamp(std::sqrt(ngpData.distanceGrid[i] / maxDistance) * 255.0f, 0.0f, 255.0f));
    }
    std::string b64StrDistanceGrid;
    packBase64String(bufDistanceGrid.data(), bufDistanceGrid.size(), true, b64StrDistanceGrid);
    gngpObj["distance_grid"] = tinygltf::Value(b64StrDistanceGrid);
    gngpObj["distance_max"] = tinygltf::Value(maxDistance);

    // The shape of distance grid is hard-coded as 128^3. Refer to README_NGP.md for more details.
    tinygltf::Value::Array distanceShapeArray = { tinygltf::Value(128),
                                                  tinygltf::Value(128),
                                                  tinygltf::Value(128) };
    gngpObj["distance_grid_shape"] = tinygltf::Value(distanceShapeArray);

    std::vector<std::uint8_t> bufDensityGrid(ngpData.densityGrid.size());
    float maxDensity = maxOfFloatArray(ngpData.densityGrid.data(), ngpData.densityGrid.size());
    for (size_t i = 0; i < bufDensityGrid.size(); ++i) {
        bufDensityGrid[i] = static_cast<std::uint8_t>(
          std::clamp(ngpData.densityGrid[i] / maxDensity * 255.0f, 0.0f, 255.0f));
    }
    std::string b64StrDensityGrid;
    packBase64String(bufDensityGrid.data(), bufDensityGrid.size(), true, b64StrDensityGrid);
    gngpObj["density"] = tinygltf::Value(b64StrDensityGrid);
    gngpObj["density_max"] = tinygltf::Value(maxDensity);
    gngpObj["sigma_threshold"] = tinygltf::Value(ngpData.densityThreshold);

    // The shape of density grid is hard-coded as 512^3. Refer to README_NGP.md for more details.
    tinygltf::Value::Array densityShapeArray = { tinygltf::Value(512),
                                                 tinygltf::Value(512),
                                                 tinygltf::Value(512) };
    gngpObj["density_shape"] = tinygltf::Value(densityShapeArray);

    auto transMatrix = GfMatrix4d(GfRotation(GfVec3d(1.0, 0.0, 0.0), 90.0), GfVec3d(0.0, 0.0, 0.0));
    if (ngpData.hasTransform) {
        transMatrix *= ngpData.transform;
    }

    auto diffMatrix = transMatrix - GfMatrix4d(1.0);
    if (infNormOfFloatArray(diffMatrix.data(), 16) > std::numeric_limits<double>::epsilon()) {
        if (restTransform.size()) {
            GfMatrix4d totalTransform;
            copyMatrix(restTransform, totalTransform);
            totalTransform *= transMatrix;
            copyMatrix(totalTransform, restTransform);
        } else {
            copyMatrix(transMatrix, restTransform);
        }
    }
}

size_t
createGltfMesh(ExportGltfContext& ctx, const Node& node)
{
    // If there are multiple usd meshes, we create one gltf mesh but add all the primitives
    // of all the usd meshes to the single gltf mesh
    size_t meshIndex = ctx.gltf->meshes.size();
    ctx.gltf->meshes.push_back(tinygltf::Mesh());
    tinygltf::Mesh& gmesh = ctx.gltf->meshes[meshIndex];
    for (int usdMeshIndex : node.staticMeshes) {
        // Primitives previously written to ctx.primitiveMap
        std::vector<tinygltf::Primitive>& primitives = ctx.primitiveMap[usdMeshIndex];
        for (size_t j = 0; j < primitives.size(); j++) {
            gmesh.primitives.push_back(primitives[j]);
        }
    }
    return meshIndex;
}

int
exportNode(ExportGltfContext& ctx, const Node& node, int offset)
{
    int nodeIndex = ctx.gltf->nodes.size();
    ctx.gltf->nodes.push_back(tinygltf::Node());
    tinygltf::Node& gnode = ctx.gltf->nodes[nodeIndex];

    TF_DEBUG_MSG(
      FILE_FORMAT_GLTF, "glTF::write node: { %s } path=%s\n", node.name.c_str(), node.path.c_str());
    gnode.name = node.name;

    bool hasAnimation =
      node.translations.times.size() || node.rotations.times.size() || node.scales.times.size();

    // from the glTF spec: "When a node is targeted for animation (referenced by an
    // animation.channel.target), only TRS properties MAY be present; matrix MUST NOT be present."
    if (node.hasTransform && !hasAnimation) {
        copyMatrix(node.transform, gnode.matrix);
    }
    if (node.camera != -1) {
        gnode.camera = exportCamera(ctx, node.camera);
    }
    if (node.ngp != -1) {
        tinygltf::Value::Object nerfExt;
        exportNgpExtension(ctx, node.ngp, nerfExt, gnode.matrix);
        addExtension(ctx, gnode.extensions, getNerfExtString(), nerfExt, true);
    }
    if (node.staticMeshes.size()) {
        // Skinned meshes are written in exportSkeletons, process only staticMeshes here.
        if (node.staticMeshes.size() == 1) {
            // If there is only one usd mesh, we can use the same gltf mesh index as an instanced
            // mesh. We check if there is an entry in the map of usd mesh index to gltf mesh index.
            // If there isn't an entry, we need to create the gltf mesh from the usd mesh.
            int usdMeshIndex = node.staticMeshes[0];
            auto it = ctx.usdMeshIndexToGltfMeshIndexMap.find(usdMeshIndex);
            if (it == ctx.usdMeshIndexToGltfMeshIndexMap.end()) {
                size_t meshIndex = createGltfMesh(ctx, node);
                gnode.mesh = meshIndex;
                // Add a mapping of usd mesh index to gltf mesh index of possible re-use
                ctx.usdMeshIndexToGltfMeshIndexMap[usdMeshIndex] = meshIndex;
            } else {
                // We've already created the gltf mesh for the usd mesh so we can instance the gltf
                // mesh
                gnode.mesh = it->second;
            }
        } else {
            // When there are multiple static meshes, we combine them into one mesh but this
            // is not common so we don't support instancing
            gnode.mesh = createGltfMesh(ctx, node);
        }
    }
    if (offset) {
        gnode.children.resize(node.children.size());
        for (size_t i = 0; i < node.children.size(); i++) {
            gnode.children[i] = node.children[i] + offset;
        }
    } else {
        gnode.children = node.children;
    }

    // Add mapping of skeletonIndex to node with skinned meshes
    for (const auto& [skeletonIndex, _] : node.skinnedMeshes) {
        ctx.skeletonsToSkelRootsMap[skeletonIndex].push_back(nodeIndex);
    }

    if (hasAnimation) {
        tinygltf::Animation animation;
        if (node.translations.times.size()) {
            int timeAccessor = addAccessor(ctx.gltf,
                                           "times",
                                           0,
                                           TINYGLTF_TYPE_SCALAR,
                                           TINYGLTF_COMPONENT_TYPE_FLOAT,
                                           node.translations.times.size(),
                                           node.translations.times.data(),
                                           true);
            int translationAccessor = addAccessor(ctx.gltf,
                                                  "translations",
                                                  0,
                                                  TINYGLTF_TYPE_VEC3,
                                                  TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                  node.translations.values.size(),
                                                  node.translations.values.data(),
                                                  false);
            tinygltf::AnimationSampler sampler;
            sampler.input = timeAccessor;
            sampler.output = translationAccessor;
            sampler.interpolation = "LINEAR";
            int samplerIndex = animation.samplers.size();
            animation.samplers.push_back(sampler);
            tinygltf::AnimationChannel channel;
            channel.sampler = samplerIndex;
            channel.target_node = nodeIndex;
            channel.target_path = "translation";
            animation.channels.push_back(channel);
        }
        if (node.rotations.times.size()) {
            int timeAccessor = addAccessor(ctx.gltf,
                                           "times",
                                           0,
                                           TINYGLTF_TYPE_SCALAR,
                                           TINYGLTF_COMPONENT_TYPE_FLOAT,
                                           node.rotations.times.size(),
                                           node.rotations.times.data(),
                                           true);
            int rotationAccessor = addAccessor(ctx.gltf,
                                               "rotations",
                                               0,
                                               TINYGLTF_TYPE_VEC4,
                                               TINYGLTF_COMPONENT_TYPE_FLOAT,
                                               node.rotations.values.size(),
                                               node.rotations.values.data(),
                                               false);
            tinygltf::AnimationSampler sampler;
            sampler.input = timeAccessor;
            sampler.output = rotationAccessor;
            sampler.interpolation = "LINEAR";
            int samplerIndex = animation.samplers.size();
            animation.samplers.push_back(sampler);
            tinygltf::AnimationChannel channel;
            channel.sampler = samplerIndex;
            channel.target_node = nodeIndex;
            channel.target_path = "rotation";
            animation.channels.push_back(channel);
        }
        if (node.scales.times.size()) {
            int timeAccessor = addAccessor(ctx.gltf,
                                           "times",
                                           0,
                                           TINYGLTF_TYPE_SCALAR,
                                           TINYGLTF_COMPONENT_TYPE_FLOAT,
                                           node.scales.times.size(),
                                           node.scales.times.data(),
                                           true);
            int scaleAccessor = addAccessor(ctx.gltf,
                                            "scales",
                                            0,
                                            TINYGLTF_TYPE_VEC3,
                                            TINYGLTF_COMPONENT_TYPE_FLOAT,
                                            node.scales.values.size(),
                                            node.scales.values.data(),
                                            false);
            tinygltf::AnimationSampler sampler;
            sampler.input = timeAccessor;
            sampler.output = scaleAccessor;
            sampler.interpolation = "LINEAR";
            int samplerIndex = animation.samplers.size();
            animation.samplers.push_back(sampler);
            tinygltf::AnimationChannel channel;
            channel.sampler = samplerIndex;
            channel.target_node = nodeIndex;
            channel.target_path = "scale";
            animation.channels.push_back(channel);
        }
        ctx.gltf->animations.push_back(animation);
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Animation exported");
    }

    return nodeIndex;
}

void
exportSkeletons(ExportGltfContext& ctx, int skeletonRootNodeIndex)
{
    const UsdData* usd = ctx.usd;
    for (size_t i = 0; i < usd->skeletons.size(); i++) {
        const Skeleton& skeleton = usd->skeletons[i];
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "gltf::export skeleton {%s}\n", skeleton.name.c_str());

        // Create a root node to hold the skeleton root nodes as there can be more than one
        int skelNodeIndex = ctx.gltf->nodes.size();
        ctx.gltf->nodes.push_back(tinygltf::Node());
        tinygltf::Node& skelNode = ctx.gltf->nodes[skelNodeIndex];
        skelNode.name = "Skel" + std::to_string(i);

        if (skeletonRootNodeIndex == -1) {
            ctx.gltf->scenes.back().nodes.push_back(skelNodeIndex);
        } else {
            ctx.gltf->nodes[skeletonRootNodeIndex].children.push_back(skelNodeIndex);
        }

        // Export skeleton transforms
        std::vector<float> values;
        copyMatrices(skeleton.inverseBindTransforms, values);
        int inverseBindMatricessAccessorIndex = addAccessor(ctx.gltf,
                                                            "inverseBindMatrices",
                                                            0,
                                                            TINYGLTF_TYPE_MAT4,
                                                            TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                            values.size() / 16,
                                                            values.data(),
                                                            false);

        // Export skeleton nodes
        std::unordered_map<SdfPath, int, SdfPath::Hash> skeletonNodesMap;
        std::vector<int> indices(skeleton.joints.size());
        int skelRoot = -1;
        int rootCount = 0;
        for (size_t j = 0; j < skeleton.joints.size(); j++) {
            SdfPath jointPath(skeleton.joints[j]);
            int nodeIndex = ctx.gltf->nodes.size();
            ctx.gltf->nodes.push_back(tinygltf::Node());
            tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];
            node.name = jointPath.GetName();
            decomposeMatrix(skeleton.restTransforms[j], node);

            indices[j] = nodeIndex;
            skeletonNodesMap[jointPath] = nodeIndex;
            int parent = skeleton.parents[j];

            if (parent < 0) {
                skelRoot = nodeIndex;
                ++rootCount;

                ctx.gltf->nodes[skelNodeIndex].children.push_back(nodeIndex);
            }
            TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                         "Adding node path %s (%s) at %d\n",
                         jointPath.GetText(),
                         node.name.c_str(),
                         nodeIndex);

            if (parent >= 0) {
                SdfPath parentJointPath(skeleton.joints[parent]);
                int parentNodeIndex = skeletonNodesMap[parentJointPath];
                tinygltf::Node& parentNode = ctx.gltf->nodes[parentNodeIndex];
                parentNode.children.push_back(nodeIndex);
                TF_DEBUG_MSG(
                  FILE_FORMAT_GLTF, "Adding node to parent %s\n", parentJointPath.GetText());
            }
        }

        // Export skeleton into a skin object
        int skinIndex = ctx.gltf->skins.size();
        ctx.gltf->skins.push_back(tinygltf::Skin());
        tinygltf::Skin& skin = ctx.gltf->skins[skinIndex];
        skin.joints = indices;
        skin.inverseBindMatrices = inverseBindMatricessAccessorIndex;
        // Only set the skeleton root on the skin if there is one root.
        // Otherwise, this is generates a gltf validation warning.
        if (rootCount == 1) {
            skin.skeleton = skelRoot;
        }

        const std::vector<int>& skelRoots = ctx.skeletonsToSkelRootsMap[i];

        // Export target skinned meshes into root nodes (previously cached in ctx.primitiveMap)
        // XXX should these form a hierarchy as well?
        for (size_t j = 0; j < skeleton.targets.size(); j++) {
            int usdMeshIndex = skeleton.targets[j];
            const std::string& meshName = usd->meshes[usdMeshIndex].name;

            for (size_t k = 0; k < skelRoots.size(); ++k) {
                int nodeIndex = ctx.gltf->nodes.size();
                ctx.gltf->nodes.push_back(tinygltf::Node());
                tinygltf::Node& node = ctx.gltf->nodes[nodeIndex];
                node.name =
                  "skeleton_" + std::to_string(i) + "_" + std::to_string(j) + "_" + meshName;
                node.skin = skinIndex;

                if (skeletonRootNodeIndex == -1) {
                    ctx.gltf->scenes.back().nodes.push_back(nodeIndex);
                } else {
                    ctx.gltf->nodes[skeletonRootNodeIndex].children.push_back(nodeIndex);
                }

                std::vector<tinygltf::Primitive>& primitives = ctx.primitiveMap[usdMeshIndex];
                if (primitives.size()) {
                    size_t meshIndex = ctx.gltf->meshes.size();
                    ctx.gltf->meshes.push_back(tinygltf::Mesh());
                    tinygltf::Mesh& gmesh = ctx.gltf->meshes[meshIndex];
                    gmesh.name = meshName;
                    for (size_t j = 0; j < primitives.size(); j++) {
                        gmesh.primitives.push_back(primitives[j]);
                    }
                    node.mesh = meshIndex;
                }
            }
        }

        // Export skeleton animations

        if (skeleton.animations.size() > 0) {
            // We need to convert from timeCodesPerSecond to seconds so be compute the multiplier.
            float secondsPerTimeCode =
              ctx.usd->timeCodesPerSecond != 0.0 ? 1.0f / ctx.usd->timeCodesPerSecond : 1.0f;
            const Animation& animation = ctx.usd->animations[skeleton.animations.front()];
            size_t boneCount = skeleton.joints.size();
            size_t animationTimesCount = animation.times.size();

            std::vector<float> times(animationTimesCount);
            std::vector<std::vector<float>> translations(
              boneCount, std::vector<float>(animationTimesCount * 3));
            std::vector<std::vector<float>> rotations(boneCount,
                                                      std::vector<float>(animationTimesCount * 4));
            std::vector<std::vector<float>> scales(boneCount,
                                                   std::vector<float>(animationTimesCount * 3));
            for (size_t i = 0; i < animationTimesCount; i++) {
                times[i] = animation.times[i] * secondsPerTimeCode;
                for (size_t j = 0; j < boneCount; j++) {
                    GfVec3f imaginary = animation.rotations[i][j].GetImaginary();
                    translations[j][i * 3] = animation.translations[i][j][0];
                    translations[j][i * 3 + 1] = animation.translations[i][j][1];
                    translations[j][i * 3 + 2] = animation.translations[i][j][2];
                    rotations[j][i * 4] = imaginary[0];
                    rotations[j][i * 4 + 1] = imaginary[1];
                    rotations[j][i * 4 + 2] = imaginary[2];
                    rotations[j][i * 4 + 3] = animation.rotations[i][j].GetReal();
                    scales[j][i * 3] = animation.scales[i][j][0];
                    scales[j][i * 3 + 1] = animation.scales[i][j][1];
                    scales[j][i * 3 + 2] = animation.scales[i][j][2];
                }
            }

            int timeAccessor = addAccessor(ctx.gltf,
                                           "times",
                                           0,
                                           TINYGLTF_TYPE_SCALAR,
                                           TINYGLTF_COMPONENT_TYPE_FLOAT,
                                           animationTimesCount,
                                           times.data(),
                                           true);

            tinygltf::AnimationSampler translationSampler;
            translationSampler.input = timeAccessor;
            translationSampler.interpolation = "LINEAR";
            tinygltf::AnimationSampler rotationSampler;
            rotationSampler.input = timeAccessor;
            rotationSampler.interpolation = "LINEAR";
            tinygltf::AnimationSampler scaleSampler;
            scaleSampler.input = timeAccessor;
            scaleSampler.interpolation = "LINEAR";

            tinygltf::AnimationChannel translationChannel;
            translationChannel.target_path = "translation";
            tinygltf::AnimationChannel rotationChannel;
            rotationChannel.target_path = "rotation";
            tinygltf::AnimationChannel scaleChannel;
            scaleChannel.target_path = "scale";

            tinygltf::Animation anim;
            for (size_t i = 0; i < boneCount; i++) {
                int translationAccessor = addAccessor(ctx.gltf,
                                                      "translations",
                                                      0,
                                                      TINYGLTF_TYPE_VEC3,
                                                      TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                      translations[i].size() / 3,
                                                      translations[i].data(),
                                                      false);
                int rotationAccessor = addAccessor(ctx.gltf,
                                                   "rotations",
                                                   0,
                                                   TINYGLTF_TYPE_VEC4,
                                                   TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                   rotations[i].size() / 4,
                                                   rotations[i].data(),
                                                   false);
                int scaleAccessor = addAccessor(ctx.gltf,
                                                "scales",
                                                0,
                                                TINYGLTF_TYPE_VEC3,
                                                TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                scales[i].size() / 3,
                                                scales[i].data(),
                                                false);
                SdfPath jointPath(skeleton.joints[i]);
                int nodeIndex = skeletonNodesMap[jointPath];

                translationSampler.output = translationAccessor;
                rotationSampler.output = rotationAccessor;
                scaleSampler.output = scaleAccessor;

                int translationSamplerIndex = anim.samplers.size();
                anim.samplers.push_back(translationSampler);
                int rotationSamplerIndex = anim.samplers.size();
                anim.samplers.push_back(rotationSampler);
                int scaleSamplerIndex = anim.samplers.size();
                anim.samplers.push_back(scaleSampler);

                translationChannel.sampler = translationSamplerIndex;
                translationChannel.target_node = nodeIndex;
                rotationChannel.sampler = rotationSamplerIndex;
                rotationChannel.target_node = nodeIndex;
                scaleChannel.sampler = scaleSamplerIndex;
                scaleChannel.target_node = nodeIndex;

                anim.channels.push_back(translationChannel);
                anim.channels.push_back(rotationChannel);
                anim.channels.push_back(scaleChannel);
            }
            ctx.gltf->animations.push_back(anim);
        }
    }
}

int
getWrapCode(const TfToken& wrap)
{
    if (wrap == AdobeTokens->repeat)
        return TINYGLTF_TEXTURE_WRAP_REPEAT;
    if (wrap == AdobeTokens->clamp)
        return TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE;
    if (wrap == AdobeTokens->mirror)
        return TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT;
    if (wrap == AdobeTokens->black || wrap == AdobeTokens->useMetadata) {
        TF_WARN("Wrap mode %s is not supported in GLTF", wrap.GetText());
    }
    // Note, the default wrap mode in USD is "useMetadata", which is not supported in GLTF. So we
    // default to the most common mode which is repeat.
    return TINYGLTF_TEXTURE_WRAP_REPEAT;
}

void
exportTexture(ExportGltfContext& ctx, const Input& input, int& textureIndex, int& texCoord)
{
    if (input.image < 0)
        return;
    tinygltf::Sampler sampler;
    sampler.magFilter = TINYGLTF_TEXTURE_FILTER_LINEAR;
    sampler.minFilter = TINYGLTF_TEXTURE_FILTER_LINEAR;
    sampler.wrapS = getWrapCode(input.wrapS);
    sampler.wrapT = getWrapCode(input.wrapT);
    int samplerIndex = ctx.gltf->samplers.size();
    ctx.gltf->samplers.push_back(sampler);
    tinygltf::Texture texture;
    texture.sampler = samplerIndex;
    texture.source = input.image;
    textureIndex = ctx.gltf->textures.size();
    ctx.gltf->textures.push_back(texture);
    texCoord = 0;
    TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                 "glTF::write texture[%d] { source: %d, coord: %d }\n",
                 textureIndex,
                 input.image,
                 texCoord);
}

void
addMaterialExt(ExportGltfContext& ctx,
               tinygltf::Material& gltfMaterial,
               const std::string& extensionName,
               const ExtMap& ext)
{
    addExtension(ctx, gltfMaterial.extensions, extensionName, ext);
}

void
addFloatValueToExt(ExtMap& ext, const std::string& name, float value)
{
    ext[name] = tinygltf::Value((double)value);
}

bool
addFloatValueToExt(ExtMap& ext,
                   const std::string& name,
                   const VtValue& vtValue,
                   float defaultValue = 0.0f)
{
    if (vtValue.IsHolding<float>()) {
        float value = vtValue.UncheckedGet<float>();
        if (value != defaultValue) {
            addFloatValueToExt(ext, name, value);

            return true;
        }
    }
    return false;
}

void
addXYValueToExt(ExtMap& ext, const std::string& name, const GfVec2f& value)
{
    tinygltf::Value::Array array(2);
    array[0] = tinygltf::Value((double)value[0]);
    array[1] = tinygltf::Value((double)value[1]);
    ext[name] = tinygltf::Value(array);
}

void
addColorValueToExt(ExtMap& ext, const std::string& name, const GfVec3f& value)
{
    tinygltf::Value::Array array(3);
    array[0] = tinygltf::Value((double)value[0]);
    array[1] = tinygltf::Value((double)value[1]);
    array[2] = tinygltf::Value((double)value[2]);
    ext[name] = tinygltf::Value(array);
}

bool
addColorValueToExt(ExtMap& ext,
                   const std::string& name,
                   const VtValue& vtValue,
                   const GfVec3f& defaultValue = GfVec3f(0.0f))
{
    if (vtValue.IsHolding<GfVec3f>()) {
        const GfVec3f& value = vtValue.UncheckedGet<GfVec3f>();
        if (value != defaultValue) {
            addColorValueToExt(ext, name, value);

            return true;
        }
    }
    return false;
}

bool
exportTextureTransform(ExportGltfContext& ctx, const Input& input, ExtMap& extensions)
{
    if (input.image < 0) {
        return false;
    }

    float rot = 0.0f;
    GfVec2f scale(1.0f);
    GfVec2f trans(0.0f);
    bool hasRot = false;
    bool hasScale = false;
    bool hasTrans = false;
    if (input.transformRotation.IsHolding<float>()) {
        rot = input.transformRotation.UncheckedGet<float>() * deg2rad;
        hasRot = rot != 0.0f;
    }
    if (input.transformScale.IsHolding<GfVec2f>()) {
        scale = input.transformScale.UncheckedGet<GfVec2f>();
        scale[1] = -scale[1];
        hasScale = scale[0] != 1.0f || scale[1] != 1.0f;
    } else {
        scale[1] = -1.0f;
        hasScale = true;
    }
    if (input.transformTranslation.IsHolding<GfVec2f>()) {
        trans = input.transformTranslation.UncheckedGet<GfVec2f>();
        trans[1] = 1.0f - trans[1];
        hasTrans = trans[0] != 0.0f || trans[1] != 0.0f;
    } else {
        trans[1] = 1.0f;
        hasTrans = true;
    }

    if (hasRot || hasScale || hasTrans) {
        ExtMap extMap;
        if (hasRot) {
            addFloatValueToExt(extMap, "rotation", rot);
        }
        if (hasScale) {
            addXYValueToExt(extMap, "scale", scale);
        }
        if (hasTrans) {
            addXYValueToExt(extMap, "offset", trans);
        }
        addExtension(ctx, extensions, "KHR_texture_transform", extMap, true /*= addToRequired*/);
        return true;
    }
    return false;
}

bool
addTextureToExt(ExportGltfContext& ctx,
                InputTranslator& inputTranslator,
                ExtMap& ext,
                const Input& input,
                const std::string& textureName,
                const std::string& factorName = std::string(),
                float factorDefaultValue = 0.0f)
{
    if (input.image >= 0) {
        Input translatedInput;
        inputTranslator.translateDirect(input, translatedInput);
        int textureIndex = -1;
        int texCoord = -1;
        exportTexture(ctx, translatedInput, textureIndex, texCoord);

        if (textureIndex != -1) {
            std::map<std::string, tinygltf::Value> textureInfo;
            textureInfo["index"] = tinygltf::Value(textureIndex);
            if (texCoord != 0) {
                textureInfo["texCoord"] = tinygltf::Value(texCoord);
            }
            ExtMap textureExtensions;
            if (exportTextureTransform(ctx, input, textureExtensions)) {
                textureInfo["extensions"] = tinygltf::Value(textureExtensions);
            }
            ext[textureName] = tinygltf::Value(textureInfo);
        }

        if (!factorName.empty()) {
            if (input.channel == AdobeTokens->rgb) {
                if (translatedInput.scale.IsHolding<GfVec4f>()) {
                    const GfVec4f& scale = translatedInput.scale.UncheckedGet<GfVec4f>();
                    if (scale[0] != factorDefaultValue || scale[1] != factorDefaultValue ||
                        scale[2] != factorDefaultValue) {
                        addColorValueToExt(ext, factorName, GfVec3f(scale[0], scale[1], scale[2]));
                    }
                }
            } else {
                int channel = token2Channel(input.channel);
                if (channel != -1 && translatedInput.scale.IsHolding<GfVec4f>()) {
                    float scale = translatedInput.scale.UncheckedGet<GfVec4f>()[channel];
                    if (scale != factorDefaultValue) {
                        addFloatValueToExt(ext, factorName, scale);
                    }
                }
            }
        }

        return true;
    } else if (!input.value.IsEmpty() && !factorName.empty()) {
        if (input.value.IsHolding<float>()) {
            return addFloatValueToExt(ext, factorName, input.value, factorDefaultValue);
        }
        if (input.value.IsHolding<GfVec3f>()) {
            return addColorValueToExt(ext, factorName, input.value, GfVec3f(factorDefaultValue));
        }
        TF_WARN("Input for %s did not contain float or GfVec3f", factorName.c_str());
    }

    return false;
}

bool
exportAnisotropyExtension(ExportGltfContext& ctx,
                          InputTranslator& inputTranslator,
                          const Material& m,
                          tinygltf::Material& gm)
{
    ExtMap ext;
    // XXX WARNING this conversion is not correct!!!
    // This mirrors the incorrect import operation. See `importMaterials` for a description of the
    // problem.
    if (addTextureToExt(ctx,
                        inputTranslator,
                        ext,
                        m.anisotropyLevel,
                        "anisotropyTexture",
                        "anisotropyStrength")) {
        if (m.anisotropyAngle.value.IsHolding<float>()) {
            float angle = m.anisotropyAngle.value.UncheckedGet<float>();
            constexpr float PI = 3.14159265358979311600f;
            float anisotropyRotation = 2.0f * PI * angle - PI;
            addFloatValueToExt(ext, "anisotropyRotation", anisotropyRotation);
        }
        addMaterialExt(ctx, gm, "KHR_materials_anisotropy", ext);
        return true;
    }

    return false;
}

bool
exportClearcoatExtension(ExportGltfContext& ctx,
                         InputTranslator& inputTranslator,
                         const Material& m,
                         tinygltf::Material& gm)
{
    ExtMap ext;
    if (addTextureToExt(
          ctx, inputTranslator, ext, m.clearcoat, "clearcoatTexture", "clearcoatFactor") |
        addTextureToExt(ctx,
                        inputTranslator,
                        ext,
                        m.clearcoatRoughness,
                        "clearcoatRoughnessTexture",
                        "clearcoatRoughnessFactor") |
        addTextureToExt(ctx, inputTranslator, ext, m.clearcoatNormal, "clearcoatNormalTexture")) {
        addMaterialExt(ctx, gm, "KHR_materials_clearcoat", ext);
        return true;
    }

    return false;
}

bool
exportEmissiveStrengthExtension(ExportGltfContext& ctx,
                                InputTranslator& inputTranslator,
                                float emissiveStrength,
                                tinygltf::Material& gm)
{
    if (emissiveStrength == 1.0f) {
        return false;
    }
    ExtMap ext;
    addFloatValueToExt(ext, "emissiveStrength", emissiveStrength);
    addMaterialExt(ctx, gm, "KHR_materials_emissive_strength", ext);
    return true;
}

bool
exportIorExtension(ExportGltfContext& ctx,
                   InputTranslator& inputTranslator,
                   const Material& m,
                   tinygltf::Material& gm)
{
    ExtMap ext;
    if (addFloatValueToExt(ext, "ior", m.ior.value, 1.5f)) {
        addMaterialExt(ctx, gm, "KHR_materials_ior", ext);
        return true;
    }
    return false;
}

bool
exportSheenExtension(ExportGltfContext& ctx,
                     InputTranslator& inputTranslator,
                     const Material& m,
                     tinygltf::Material& gm)
{
    ExtMap ext;
    if (addTextureToExt(
          ctx, inputTranslator, ext, m.sheenColor, "sheenColorTexture", "sheenColorFactor") |
        addTextureToExt(ctx,
                        inputTranslator,
                        ext,
                        m.sheenRoughness,
                        "sheenRoughnessTexture",
                        "sheenRoughnessFactor")) {
        addMaterialExt(ctx, gm, "KHR_materials_sheen", ext);
        return true;
    }

    return false;
}

bool
exportSpecularExtension(ExportGltfContext& ctx,
                        InputTranslator& inputTranslator,
                        const Material& m,
                        tinygltf::Material& gm)
{
    ExtMap ext;
    if (addTextureToExt(
          ctx, inputTranslator, ext, m.specularLevel, "specularTexture", "specularFactor", 1.0f) |
        addTextureToExt(ctx,
                        inputTranslator,
                        ext,
                        m.specularColor,
                        "specularColorTexture",
                        "specularColorFactor",
                        1.0f)) {
        addMaterialExt(ctx, gm, "KHR_materials_specular", ext);
        return true;
    }

    return false;
}

bool
exportTransmissionExtension(ExportGltfContext& ctx,
                            InputTranslator& inputTranslator,
                            const Material& m,
                            tinygltf::Material& gm)
{
    ExtMap ext;
    if (addTextureToExt(
          ctx, inputTranslator, ext, m.transmission, "transmissionTexture", "transmissionFactor")) {
        // If no transmission factor was associated with the input, we author a factor of 1.0 to
        // enable the extension
        if (!ext.count("transmissionFactor")) {
            addFloatValueToExt(ext, "transmissionFactor", 1.0f);
        }
        addMaterialExt(ctx, gm, "KHR_materials_transmission", ext);
        return true;
    }

    return false;
}

bool
exportVolumeExtension(ExportGltfContext& ctx,
                      InputTranslator& inputTranslator,
                      const Material& m,
                      tinygltf::Material& gm)
{
    ExtMap ext;
    if (addTextureToExt(
          ctx, inputTranslator, ext, m.thickness, "thicknessTexture", "thicknessFactor") |
        addFloatValueToExt(ext, "attenuationDistance", m.absorptionDistance.value) |
        addColorValueToExt(ext, "attenuationColor", m.absorptionColor.value, GfVec3f(1.0f))) {
        addMaterialExt(ctx, gm, "KHR_materials_volume", ext);
        return true;
    }

    return false;
}

bool
exportAdobeClearcoatSpecularExtension(ExportGltfContext& ctx,
                                      InputTranslator& inputTranslator,
                                      const Material& m,
                                      tinygltf::Material& gm)
{
    ExtMap ext;
    if (addTextureToExt(ctx,
                        inputTranslator,
                        ext,
                        m.clearcoatSpecular,
                        "clearcoatSpecularTexture",
                        "clearcoatSpecularFactor",
                        1.0f) |
        addFloatValueToExt(ext, "clearcoatIor", m.clearcoatIor.value, 1.5f)) {
        addMaterialExt(ctx, gm, "ADOBE_materials_clearcoat_specular", ext);
        return true;
    }

    return false;
}

bool
exportAdobeClearcoatTintExtension(ExportGltfContext& ctx,
                                  InputTranslator& inputTranslator,
                                  const Material& m,
                                  tinygltf::Material& gm)
{
    ExtMap ext;
    if (addTextureToExt(ctx,
                        inputTranslator,
                        ext,
                        m.clearcoatColor,
                        "clearcoatTintTexture",
                        "clearcoatTintFactor")) {
        addMaterialExt(ctx, gm, "ADOBE_materials_clearcoat_tint", ext);
        return true;
    }

    return false;
}

bool
isSupportedGLTFImageFormat(const adobe::usd::ImageFormat format)
{
    switch (format) {
        case ImageFormatPng:
        case ImageFormatJpg:
        case ImageFormatBmp:
        case ImageFormatWebp:
            return true;
        default:
            return false;
    }
}

// Missing extensions relative to import:
// * KHR_materials_diffuse_transmission
// * KHR_materials_subsurface
// Both of these extensions are not yet ratified and we might not want to produce assets with these
// since the extensions could still change

void
exportMaterials(ExportGltfContext& ctx)
{
    InputTranslator inputTranslator(true, ctx.usd->images, DEBUG_TAG);
    ctx.gltf->materials.resize(ctx.usd->materials.size());
    for (size_t i = 0; i < ctx.usd->materials.size(); i++) {
        Material& m = ctx.usd->materials[i];
        tinygltf::Material& gm = ctx.gltf->materials[i];

        gm.name = m.name;
        // If we're not exporting material extensions which can express transmission directly, we
        // map it to opacity since transmission is an important effect we want to capture, even if
        // approximated as opacity
        if (!ctx.options.useMaterialExtensions && !m.transmission.isEmpty()) {
            m.opacity = m.transmission;
            GfVec4f scale = m.opacity.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
            GfVec4f bias = m.opacity.bias.GetWithDefault<GfVec4f>(GfVec4f(0.0f));

            // When converting from transmission to opacity, we should not convert full transmission
            // into zero opacity, since that completely removes the material. It also prevents any
            // of the original surface color from coming through. So we limit the transmission to
            // 75%, which will lead to a minimum opacity of 25%, which makes sure transparent
            // objects do not completely disappear or lose their tint.
            static const float maxTransmissionFactor = 0.75f;
            scale *= maxTransmissionFactor;

            // Transmission is inverted relative to opacity. So we invert using scale and bias,
            // considering that there could be a previous scale and bias.
            m.opacity.scale = -scale;
            m.opacity.bias = GfVec4f(1.0f) - bias;
            TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                         "glTF::write material %s, using transmission for opacity\n",
                         m.name.c_str());
        }
        if (m.opacity.image >= 0) {
            // Unwarranted opacity is expensive and leads to rendering errors, so we check the pixel
            // values, which is expensive
            // XXX since we only need the range for a single channel it is probably cheaper to
            // compute the range just for that. But we can't avoid reading the texture as a whole
            // since channels are packed.

            float texOpacity = -1.0f;
            int ch = token2Channel(m.opacity.channel);
            if (ch >= 0) {
                auto [minRgba, maxRgba] = inputTranslator.computeRange(m.opacity);
                float minValue = minRgba[ch];
                float maxValue = maxRgba[ch];

                if (minValue > maxValue) {
                    // No texture data for opacity. We assume opacity from the texture is 1.0
                    TF_DEBUG_MSG(
                      FILE_FORMAT_GLTF, "Invalid opacity texture on material %s", m.name.c_str());
                    texOpacity = 1.0f;
                } else {
                    static const float eps = 0.001f;
                    if ((maxValue - minValue) < eps) {
                        // No variance. We have a single fixed value
                        texOpacity = maxValue;
                    }
                }
            }

            // We have a constant value and don't need a texture (or we need to ignore it because
            // the channel is invalid)
            if (texOpacity >= 0 || ch < 0) {
                float opacityValue = 1.0f;
                if (ch >= 0) {
                    GfVec4f scale = m.opacity.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
                    GfVec4f bias = m.opacity.bias.GetWithDefault<GfVec4f>(GfVec4f(0.0f));
                    opacityValue = scale[ch] * texOpacity + bias[ch];
                } else {
                    // the channel token is invalid (eg rgb) so we default to an opacity value
                    // of 1.0
                    TF_WARN("An invalid channel identifier was provided resulting in the opacity "
                            "texture being ignored. A default opacity of 1.0 is used.");
                }
                m.opacity.image = -1;
                m.opacity.value = opacityValue;
                // Clear the scale and bias since it was applied to the constant value
                m.opacity.scale = VtValue();
                m.opacity.bias = VtValue();
                TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                             "glTF::write opacity for %s is a constant %f (texture omitted)\n",
                             m.name.c_str(),
                             opacityValue);
            }
        }
        float constOpacity = -1.0f;
        if (m.opacity.image >= 0 ||
            (getInputValue(m.opacity, &constOpacity) && constOpacity != 1.0f)) {
            TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                         "glTF::write material %s, opacity in use (image %d, const %f)\n",
                         m.name.c_str(),
                         m.opacity.image,
                         constOpacity);
            gm.alphaMode = "BLEND";
        }
        Input baseColor;
        Input emissive;
        Input normal;
        Input occlusion;
        Input emptyInput;
        emptyInput.value = 0.0f;
        if (m.opacity.image >= 0 || !m.opacity.value.IsEmpty()) {
            // Create a texture that combines diffuse color and opacity in the alpha channel
            TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                         "glTF::write material %s, generating baseColor and opacity texture\n",
                         m.name.c_str());
            // GLTF can't express the bias on a texture, so if a texture uses bias we need to
            // process the pixels and incorporate it into the texel data. Note, this always happens
            // when we turn transmission into opacity in the code above.
            GfVec4f bias = m.opacity.bias.GetWithDefault<GfVec4f>(GfVec4f(0.0f));
            if (bias != GfVec4f(0.0f)) {
                GfVec4f scale = m.opacity.scale.GetWithDefault<GfVec4f>(GfVec4f(1.0f));
                Input opacity = m.opacity;
                int chIdx = m.opacity.image >= 0 ? token2Channel(m.opacity.channel) : 0;
                float opacityScale = scale[chIdx];
                float opacityBias = bias[chIdx];
                TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                             "glTF::write material %s, opacity uses bias -> affine transform "
                             "image: %d %f %f\n",
                             m.name.c_str(),
                             chIdx,
                             opacityScale,
                             opacityBias);
                inputTranslator.translateAffine(
                  "opacity", m.opacity, opacityScale, opacityBias, opacity, /*intermediate=*/true);

                // Replace the old opacity
                m.opacity = opacity;
            }
            // XXX This step can be avoided if the baseColor and opacity textures are the same
            // physical texture, which is very common for assets that came from GLTF originally
            // TODO add a check for this common case and avoid the texture generation for faster
            // export
            inputTranslator.translateMix("baseColor",
                                         AdobeTokens->sRGB,
                                         inputTranslator.split3f(m.diffuseColor, 0),
                                         inputTranslator.split3f(m.diffuseColor, 1),
                                         inputTranslator.split3f(m.diffuseColor, 2),
                                         m.opacity,
                                         baseColor);
        } else {
            // No opacity! Just use diffuseColor as baseColor
            inputTranslator.translateDirect(m.diffuseColor, baseColor);
        }
        inputTranslator.translateDirect(m.emissiveColor, emissive);
        inputTranslator.translateDirect(m.normal, normal);

        exportTexture(ctx,
                      baseColor,
                      gm.pbrMetallicRoughness.baseColorTexture.index,
                      gm.pbrMetallicRoughness.baseColorTexture.texCoord);
        exportTextureTransform(ctx, baseColor, gm.pbrMetallicRoughness.baseColorTexture.extensions);

        exportTexture(ctx, emissive, gm.emissiveTexture.index, gm.emissiveTexture.texCoord);
        exportTextureTransform(ctx, emissive, gm.emissiveTexture.extensions);

        exportTexture(ctx, normal, gm.normalTexture.index, gm.normalTexture.texCoord);
        exportTextureTransform(ctx, normal, gm.normalTexture.extensions);

        // Occlusion texture needs to be in the r channel
        bool needToPackOcclusion = m.occlusion.image >= 0 && m.occlusion.channel != AdobeTokens->r;
        // Roughness texture needs to be in the g channel
        bool needToPackRoughness = m.roughness.image >= 0 && m.roughness.channel != AdobeTokens->g;
        // Metallic texture needs to be in the b channel
        bool needToPackMetallic = m.metallic.image >= 0 && m.metallic.channel != AdobeTokens->b;
        // Roughness and metallic need to be in the same texture
        bool needToPackRoughnessWithMetallic =
          m.roughness.image >= 0 && m.metallic.image >= 0 && m.roughness.image != m.metallic.image;

        if (needToPackOcclusion || needToPackRoughness || needToPackMetallic ||
            needToPackRoughnessWithMetallic) {
            TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                         "glTF::write material %s, generating occlusionRoughnessMetallic texture: "
                         "%d %d %d %d\n",
                         m.name.c_str(),
                         needToPackOcclusion,
                         needToPackRoughness,
                         needToPackMetallic,
                         needToPackRoughnessWithMetallic);
            // XXX This is currently generating a 4 channel texture, where a 3 channel texture would
            // do
            Input occlusionRoughnessMetallic;
            Input solidAlphaInput;
            solidAlphaInput.value = 1.0f;

            inputTranslator.translateMix("occlusionRoughnessMetallic",
                                         AdobeTokens->raw,
                                         m.occlusion,
                                         m.roughness,
                                         m.metallic,
                                         solidAlphaInput,
                                         occlusionRoughnessMetallic);
            if (m.roughness.image >= 0 || m.metallic.image >= 0) {
                exportTexture(ctx,
                              occlusionRoughnessMetallic,
                              gm.pbrMetallicRoughness.metallicRoughnessTexture.index,
                              gm.pbrMetallicRoughness.metallicRoughnessTexture.texCoord);
                exportTextureTransform(ctx,
                                       occlusionRoughnessMetallic,
                                       gm.pbrMetallicRoughness.metallicRoughnessTexture.extensions);
            }
            if (m.occlusion.image >= 0) {
                exportTexture(ctx,
                              occlusionRoughnessMetallic,
                              gm.occlusionTexture.index,
                              gm.occlusionTexture.texCoord);
                exportTextureTransform(
                  ctx, occlusionRoughnessMetallic, gm.occlusionTexture.extensions);
            }
        } else {
            inputTranslator.translateDirect(m.occlusion, occlusion);
            // The roughness texture (if valid) also contains the metallic data, so one transfer
            // is enough
            Input roughnessMetallic;
            inputTranslator.translateDirect(m.roughness, roughnessMetallic);

            exportTexture(ctx, occlusion, gm.occlusionTexture.index, gm.occlusionTexture.texCoord);
            exportTextureTransform(ctx, occlusion, gm.occlusionTexture.extensions);

            exportTexture(ctx,
                          roughnessMetallic,
                          gm.pbrMetallicRoughness.metallicRoughnessTexture.index,
                          gm.pbrMetallicRoughness.metallicRoughnessTexture.texCoord);
            exportTextureTransform(
              ctx, roughnessMetallic, gm.pbrMetallicRoughness.metallicRoughnessTexture.extensions);
        }

        if (m.diffuseColor.image >= 0 && m.diffuseColor.scale.IsHolding<GfVec4f>()) {
            GfVec4f scale = baseColor.scale.UncheckedGet<GfVec4f>();
            gm.pbrMetallicRoughness.baseColorFactor.resize(4, 1);
            gm.pbrMetallicRoughness.baseColorFactor[0] = scale[0];
            gm.pbrMetallicRoughness.baseColorFactor[1] = scale[1];
            gm.pbrMetallicRoughness.baseColorFactor[2] = scale[2];
        } else if (m.diffuseColor.value.IsHolding<GfVec3f>()) {
            GfVec4f value = baseColor.value.UncheckedGet<GfVec4f>();
            gm.pbrMetallicRoughness.baseColorFactor.resize(4, 1);
            gm.pbrMetallicRoughness.baseColorFactor[0] = value[0];
            gm.pbrMetallicRoughness.baseColorFactor[1] = value[1];
            gm.pbrMetallicRoughness.baseColorFactor[2] = value[2];
        }
        if (m.opacity.image >= 0 && m.opacity.scale.IsHolding<GfVec4f>()) {
            GfVec4f scale = m.opacity.scale.UncheckedGet<GfVec4f>();
            gm.pbrMetallicRoughness.baseColorFactor.resize(4, 1);
            gm.pbrMetallicRoughness.baseColorFactor[3] = scale[3];
        } else if (m.opacity.value.IsHolding<float>()) {
            float value = m.opacity.value.UncheckedGet<float>();
            gm.pbrMetallicRoughness.baseColorFactor.resize(4, 1);
            gm.pbrMetallicRoughness.baseColorFactor[3] = value;
        }
        float emissiveStrength = 1.0f;
        if (m.emissiveColor.image >= 0) {
            if (m.emissiveColor.scale.IsHolding<GfVec4f>()) {
                GfVec4f scale = m.emissiveColor.scale.UncheckedGet<GfVec4f>();
                // The emissiveFactor can only go up to 1.0 per component. Anything beyond that
                // needs to be handled by the emissiveStrength extension.
                float maxFactor = std::max(scale[0], std::max(scale[1], scale[2]));
                if (maxFactor > 1.0f) {
                    emissiveStrength = maxFactor;
                    scale[0] /= maxFactor;
                    scale[1] /= maxFactor;
                    scale[2] /= maxFactor;
                }
                gm.emissiveFactor.resize(3);
                gm.emissiveFactor[0] = scale[0];
                gm.emissiveFactor[1] = scale[1];
                gm.emissiveFactor[2] = scale[2];
            } else {
                gm.emissiveFactor.resize(3, 1);
            }
        } else if (m.emissiveColor.value.IsHolding<GfVec3f>()) {
            GfVec3f value = m.emissiveColor.value.UncheckedGet<GfVec3f>();
            // The emissiveFactor can only go up to 1.0 per component. Anything beyond that
            // needs to be handled by the emissiveStrength extension.
            float maxFactor = std::max(value[0], std::max(value[1], value[2]));
            if (maxFactor > 1.0f) {
                emissiveStrength = maxFactor;
                value[0] /= maxFactor;
                value[1] /= maxFactor;
                value[2] /= maxFactor;
            }
            gm.emissiveFactor.resize(3);
            gm.emissiveFactor[0] = value[0];
            gm.emissiveFactor[1] = value[1];
            gm.emissiveFactor[2] = value[2];
        }
        if (m.occlusion.image >= 0 && m.occlusion.scale.IsHolding<GfVec4f>()) {
            GfVec4f scale = m.occlusion.scale.UncheckedGet<GfVec4f>();
            gm.occlusionTexture.strength = scale[0];
        } else if (m.occlusion.value.IsHolding<float>()) {
            float value = m.occlusion.value.UncheckedGet<float>();
            gm.occlusionTexture.strength = value;
        }
        if (m.metallic.image >= 0) {
            if (m.metallic.scale.IsHolding<GfVec4f>()) {
                GfVec4f scale = m.metallic.scale.UncheckedGet<GfVec4f>();
                gm.pbrMetallicRoughness.metallicFactor = scale[0];
            }
        } else if (m.metallic.value.IsHolding<float>()) {
            float value = m.metallic.value.UncheckedGet<float>();
            gm.pbrMetallicRoughness.metallicFactor = value;
        } else {
            // UsdPreviewSurface uses a default of 0.0, but GLTF has a default of 1.0. So if we
            // don't author a value we would make every surface metallic
            gm.pbrMetallicRoughness.metallicFactor = 0.0f;
        }

        if (m.roughness.image >= 0) {
            if (m.roughness.scale.IsHolding<GfVec4f>()) {
                GfVec4f scale = m.roughness.scale.UncheckedGet<GfVec4f>();
                gm.pbrMetallicRoughness.roughnessFactor = scale[0];
            }
        } else if (m.roughness.value.IsHolding<float>()) {
            float value = m.roughness.value.UncheckedGet<float>();
            gm.pbrMetallicRoughness.roughnessFactor = value;
        } else {
            // UsdPreviewSurface uses a default of 0.5, but GLTF has a default of 1.0. So if we
            // don't author a value we would make every surface very rough
            gm.pbrMetallicRoughness.roughnessFactor = 0.5f;
        }

        if (m.opacityThreshold.image >= 0) {
            // TODO: can opacityThreshold really be sourced?
            gm.alphaMode = "MASK";
            gm.alphaCutoff = 0.5f;
        } else if (m.opacityThreshold.value.IsHolding<float>()) {
            float value = m.opacityThreshold.value.UncheckedGet<float>();
            gm.alphaMode = "MASK";
            gm.alphaCutoff = value;
        }

        if (ctx.options.useMaterialExtensions) {
            exportAnisotropyExtension(ctx, inputTranslator, m, gm);
            exportEmissiveStrengthExtension(ctx, inputTranslator, emissiveStrength, gm);
            exportIorExtension(ctx, inputTranslator, m, gm);
            exportSheenExtension(ctx, inputTranslator, m, gm);
            exportSpecularExtension(ctx, inputTranslator, m, gm);
            exportTransmissionExtension(ctx, inputTranslator, m, gm);
            exportVolumeExtension(ctx, inputTranslator, m, gm);

            // If the material was imported from GLTF and the clearcoat lobe was used to model
            // tinting of transmission (something ASM natively doesn't support), then we should not
            // export the clearcoat to GLTF here, since the shading model there will do the tint
            // by default and the clearcoat is redundant at best, if not wrong.
            bool exportClearcoat = !m.clearcoatModelsTransmissionTint;
            if (exportClearcoat) {
                exportClearcoatExtension(ctx, inputTranslator, m, gm);
                exportAdobeClearcoatSpecularExtension(ctx, inputTranslator, m, gm);
                exportAdobeClearcoatTintExtension(ctx, inputTranslator, m, gm);
            }
        }

        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "glTF::write material { %s }\n", m.name.c_str());
    }
    std::vector<ImageAsset>& images = inputTranslator.getImages();
    ctx.gltf->images.resize(images.size());
    for (size_t i = 0; i < images.size(); i++) {
        ImageAsset* ui = &images[i];
        tinygltf::Image& gi = ctx.gltf->images[i];
        gi.name = ui->name;
        if (ui->format == ImageFormatWebp) {
            ctx.extensionsUsed.insert("EXT_texture_webp");
            ctx.extensionsRequired.insert("EXT_texture_webp");
        }

        ImageAsset convertedImage;
        if (!isSupportedGLTFImageFormat(ui->format)) {
            if (Image::convertImageToPng(*ui, convertedImage)) {
                ui = &convertedImage;
            }
        }

        // We store embedded images in the binary buffer even when exporting to GLTF
        if (ctx.options.embedImages) {
            switch (ui->format) {
                case ImageFormatPng:
                    gi.mimeType = "image/png";
                    break;
                case ImageFormatJpg:
                    gi.mimeType = "image/jpeg";
                    break;
                case ImageFormatBmp:
                    gi.mimeType = "image/bmp";
                    break;
                case ImageFormatWebp:
                    gi.mimeType = "image/webp";
                    break;
                default:
                    gi.mimeType = "image/png";
                    break;
            }

            gi.bufferView =
              addImageBufferView(ctx.gltf, ui->name, ui->image.size(), ui->image.data());
            TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                         "glTF::write image buffer view { %s, %s, %d }\n",
                         ui->name.c_str(),
                         ui->uri.c_str(),
                         gi.bufferView);
        } else {
            gi.uri = ui->uri;
            // Store the image in the tinygltf image struct, so that it will be written to the
            // location of the URI
            gi.image.resize(ui->image.size());
            memcpy(gi.image.data(), ui->image.data(), ui->image.size());
        }

        TF_DEBUG_MSG(FILE_FORMAT_GLTF,
                     "glTF::write image[%lu] { %s %s %d }\n",
                     i,
                     gi.name.c_str(),
                     gi.uri.c_str(),
                     gi.bufferView);
    }
    TF_DEBUG_MSG(FILE_FORMAT_GLTF, "glTF::write all images written\n");
}

bool
exportPrimitive(ExportGltfContext& ctx,
                tinygltf::Primitive& primitive,
                int usdMeshIndex,
                Mesh& mesh,
                VtIntArray& indices,
                int positionsAccessor,
                int normalsAccessor,
                int tangentsAccessor,
                int uvsAccessor,
                int colorsAccessor,
                const std::vector<int>& jointsAccessors,
                const std::vector<int>& weightsAccessors,
                int material,
                bool doubleSided,
                bool isSubset)
{
    int indicesAccessor = addAccessor(ctx.gltf,
                                      "indices",
                                      TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER,
                                      TINYGLTF_TYPE_SCALAR,
                                      TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
                                      indices.size(),
                                      indices.data(),
                                      true);
    primitive.mode = TINYGLTF_MODE_TRIANGLES;
    if (material != -1)
        primitive.material = material;
    if (indicesAccessor != -1)
        primitive.indices = indicesAccessor;
    if (positionsAccessor != -1)
        primitive.attributes["POSITION"] = positionsAccessor;
    if (normalsAccessor != -1)
        primitive.attributes["NORMAL"] = normalsAccessor;
    if (tangentsAccessor != -1)
        primitive.attributes["TANGENT"] = tangentsAccessor;
    if (uvsAccessor != -1)
        primitive.attributes["TEXCOORD_0"] = uvsAccessor;
    if (colorsAccessor != -1)
        primitive.attributes["COLOR_0"] = colorsAccessor;
    for (size_t i = 0; i < jointsAccessors.size(); ++i) {
        std::string key = "JOINTS_" + std::to_string(i);
        primitive.attributes[key] = jointsAccessors[i];
    }
    for (size_t i = 0; i < weightsAccessors.size(); ++i) {
        std::string key = "WEIGHTS_" + std::to_string(i);
        primitive.attributes[key] = weightsAccessors[i];
    }

    // If multiple meshes have a different double sided property but the same material we will
    // be overwriting this setting in the gltf material. But we have no choice. Making a
    // material variant could be too costly. So in that case the last mesh to write this value
    // wins.
    if (doubleSided && material >= 0) {
        ctx.gltf->materials[material].doubleSided = true;
    }
    TF_DEBUG_MSG(
      FILE_FORMAT_GLTF,
      "glTF::cache primitive[%d]: {\"%s\", TRIANGLES, indices: %lu, pos: %lu, norms: %lu, "
      "uvs: %lu, joints: %lu, weights: %lu, subset: %s}\n",
      usdMeshIndex,
      mesh.name.c_str(),
      indices.size(),
      mesh.points.size(),
      mesh.normals.values.size(),
      mesh.uvs.values.size(),
      mesh.joints.size() / mesh.influenceCount,
      mesh.weights.size() / mesh.influenceCount,
      isSubset ? "true" : "false");
    return true;
}

bool
exportMeshes(ExportGltfContext& ctx)
{
    ctx.primitiveMap.resize(ctx.usd->meshes.size());
    for (size_t i = 0; i < ctx.usd->meshes.size(); i++) {
        std::vector<tinygltf::Primitive>& primitives = ctx.primitiveMap[i];
        Mesh& mesh = ctx.usd->meshes[i];
        if (mesh.points.size() == 0) {
            continue;
        }

        // bake the geomBindTransform into the mesh
        transformMesh(mesh, mesh.geomBindTransform);

        int positionsAccessor = addAccessor(ctx.gltf,
                                            "positions",
                                            TINYGLTF_TARGET_ARRAY_BUFFER,
                                            TINYGLTF_TYPE_VEC3,
                                            TINYGLTF_COMPONENT_TYPE_FLOAT,
                                            mesh.points.size(),
                                            mesh.points.data(),
                                            true);

        int normalsAccessor = addAccessor(ctx.gltf,
                                          "normals",
                                          TINYGLTF_TARGET_ARRAY_BUFFER,
                                          TINYGLTF_TYPE_VEC3,
                                          TINYGLTF_COMPONENT_TYPE_FLOAT,
                                          mesh.normals.values.size(),
                                          mesh.normals.values.data(),
                                          true);

        int tangentsAccessor = addAccessor(ctx.gltf,
                                           "tangents",
                                           TINYGLTF_TARGET_ARRAY_BUFFER,
                                           TINYGLTF_TYPE_VEC4,
                                           TINYGLTF_COMPONENT_TYPE_FLOAT,
                                           mesh.tangents.values.size(),
                                           mesh.tangents.values.data(),
                                           true);

        int uvsAccessor = addAccessor(ctx.gltf,
                                      "texCoords",
                                      TINYGLTF_TARGET_ARRAY_BUFFER,
                                      TINYGLTF_TYPE_VEC2,
                                      TINYGLTF_COMPONENT_TYPE_FLOAT,
                                      mesh.uvs.values.size(),
                                      mesh.uvs.values.data(),
                                      true);

        int colorsAccessor = -1;
        if (mesh.colors.size() && mesh.opacities.size() && mesh.colors[0].values.size() &&
            mesh.opacities[0].values.size() && mesh.colors[0].values.size() == mesh.points.size() &&
            mesh.opacities[0].values.size() == mesh.points.size()) {
            std::vector<float> colors(mesh.colors[0].values.size() * 4);
            for (size_t i = 0; i < mesh.colors[0].values.size(); i++) {
                colors[4 * i] = mesh.colors[0].values[i][0];
                colors[4 * i + 1] = mesh.colors[0].values[i][1];
                colors[4 * i + 2] = mesh.colors[0].values[i][2];
                colors[4 * i + 3] = mesh.opacities[0].values[i];
            }
            colorsAccessor = addAccessor(ctx.gltf,
                                         "color_0",
                                         TINYGLTF_TARGET_ARRAY_BUFFER,
                                         TINYGLTF_TYPE_VEC4,
                                         TINYGLTF_COMPONENT_TYPE_FLOAT,
                                         colors.size() / 4,
                                         colors.data(),
                                         true);
        } else if (mesh.colors.size() && mesh.colors[0].values.size() &&
                   mesh.colors[0].values.size() == mesh.points.size()) {
            colorsAccessor = addAccessor(ctx.gltf,
                                         "color_0",
                                         TINYGLTF_TARGET_ARRAY_BUFFER,
                                         TINYGLTF_TYPE_VEC3,
                                         TINYGLTF_COMPONENT_TYPE_FLOAT,
                                         mesh.colors[0].values.size(),
                                         mesh.colors[0].values.data(),
                                         true);
        } else if (mesh.opacities.size() && mesh.opacities[0].values.size() &&
                   mesh.opacities[0].values.size() == mesh.points.size()) {
            std::vector<float> colors(mesh.opacities[0].values.size() * 4);
            for (size_t i = 0; i < mesh.opacities[0].values.size(); i++) {
                colors[4 * i] = 1.0f;
                colors[4 * i + 1] = 1.0f;
                colors[4 * i + 2] = 1.0f;
                colors[4 * i + 3] = mesh.opacities[0].values[i];
            }
            colorsAccessor = addAccessor(ctx.gltf,
                                         "color_0",
                                         TINYGLTF_TARGET_ARRAY_BUFFER,
                                         TINYGLTF_TYPE_VEC4,
                                         TINYGLTF_COMPONENT_TYPE_FLOAT,
                                         colors.size() / 4,
                                         colors.data(),
                                         true);
        }

        std::vector<int> jointsAccessors;
        std::vector<int> weightsAccessors;
        if (mesh.joints.size() && mesh.influenceCount > 0) {

            int pointCount = mesh.joints.size() / mesh.influenceCount;

            int numValuesPerVertex = mesh.influenceCount;
            int paddedValuesPerVertex = ((numValuesPerVertex + 3) / 4) * 4;

            std::vector<unsigned short> jointIndicesValues(pointCount * paddedValuesPerVertex);
            std::vector<float> jointWeightsValues(pointCount * paddedValuesPerVertex);

            // de-dup the joint weights where a joint index appears more than once in the set of
            // values for a vertex
            for (int i = 0; i < pointCount; i++) {
                int srcOffset = numValuesPerVertex * i;
                int dstOffset = paddedValuesPerVertex * i;
                for (int j = 0; j < numValuesPerVertex; j++) {
                    int jointIndex = mesh.joints[srcOffset + j];
                    float jointWeight = mesh.weights[srcOffset + j];
                    jointIndicesValues[dstOffset + j] = jointIndex;
                    jointWeightsValues[dstOffset + j] = jointWeight;
                    // if jointWeight > 0, we need to possible merge duplicate joint indices. In
                    // many cases, both jointIndex and jointWeight will be zero so we can avoid this
                    // inner loop to check for duplicates
                    if (jointWeight > 0.0f) {
                        for (int jj = 0; jj < j; jj++) {
                            // this avoids joint index repetition
                            if (jointIndex == jointIndicesValues[dstOffset + jj]) {
                                jointIndicesValues[dstOffset + j] = 0;
                                jointWeightsValues[dstOffset + j] = 0;
                                jointWeightsValues[dstOffset + jj] += jointWeight;
                                break;
                            }
                        }
                    }
                }
            }

            if (paddedValuesPerVertex == 4) {

                int jointsAccessor = addAccessor(ctx.gltf,
                                                 "jointIndices",
                                                 TINYGLTF_TARGET_ARRAY_BUFFER,
                                                 TINYGLTF_TYPE_VEC4,
                                                 TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT,
                                                 pointCount,
                                                 jointIndicesValues.data(),
                                                 false);
                jointsAccessors.push_back(jointsAccessor);

                int weightsAccessor = addAccessor(ctx.gltf,
                                                  "jointWeights",
                                                  TINYGLTF_TARGET_ARRAY_BUFFER,
                                                  TINYGLTF_TYPE_VEC4,
                                                  TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                  pointCount,
                                                  jointWeightsValues.data(),
                                                  false);
                weightsAccessors.push_back(weightsAccessor);
            } else {
                std::vector<unsigned short> jointIndices(pointCount * 4);
                std::vector<float> jointWeights(pointCount * 4);

                int setCount = (mesh.influenceCount + 3) / 4;
                for (int setId = 0; setId < setCount; ++setId) {

                    // copy sets of 4 values into contiguous blocks
                    int offset = setId * 4;
                    for (int i = 0; i < pointCount; i++) {
                        const int k = paddedValuesPerVertex * i + offset;
                        jointIndices[4 * i + 0] = jointIndicesValues[k + 0];
                        jointIndices[4 * i + 1] = jointIndicesValues[k + 1];
                        jointIndices[4 * i + 2] = jointIndicesValues[k + 2];
                        jointIndices[4 * i + 3] = jointIndicesValues[k + 3];
                        jointWeights[4 * i + 0] = jointWeightsValues[k + 0];
                        jointWeights[4 * i + 1] = jointWeightsValues[k + 1];
                        jointWeights[4 * i + 2] = jointWeightsValues[k + 2];
                        jointWeights[4 * i + 3] = jointWeightsValues[k + 3];
                    }

                    int jointsAccessor = addAccessor(ctx.gltf,
                                                     "jointIndices_" + std::to_string(setId),
                                                     TINYGLTF_TARGET_ARRAY_BUFFER,
                                                     TINYGLTF_TYPE_VEC4,
                                                     TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT,
                                                     pointCount,
                                                     jointIndices.data(),
                                                     false);
                    jointsAccessors.push_back(jointsAccessor);

                    int weightsAccessor = addAccessor(ctx.gltf,
                                                      "jointWeights_" + std::to_string(setId),
                                                      TINYGLTF_TARGET_ARRAY_BUFFER,
                                                      TINYGLTF_TYPE_VEC4,
                                                      TINYGLTF_COMPONENT_TYPE_FLOAT,
                                                      pointCount,
                                                      jointWeights.data(),
                                                      false);
                    weightsAccessors.push_back(weightsAccessor);
                }
            }
        }

        if (mesh.subsets.size()) {
            primitives.resize(mesh.subsets.size());
            for (size_t j = 0; j < mesh.subsets.size(); j++) {
                Subset& subset = mesh.subsets[j];
                exportPrimitive(ctx,
                                primitives[j],
                                i,
                                mesh,
                                subset.indices,
                                positionsAccessor,
                                normalsAccessor,
                                tangentsAccessor,
                                uvsAccessor,
                                colorsAccessor,
                                jointsAccessors,
                                weightsAccessors,
                                subset.material,
                                mesh.doubleSided,
                                true);
            }
        } else {
            primitives.resize(1);
            exportPrimitive(ctx,
                            primitives[0],
                            i,
                            mesh,
                            mesh.indices,
                            positionsAccessor,
                            normalsAccessor,
                            tangentsAccessor,
                            uvsAccessor,
                            colorsAccessor,
                            jointsAccessors,
                            weightsAccessors,
                            mesh.material,
                            mesh.doubleSided,
                            false);
        }
    }
    return true;
}

bool
exportGltf(const ExportGltfOptions& options, UsdData& usd, tinygltf::Model& gltf)
{
    ExportGltfContext ctx;
    ctx.options = options;
    ctx.usd = &usd;
    ctx.gltf = &gltf;
    exportMetadata(ctx);
    exportMaterials(ctx);
    exportMeshes(ctx);

    int offsetNode = -1;
    if (usd.nodes.size()) {
        gltf.scenes.push_back(tinygltf::Scene());
        tinygltf::Scene& scene = gltf.scenes[0];

        // Gltf doesn't have a global orientation or scaling, so we fix with a correction node
        // Note that in that case, the correction node now acts as the holder of all root nodes.
        // Note also when creating nodes, we pass an offset to correct children indices.
        offsetNode = exportOffsetNode(gltf, usd.upAxis, usd.metersPerUnit);

        int offset = 0;
        if (offsetNode != -1) {
            // XXX we assume the offset node is the first node and all the indices shift by one,
            // relative to the indices in the UsdData
            offset = 1;
            scene.nodes.push_back(offsetNode);
            for (size_t i = 0; i < usd.rootNodes.size(); i++) {
                gltf.nodes[offsetNode].children.push_back(usd.rootNodes[i] + offset);
            }
        } else {
            scene.nodes = usd.rootNodes;
        }

        for (size_t i = 0; i < usd.nodes.size(); i++) {
            exportNode(ctx, usd.nodes[i], offset);
        }
    }
    exportSkeletons(ctx, offsetNode);

    // Convert extension sets into vectors
    gltf.extensionsUsed =
      std::vector<std::string>(ctx.extensionsUsed.begin(), ctx.extensionsUsed.end());
    gltf.extensionsRequired =
      std::vector<std::string>(ctx.extensionsRequired.begin(), ctx.extensionsRequired.end());

    return true;
}

}
