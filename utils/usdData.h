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
#pragma once

/// \file usdData.h
///
/// These structs are used to avoid redefining read/write operations on the USD layer in each
/// plugin. Instead, each plugin just passes data to/from these stucts, and then reuses the
/// \link readLayer \endlink and \link writeLayer \endlink functions.
/// Intended as data-structs, however some might define handy methods make edits easier.
/// Where possible std::vector is used, since it's more easily debuggable than PXR_NS::VtArray.
/// PXR_NS::VtArray is used for more direct data transfer to/from USD.

#include "api.h"
#include "common.h"
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/material.h>
#include <string>
#include <vector>

namespace adobe::usd {

/// \ingroup utils_animations
template<typename T>
struct USDFFUTILS_API TimeValues
{
    PXR_NS::VtArray<float> times;
    PXR_NS::VtArray<T> values;
};

/// \ingroup utils_nodes
/// \brief A cache for Xform data, including TRS properties, transform matrix, and associated child
/// caches like other nodes, meshes, cameras, etc. A collection of these will drive how layerWrite
/// will author Xform prims and its children prims.
///
/// `staticMeshes` references a group of non-skinned meshes indices.
/// `skinnedMeshes` associates a skeleton index, with a group of mesh indices.
struct USDFFUTILS_API Node
{
    std::string name;
    bool hasTransform = false;
    PXR_NS::GfMatrix4d transform = PXR_NS::GfMatrix4d(1);
    PXR_NS::GfMatrix4d worldTransform = PXR_NS::GfMatrix4d(1);
    PXR_NS::GfVec3d translation = PXR_NS::GfVec3d(0.0, 0.0, 0.0);
    PXR_NS::GfQuatf rotation = PXR_NS::GfQuatf(0.0f, 0.0f, 0.0f, 0.0f);
    PXR_NS::GfVec3f scale = PXR_NS::GfVec3f(1.0f, 1.0f, 1.0f);
    // XXX the single translation is GfVec3d, but the translations is GfVec3f. That should be
    // GfVec3d as well
    TimeValues<PXR_NS::GfVec3f> translations;
    TimeValues<PXR_NS::GfQuatf> rotations;
    TimeValues<PXR_NS::GfVec3f> scales;
    int parent = -1;
    int camera = -1;
    int ngp = -1;
    int light = -1;
    std::vector<int> nurbs = {};
    std::vector<int> staticMeshes = {};
    std::unordered_map<int, std::vector<int>> skinnedMeshes = {};
    std::vector<int> children = {};

    std::string path;
    bool isJoint = false;
};

/// \ingroup utils_geometry
/// \brief Camera data
struct USDFFUTILS_API Camera
{
    std::string name;
    PXR_NS::GfCamera::Projection projection;
    float f;
    float horizontalAperture;
    float verticalAperture;
    float nearZ;
    float farZ;
    float fStop;
    float focusDistance;
    // Members below are used for importing/exporting cameras from fbx and gltf files.
    PXR_NS::GfCamera camera;
    float fov;
    float aspectRatio;
};

/// \ingroup utils_geometry
/// \brief Mesh subsets data
struct USDFFUTILS_API Subset
{
    PXR_NS::VtIntArray faces;   // indices to a subset of geom faces
    PXR_NS::VtIntArray indices; // subset of geom indices
    int material;
};

/// \ingroup utils_geometry
/// \brief Primvar data
template<typename T>
struct USDFFUTILS_API Primvar
{
    PXR_NS::TfToken interpolation = PXR_NS::UsdGeomTokens->constant;
    PXR_NS::VtArray<T> values;
    PXR_NS::VtIntArray indices;
};

/// \ingroup utils_geometry
/// \brief Mesh data
struct USDFFUTILS_API Mesh
{
    std::string name;
    PXR_NS::VtIntArray faces;
    PXR_NS::VtIntArray indices;
    PXR_NS::VtVec3fArray points;
    PXR_NS::VtFloatArray pointWidths;
    Primvar<PXR_NS::GfVec3f> normals;
    // XXX tangents in USD are usually GfVec3f and are only supported by Hermite curves. Something
    // is not quite right
    Primvar<PXR_NS::GfVec4f> tangents;
    Primvar<PXR_NS::GfVec2f> uvs;
    std::vector<Primvar<PXR_NS::GfVec2f>> extraUVSets;
    std::vector<Primvar<PXR_NS::GfVec3f>> colors;
    std::vector<Primvar<float>> opacities;
    std::vector<Primvar<float>> pointExtraWidths;
    std::vector<Primvar<float>> pointSHCoeffs;
    Primvar<PXR_NS::GfQuatf> pointRotations;
    PXR_NS::VtIntArray joints;
    PXR_NS::VtFloatArray weights;
    int material = -1;
    std::vector<Subset> subsets;
    bool doubleSided = false;
    bool instanceable = false;
    bool asPoints = false;
    bool asGsplats = false;
    bool isRigid = false;
    int influenceCount = 1;
    PXR_NS::GfMatrix4d geomBindTransform = PXR_NS::GfMatrix4d(1.0);
    PXR_NS::TfToken subdivisionScheme = PXR_NS::UsdGeomTokens->none;
    Primvar<PXR_NS::GfVec3f> clippingBox;
};

/// \ingroup utils_geometry
/// \brief Nurb data
struct USDFFUTILS_API NurbData
{
    std::string name;
    int knotType;
    int surfaceForm;
    int uOrder;
    int vOrder;
    int uControlPointCount;
    int vControlPointCount;
    PXR_NS::VtArray<PXR_NS::GfVec3f> controlPoints;
    PXR_NS::VtArray<double> uKnots;
    PXR_NS::VtArray<double> vKnots;
    PXR_NS::VtArray<double> weights;
    PXR_NS::VtArray<int> trimCurveCounts;
    PXR_NS::VtArray<double> trimCurveKnots;
    PXR_NS::VtArray<int> trimCurveOrders;
    PXR_NS::VtArray<PXR_NS::GfVec3d> trimCurvePoints;
    PXR_NS::VtArray<PXR_NS::GfVec2d> trimCurveRanges;
    PXR_NS::VtArray<int> trimCurveVertexCounts;
};

/// \ingroup utils_geometry
/// \brief Ngp data
struct USDFFUTILS_API NgpData
{
    float densityThreshold;
    bool hasTransform = false;
    PXR_NS::VtFloatArray densityMlpLayer0Bias;
    PXR_NS::VtFloatArray densityMlpLayer0Weight;
    PXR_NS::VtFloatArray densityMlpLayer1Bias;
    PXR_NS::VtFloatArray densityMlpLayer1Weight;
    PXR_NS::VtFloatArray colorMlpLayer0Bias;
    PXR_NS::VtFloatArray colorMlpLayer0Weight;
    PXR_NS::VtFloatArray colorMlpLayer1Bias;
    PXR_NS::VtFloatArray colorMlpLayer1Weight;
    PXR_NS::VtFloatArray colorMlpLayer2Bias;
    PXR_NS::VtFloatArray colorMlpLayer2Weight;
    PXR_NS::VtFloatArray densityGrid;
    PXR_NS::VtFloatArray distanceGrid;
    PXR_NS::VtFloatArray hashGrid;
    PXR_NS::VtUIntArray hashGridResolution;
    PXR_NS::GfMatrix4d transform = PXR_NS::GfMatrix4d(1);
};

/// \ingroup utils_skeletons
struct USDFFUTILS_API Animation
{
    std::string name;
    PXR_NS::VtArray<PXR_NS::TfToken> joints;
    std::vector<float> times;
    std::vector<PXR_NS::VtArray<PXR_NS::GfQuatf>> rotations;
    std::vector<PXR_NS::VtArray<PXR_NS::GfVec3f>> translations;
    std::vector<PXR_NS::VtArray<PXR_NS::GfVec3h>> scales;
};

/// \ingroup utils_skeletons
/// \brief Skeleton data
struct USDFFUTILS_API Skeleton
{
    std::string name;
    std::vector<int> parents;
    std::vector<int> targets;
    PXR_NS::VtTokenArray joints;
    PXR_NS::VtTokenArray jointNames;
    PXR_NS::VtMatrix4dArray restTransforms;
    PXR_NS::VtArray<PXR_NS::GfMatrix4f> inverseBindMatricesFloat; // used for import
    PXR_NS::VtMatrix4dArray inverseBindTransforms;                // used for export
    PXR_NS::VtMatrix4dArray bindTransforms;
    PXR_NS::VtArray<int> animations;
};

enum USDFFUTILS_API ImageFormat
{
    ImageFormatUnknown,
    ImageFormatBmp,
    ImageFormatExr,
    ImageFormatJpg,
    ImageFormatPng,
    ImageFormatPsd,
    ImageFormatTga,
    ImageFormatTiff,
    ImageFormatWebp,
};

struct USDFFUTILS_API ImageAsset
{
    std::string name;
    std::string uri;
    ImageFormat format = ImageFormatUnknown;
    std::vector<uint8_t> image;
};
USDFFUTILS_API ImageFormat
getFormat(const std::string& extension);
USDFFUTILS_API std::string
getFormatExtension(ImageFormat format);

enum USDFFUTILS_API LightType
{
    Disk,
    Rectangle,
    Sphere,
    Environment,
    Sun,
};

struct USDFFUTILS_API Light
{
    std::string name;
    LightType type;
    PXR_NS::GfVec3f color;
    PXR_NS::GfVec2f length; // Rect light dimensions.
    float intensity;
    float radius;
    float coneAngle;    // Control the light spread for disk light.
    float coneFalloff;  // Control the cutoff for disk light.
    float angle;        // Angular size of distant/sun light.
    ImageAsset texture; // IBL texture.
};

/// \ingroup utils_materials
/// \brief Material Input data
struct USDFFUTILS_API Input
{
    PXR_NS::VtValue value;
    int image = -1;
    int uvIndex = 0;
    PXR_NS::TfToken channel;
    PXR_NS::TfToken wrapS;
    PXR_NS::TfToken wrapT;
    PXR_NS::TfToken minFilter;
    PXR_NS::TfToken magFilter;
    PXR_NS::TfToken colorspace;
    PXR_NS::VtValue scale;
    PXR_NS::VtValue bias;
    PXR_NS::VtValue transformRotation;
    PXR_NS::VtValue transformScale;
    PXR_NS::VtValue transformTranslation;

    bool isEmpty() const;
    int numChannels() const;
    bool isZeroInput() const;
    bool isZeroTexture() const;
    bool isZeroValue() const;
};

/// \ingroup utils_materials
/// \brief Material data
struct USDFFUTILS_API Material
{
    std::string name;

    // Import of transmission from GLTF can activate the clearcoat lobe to model tinting of
    // transmission, which ASM doesn't do automatically. If this was activated on import, we do
    // not want to export clearcoat to GLTF again.
    bool clearcoatModelsTransmissionTint = false;

    Input useSpecularWorkflow;
    Input diffuseColor;
    Input emissiveColor;
    Input specularLevel;
    Input specularColor;
    Input normal;
    Input normalScale;
    Input metallic;
    Input roughness;
    Input clearcoat;
    Input clearcoatColor;
    Input clearcoatRoughness;
    Input clearcoatIor;
    Input clearcoatSpecular;
    Input clearcoatNormal;
    Input sheenColor;
    Input sheenRoughness;
    Input anisotropyLevel;
    Input anisotropyAngle;
    Input opacity;
    Input opacityThreshold;
    Input displacement;
    Input occlusion;
    Input ior;
    Input transmission;
    Input volumeThickness;
    Input absorptionDistance;
    Input absorptionColor;
    Input scatteringDistance;
    Input scatteringColor;
};

/// \ingroup utils_layer
/// \brief An aggregation of different caches of USD data.
/// * During export, layerRead dumps data from the USD stage into this struct, for exporters to take
///   and author data in their file formats of origin.
/// * During import, importers dump data from their file formats of origin into this struct, for
///   layerWrite to take and author data in the USD stage.
struct USDFFUTILS_API UsdData
{
    // Layer metadata
    // Note, upAxis and metersPerUnit are left intentially empty, so that they only get authored
    // if that information is actually available to the plugin.
    PXR_NS::TfToken upAxis;
    double metersPerUnit = 0.0;
    std::string doc;
    PXR_NS::VtDictionary metadata;
    bool hasAnimations = false;
    float minTime = std::numeric_limits<int>::max();
    float maxTime = 0;
    double timeCodesPerSecond = 24;

    std::vector<int> rootNodes;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Camera> cameras;
    std::vector<NurbData> nurbs;
    std::vector<ImageAsset> images;
    std::vector<Light> lights;
    std::vector<Material> materials;
    std::vector<Skeleton> skeletons;
    std::vector<Animation> animations;
    std::vector<NgpData> ngps;

    std::pair<int, Node&> addNode(int parent);
    std::pair<int, Node&> getParent(int parent);
    std::pair<int, Mesh&> addMesh();
    std::pair<int, Subset&> addSubset(int meshIndex);
    std::pair<int, Primvar<PXR_NS::GfVec3f>&> addColorSet(int meshIndex);
    std::pair<int, Primvar<float>&> addOpacitySet(int meshIndex);
    std::pair<int, Primvar<float>&> addExtraPointWidthSet(int meshIndex);
    std::pair<int, Primvar<float>&> addPointSHCoeffSet(int meshIndex);
    std::pair<int, Material&> addMaterial();
    std::pair<int, ImageAsset&> addImage();
    std::pair<int, Light&> addLight();
    std::pair<int, Camera&> addCamera();
    std::pair<int, Skeleton&> addSkeleton();
    std::pair<int, Animation&> addAnimation();
    std::pair<int, NgpData&> addNgp();
};

// Returns true if the Input has a constant value of supported type
// The final constant value is computed by applying the scale and bias to that value before it is
// returned.
template<typename T>
bool
getInputValue(const Input& input, T* value)
{
    if (!input.value.IsHolding<T>()) {
        return false;
    }

    T v = input.value.UncheckedGet<T>();

    PXR_NS::GfVec4f scale = input.scale.GetWithDefault<PXR_NS::GfVec4f>(PXR_NS::GfVec4f(1.0f));
    PXR_NS::GfVec4f bias = input.bias.GetWithDefault<PXR_NS::GfVec4f>(PXR_NS::GfVec4f(0.0f));

    if constexpr (std::is_same_v<T, float>) {
        *value = scale[0] * v + bias[0];
    } else if constexpr (std::is_same_v<T, PXR_NS::GfVec2f>) {
        *value = PXR_NS::GfVec2f(scale[0], scale[1]) * v + PXR_NS::GfVec2f(bias[0], bias[1]);
    } else if constexpr (std::is_same_v<T, PXR_NS::GfVec3f>) {
        *value = PXR_NS::GfVec3f(scale[0], scale[1], scale[2]) * v +
                 PXR_NS::GfVec3f(bias[0], bias[1], bias[2]);
    } else if constexpr (std::is_same_v<T, PXR_NS::GfVec4f>) {
        *value = scale * v + bias;
    } else {
        return false;
    }

    return true;
}

// Returns an inverted version of the input as if a scale=-1 and bias=1 were applied
USDFFUTILS_API Input
invertInput(const Input& in);

USDFFUTILS_API void
printMaterial(const std::string& header,
              const PXR_NS::SdfPath& path,
              const Material& material,
              const std::string& debugTag);
USDFFUTILS_API void
printMesh(const std::string& header, const Mesh& mesh, const std::string& debugTag);
// void printImage(const std::string& header, const SdfPath& path, const ImageAsset& image);
USDFFUTILS_API void
printSkeleton(const std::string& header,
              const PXR_NS::SdfPath& path,
              const Skeleton& skeleton,
              const std::string& debugTag);

// Makes sure that siblings in the hierarchy have unique names and that the names are valid USD prim
// names
USDFFUTILS_API void
uniquifyNames(UsdData& data);

class USDFFUTILS_API UniqueNameEnforcer
{
    std::unordered_map<std::string, int> namesMap;

  public:
    void enforceUniqueness(std::string& name);
};

// Currently used by the FBX and OBJ plugins whose color space data may either be linear
// or sRGB.  This checks if the outputColorSpace if specifically set, if not, it will
// check the USD metatadata for the original color space.
USDFFUTILS_API bool
shouldConvertToSRGB(const UsdData& usd, const std::string& outputColorSpace);

}
