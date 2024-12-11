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

/// \file obj.h
/// \brief Implementation of obj data read/write.
///
/// Specifications:
/// obj: http://fegemo.github.io/cefet-cg/attachments/obj-spec.pdf
/// mtl: http://paulbourke.net/dataformats/mtl/
/// pbr-extension: http://exocortex.com/blog/extending_wavefront_mtl_to_support_pbr

#include <iosfwd>
#include <map>
#include <fileformatutils/usdData.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/pxr.h>
#include <string>
#include <vector>

namespace adobe::usd {

/// \enum ObjMapChannel
/// \brief Obj map channel
enum ObjMapChannel
{
    ObjMapChannelR,
    ObjMapChannelG,
    ObjMapChannelB,
    ObjMapChannelM,
    ObjMapChannelL,
    ObjMapChannelZ
};

/// \struct ObjMap
/// \brief Obj material map
/// Obj materials are often described by a scalar attribute
/// and a texture attribute, the latter called map.
struct ObjMap
{
    bool defined = false;
    std::string filename;
    int image = -1;
    bool blendu = true;
    bool blendv = true;
    float bumpMultiplier = 1.0f;
    float boost = 0.0f;
    bool colorCorrection = false;
    bool clamp = false;
    // The channel selection is only used by bump and scalar channels
    ObjMapChannel channel = ObjMapChannelL;
    float base = 0.0f;
    float gain = 1.0f;
    PXR_NS::GfVec3f origin = PXR_NS::GfVec3f(0.0f);
    PXR_NS::GfVec3f scale = PXR_NS::GfVec3f(1.0f);
    PXR_NS::GfVec3f turbulence = PXR_NS::GfVec3f(0.0f);
};

/// \struct ObjMaterial
/// \brief Obj material.
/// Contains some extended attributes regarding PBR, and Adobe specific attributes.
struct ObjMaterial
{
    bool defined = false;
    bool mdlDefined = false;
    std::string name;
    int illum = -1;
    PXR_NS::GfVec3f ka = PXR_NS::GfVec3f(-1);
    PXR_NS::GfVec3f kd = PXR_NS::GfVec3f(-1);
    PXR_NS::GfVec3f ks = PXR_NS::GfVec3f(-1);
    PXR_NS::GfVec3f tf = PXR_NS::GfVec3f(-1);
    PXR_NS::GfVec3f ke = PXR_NS::GfVec3f(-1);
    float d = -1;
    bool hasHalo = false;
    float ns = -1;
    float sharpness = -1;
    float ni = -1;
    ObjMap mapKa;
    ObjMap mapKd;
    ObjMap mapKs;
    ObjMap mapNs;
    ObjMap mapKe;
    ObjMap mapD;
    ObjMap norm;
    ObjMap decal;
    ObjMap disp;
    ObjMap bump;

    // Extended attributes for PBR
    float roughness = -1;
    float metallic = -1;
    ObjMap mapRoughness;
    ObjMap mapMetallic;

    // Extended attributes from Adobe
    // baseColor is shared with kd
    // normal is shared with norm
    PXR_NS::GfVec3f interiorColor = PXR_NS::GfVec3f(-1);
    float opacity = -1;
    float height = -1;
    float heightScale = -1;
    float glow = -1;
    float translucence = -1;
    float density = -1;
    ObjMap mapOpacity;
    ObjMap mapHeight;
    ObjMap mapGlow;
    ObjMap mapTranslucence;

    ObjMaterial(const std::string& name_ = {})
      : name(name_)
    {
        // The decal map defaults to the matte channel
        decal.channel = ObjMapChannelM;
    }
};

/// \struct ObjMaterialLibrary
/// \brief Obj material library.
/// Note we support standard material libraries from `.mtl`
/// as well as adobe stock model material ibraries from `.mdl`.
struct ObjMaterialLibrary
{
    std::string filename;
    bool isMdl = false;
    std::vector<int> materials;
};

/// \struct ObjSubset
/// \brief Obj subset.
/// Contains a subset of the faces of the parent group,
/// and links to a material.
struct ObjSubset
{
    int material = -1;
    PXR_NS::VtIntArray faces;
};

/// \struct ObjGroup
/// \brief Obj group.
/// A group contains geometry, subsets and is linked to a material.
struct ObjGroup
{
    std::string name;
    PXR_NS::VtVec3fArray vertices;
    PXR_NS::VtVec3fArray colors;
    PXR_NS::VtVec2fArray uvs;
    PXR_NS::VtVec3fArray normals;
    PXR_NS::VtVec3fArray sVertices;
    PXR_NS::VtIntArray faces;
    PXR_NS::VtIntArray indices;
    PXR_NS::VtIntArray uvIndices;
    PXR_NS::VtIntArray normalIndices;
    std::vector<ObjSubset> subsets;
    int material = -1;
};

/// \struct ObjObject
/// \brief Obj object.
/// An object contains several groups.
struct ObjObject
{
    std::string name;
    std::vector<ObjGroup> groups;
};

/// \struct Obj
/// \brief Obj data cache.
/// Use `readObj` to read data into this struct.
/// USe `writeObj` to write data from this struct.
struct Obj
{
    bool hasAdobeProperties = false;
    std::set<std::string> importedFilenames;
    std::vector<ObjObject> objects;
    std::vector<ObjMaterial> materials;
    std::vector<ImageAsset> images;
    std::vector<ObjMaterialLibrary> libraries;
    std::vector<std::string> comments;
    std::vector<std::string> arbitraryText;

    // This is passed in as a fileformat argument on import
    // It will be used by the exporter if the outputColorSpace is not set
    PXR_NS::TfToken originalColorSpace;

    // This is passed in as a fileformat argument on export
    // This will take priority over the originalColorSpace if set
    PXR_NS::TfToken outputColorSpace;
};

/// \fn readObj
/// \brief Read an obj from file `filename` and store it in `obj`.
/// Optionally reads in the images if `readImages` is true.
/// Due to the way we resolve images in usd from the origin file format,
/// `readImages` will be false during import, and true during asset resolution.
bool
readObj(Obj& obj, const std::string& filename, bool readImages);

/// \fn readObj
/// \brief Read an obj from the buffer `data` and store it in `obj`.
/// Note this does not carry material data.
bool
readObj(Obj& obj, const std::vector<char>& data);

/// \fn writeObj
/// \brief Write an obj from `obj` to the file `filename`.
bool
writeObj(const Obj& obj, const std::string& filename, bool sameMaterialName);

/// \fn writeObj
/// \brief Write an obj from `obj` to the `output` string.
bool
writeObj(const Obj& obj, std::string& output);

}
