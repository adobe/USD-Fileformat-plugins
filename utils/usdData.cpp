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
#include "usdData.h"
#include "common.h"
#include "debugCodes.h"
#include <iomanip>

using namespace PXR_NS;

namespace adobe::usd {

bool
Input::isEmpty() const
{
    return image == -1 && value.IsEmpty();
}

int
Input::numChannels() const
{
    if (image >= 0) {
        if (channel == AdobeTokens->r || channel == AdobeTokens->g || channel == AdobeTokens->b ||
            channel == AdobeTokens->a) {
            return 1;
        } else if (channel == AdobeTokens->rgb) {
            return 3;
        } else if (channel == AdobeTokens->rgba) {
            return 4;
        }
    } else {
        if (value.IsHolding<float>()) {
            return 1;
        } else if (value.IsHolding<GfVec2f>()) {
            return 2;
        } else if (value.IsHolding<GfVec3f>()) {
            return 3;
        } else if (value.IsHolding<GfVec4f>()) {
            return 4;
        }
    }
    return -1;
}

bool
Input::isZeroInput() const
{
    return image >= 0 ? isZeroTexture() : isZeroValue();
}

bool
Input::isZeroTexture() const
{
    // If scale and bias are zero, the texture will only produce zero values
    PXR_NS::GfVec4f scaleValue = scale.GetWithDefault(PXR_NS::GfVec4f(1.0f));
    PXR_NS::GfVec4f biasValue = bias.GetWithDefault(PXR_NS::GfVec4f(0.0f));
    // Note, we're only checking the first three, since the multipliers are usually stored there
    return scaleValue[0] == 0.0f && scaleValue[1] == 0.0f && scaleValue[2] == 0.0f &&
           biasValue == PXR_NS::GfVec4f(0.0f);
}

bool
Input::isZeroValue() const
{
    if (value.IsHolding<float>()) {
        return value.UncheckedGet<float>() == 0.0f;
    } else if (value.IsHolding<GfVec2f>()) {
        return value.UncheckedGet<GfVec2f>() == GfVec2f(0.0f);
    } else if (value.IsHolding<GfVec3f>()) {
        return value.UncheckedGet<GfVec3f>() == GfVec3f(0.0f);
    } else if (value.IsHolding<GfVec4f>()) {
        return value.UncheckedGet<GfVec4f>() == GfVec4f(0.0f);
    }
    return false;
}

Input
invertInput(const Input& in)
{
    Input out = in;
    if (in.image >= 0) {
        // Preserve old scale and bias
        // Original transformation is: y = (scale)x + (bias)
        // Invert transformation is: (-1)y + (+1) = (-1)(scale)x + (-1)(bias) + (+1)
        //                                          -----------    -----------------
        //                                           newScale       newBias
        GfVec4f oldScale = in.scale.GetWithDefault(GfVec4f(1.0f));
        GfVec4f oldBias = in.bias.GetWithDefault(GfVec4f(0.0f));
        out.scale = -oldScale;
        out.bias = -oldBias + GfVec4f(1.0f);
    } else if (!in.value.IsEmpty()) {
        if (in.value.IsHolding<float>()) {
            out.value = 1.0f - in.value.UncheckedGet<float>();
        } else if (in.value.IsHolding<GfVec2f>()) {
            out.value = GfVec2f(1.0f) - in.value.UncheckedGet<GfVec2f>();
        } else if (in.value.IsHolding<GfVec3f>()) {
            out.value = GfVec3f(1.0f) - in.value.UncheckedGet<GfVec3f>();
        } else if (in.value.IsHolding<GfVec4f>()) {
            out.value = GfVec4f(1.0f) - in.value.UncheckedGet<GfVec4f>();
        }
    }
    return out;
}

std::string
printInput(const TfToken& name, const Input& input)
{
    std::ostringstream ss;
    ss << "\n    " << std::setfill(' ') << std::setw(20) << std::left << name.GetString() << ": ";
    if (input.image >= 0) {
        ss << std::setfill(' ') << std::setw(3) << std::right << input.image
           << ", ch: " << std::setfill(' ') << std::setw(4) << std::right
           << input.channel
           // << ", ch: " << input.channel
           << ", uv: " << input.uvIndex;
        if (!input.wrapS.IsEmpty()) {
            ss << ", wrapS: " << input.wrapS;
        }
        if (!input.wrapS.IsEmpty()) {
            ss << ", wrapT: " << input.wrapT;
        }
        if (!input.colorspace.IsEmpty()) {
            ss << ", colorspace: " << input.colorspace;
        }
        if (!input.bias.IsEmpty()) {
            ss << ", bias: " << input.bias;
        }
        if (!input.scale.IsEmpty()) {
            ss << ", scale: " << input.scale;
        }
        if (!input.transformRotation.IsEmpty()) {
            ss << ", stRot: " << input.transformRotation;
        }
        if (!input.transformScale.IsEmpty()) {
            ss << ", stScale: " << input.transformScale;
        }
        if (!input.transformTranslation.IsEmpty()) {
            ss << ", stTrans: " << input.transformTranslation;
        }
    } else if (!input.value.IsEmpty()) {
        ss << std::setprecision(3);
        ss << "<";
        if (input.value.IsHolding<int>()) {
            ss << input.value.Get<int>();
        } else if (input.value.IsHolding<float>()) {
            ss << input.value.Get<float>();
        } else if (input.value.IsHolding<PXR_NS::GfVec3f>()) {
            GfVec3f val = input.value.Get<GfVec3f>();
            ss << val[0] << "," << val[1] << "," << val[2];
        }
        ss << ">";
    }
    return ss.str();
}

std::string
printClearcoatModelsTransmissionTint(const Material& material)
{
    if (!material.clearcoatModelsTransmissionTint) {
        return {};
    } else {
        return "\n    clearcoatModelsTransmissionTint = true";
    }
}

void
printMaterial(const std::string& header,
              const SdfPath& path,
              const Material& material,
              const std::string& debugTag)
{
    TF_DEBUG_MSG(
      FILE_FORMAT_UTIL,
      "%s: %s material { %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
      debugTag.c_str(),
      header.c_str(),
      path.GetAsString().c_str(),
      printInput(AdobeTokens->useSpecularWorkflow, material.useSpecularWorkflow).c_str(),
      printInput(AdobeTokens->diffuseColor, material.diffuseColor).c_str(),
      printInput(AdobeTokens->emissiveColor, material.emissiveColor).c_str(),
      printInput(AdobeTokens->specularLevel, material.specularLevel).c_str(),
      printInput(AdobeTokens->specularColor, material.specularColor).c_str(),
      printInput(AdobeTokens->normal, material.normal).c_str(),
      printInput(AdobeTokens->metallic, material.metallic).c_str(),
      printInput(AdobeTokens->roughness, material.roughness).c_str(),
      printInput(AdobeTokens->coatOpacity, material.clearcoat).c_str(),
      printInput(AdobeTokens->coatColor, material.clearcoatColor).c_str(),
      printInput(AdobeTokens->coatRoughness, material.clearcoatRoughness).c_str(),
      printInput(AdobeTokens->coatIOR, material.clearcoatIor).c_str(),
      printInput(AdobeTokens->coatSpecularLevel, material.clearcoatSpecular).c_str(),
      printInput(AdobeTokens->coatNormal, material.clearcoatNormal).c_str(),
      printInput(AdobeTokens->sheenColor, material.sheenColor).c_str(),
      printInput(AdobeTokens->sheenRoughness, material.sheenRoughness).c_str(),
      printInput(AdobeTokens->anisotropyLevel, material.anisotropyLevel).c_str(),
      printInput(AdobeTokens->anisotropyAngle, material.anisotropyAngle).c_str(),
      printInput(AdobeTokens->opacity, material.opacity).c_str(),
      printInput(AdobeTokens->opacityThreshold, material.opacityThreshold).c_str(),
      printInput(AdobeTokens->displacement, material.displacement).c_str(),
      printInput(AdobeTokens->occlusion, material.occlusion).c_str(),
      printInput(AdobeTokens->ior, material.ior).c_str(),
      printInput(AdobeTokens->translucency, material.transmission).c_str(),
      printInput(AdobeTokens->volumeThickness, material.thickness).c_str(),
      printInput(AdobeTokens->absorptionDistance, material.absorptionDistance).c_str(),
      printInput(AdobeTokens->absorptionColor, material.absorptionColor).c_str(),
      printInput(AdobeTokens->scatteringDistance, material.scatteringDistance).c_str(),
      printInput(AdobeTokens->scatteringColor, material.scatteringColor).c_str(),
      printClearcoatModelsTransmissionTint(material).c_str());
}

void
printMesh(const std::string& header, const Mesh& mesh, const std::string& debugTag)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: %s mesh { inst: %s, faces: %zu, indcs: %zu, pos: %zu, norms: %zu, uvs: %zu, "
                 "tangents: %zu, joints: %zu, weights: %zu, infCount: %d, mat: %d }\n",
                 debugTag.c_str(),
                 header.c_str(),
                 mesh.instanceable ? "yes" : "no",
                 mesh.faces.size(),
                 mesh.indices.size(),
                 mesh.points.size(),
                 mesh.normals.values.size(),
                 mesh.uvs.values.size(),
                 mesh.tangents.values.size(),
                 mesh.joints.size(),
                 mesh.weights.size(),
                 mesh.influenceCount,
                 mesh.material);
}

void
printSkeleton(const std::string& header,
              const SdfPath& path,
              const Skeleton& skeleton,
              const std::string& debugTag)
{
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "%s: %s skeleton { %s name: %s, %zu }\n",
                 debugTag.c_str(),
                 header.c_str(),
                 path.GetAsString().c_str(),
                 skeleton.name.c_str(),
                 skeleton.joints.size());
}

ImageFormat
getFormat(const std::string& extension)
{
    if (extension == "bmp")
        return ImageFormatBmp;
    else if (extension == "exr")
        return ImageFormatExr;
    else if (extension == "jpg")
        return ImageFormatJpg;
    else if (extension == "jpeg")
        return ImageFormatJpg;
    else if (extension == "png")
        return ImageFormatPng;
    else if (extension == "psd")
        return ImageFormatPsd;
    else if (extension == "tga")
        return ImageFormatTga;
    else if (extension == "tiff")
        return ImageFormatTiff;
    else if (extension == "webp")
        return ImageFormatWebp;
    else
        TF_WARN("getFormat for unsupported extension '%s'", extension.c_str());
    return ImageFormatUnknown;
}

std::string
getFormatExtension(ImageFormat format)
{
    switch (format) {
        case ImageFormatBmp:
            return "bmp";
        case ImageFormatExr:
            return "exr";
        case ImageFormatJpg:
            return "jpg";
        case ImageFormatPng:
            return "png";
        case ImageFormatPsd:
            return "psd";
        case ImageFormatTga:
            return "tga";
        case ImageFormatTiff:
            return "tiff";
        case ImageFormatWebp:
            return "webp";
        case ImageFormatUnknown:
        default:
            TF_WARN("getFormatExtension for unknown extension");
            return {};
    }
}

std::pair<int, Node&>
UsdData::addNode(int parent)
{
    int index = nodes.size();
    nodes.push_back(Node());
    Node& node = nodes[index];
    node.parent = parent;
    if (parent >= 0) {
        nodes[parent].children.push_back(index);
    } else {
        rootNodes.push_back(index);
    }
    return { index, node };
}

std::pair<int, Node&>
UsdData::getParent(int parent)
{
    if (parent >= 0) {
        return { parent, nodes[parent] };
    } else {
        return addNode(parent);
    }
}

std::pair<int, Mesh&>
UsdData::addMesh()
{
    int index = meshes.size();
    meshes.push_back(Mesh());
    return { index, meshes[index] };
}

std::pair<int, Subset&>
UsdData::addSubset(int meshIndex)
{
    Mesh& m = meshes[meshIndex];
    int index = m.subsets.size();
    m.subsets.push_back(Subset());
    return { index, m.subsets[index] };
}

std::pair<int, Primvar<PXR_NS::GfVec3f>&>
UsdData::addColorSet(int meshIndex)
{
    Mesh& m = meshes[meshIndex];
    int index = m.colors.size();
    m.colors.push_back(Primvar<GfVec3f>());
    return { index, m.colors[index] };
}

std::pair<int, Primvar<float>&>
UsdData::addOpacitySet(int meshIndex)
{
    Mesh& m = meshes[meshIndex];
    int index = m.opacities.size();
    m.opacities.push_back(Primvar<float>());
    return { index, m.opacities[index] };
}

std::pair<int, Material&>
UsdData::addMaterial()
{
    int index = materials.size();
    materials.push_back(Material());
    return { index, materials[index] };
}

std::pair<int, Camera&>
UsdData::addCamera()
{
    int index = cameras.size();
    cameras.push_back(Camera());
    return { index, cameras[index] };
}

std::pair<int, ImageAsset&>
UsdData::addImage()
{
    int index = images.size();
    images.push_back(ImageAsset());
    return { index, images[index] };
}

std::pair<int, Skeleton&>
UsdData::addSkeleton()
{
    int index = skeletons.size();
    skeletons.push_back(Skeleton());
    return { index, skeletons[index] };
}

std::pair<int, Animation&>
UsdData::addAnimation()
{
    int index = animations.size();
    animations.push_back(Animation());
    return { index, animations[index] };
}

std::pair<int, NgpData&>
UsdData::addNgp()
{
    int index = ngps.size();
    ngps.push_back(NgpData());
    return { index, ngps[index] };
}

std::string
_makeValidPrimName(const std::string& name, const std::string& defaultName)
{
    return name.empty() ? defaultName : TfMakeValidIdentifier(name);
}

void
_makeUniqueAndAdd(std::unordered_map<std::string, int>& siblingNames, std::string& primName)
{
    // siblingNames is a map of names to occurences of the name.
    // This will retrieve the occurance count for the name or insert the name into the
    // map and set the occurance count to zero.
    int& count = siblingNames[primName];

    // if the count is zero, we haven't seen this name yet so know it's unique and don't need to
    // modify. We then set the occurence count for this name to 1
    if (count == 0) {
        count = 1;
    } else {
        // The name has been seen before so append the occurence count to the original name
        std::string newName = primName + std::to_string(count);
        while (1) {
            // add the new name to the map. If the count is 0, it's an unused name
            // so we can use it.
            int& newNameCount = siblingNames[newName];
            if (newNameCount == 0) {
                count++;
                newNameCount++;
                primName = std::move(newName);
                return;
            }
            // The new proposed name is also taken so append the count of it
            // to create another proposed name and then loop again to check the
            // new name for uniqueness
            newName = newName + std::to_string(newNameCount);
        }
    }
}

// Assumes type T has a std::string `name` field
template<typename T>
void
_uniquifySiblings(std::vector<T>& siblings, const std::string& defaultName)
{
    std::unordered_map<std::string, int> siblingNames;
    for (T& sibling : siblings) {
        sibling.name = _makeValidPrimName(sibling.name, defaultName);
        _makeUniqueAndAdd(siblingNames, sibling.name);
    }
}

// Same as above, but with an indexing indirection
template<typename T>
void
_uniquifySiblings(std::vector<T>& all,
                  const std::vector<int>& siblingIndices,
                  const std::string& defaultName)
{
    std::unordered_map<std::string, int> siblingNames;
    for (int idx : siblingIndices) {
        T& sibling = all[idx];
        sibling.name = _makeValidPrimName(sibling.name, defaultName);
        _makeUniqueAndAdd(siblingNames, sibling.name);
    }
}

// Special case for meshes, since some meshes are actually points
void
_uniquifySiblingMeshes(std::vector<Mesh>& all, const std::vector<int>& siblingIndices)
{
    static const std::string pointsStr = "Points";
    static const std::string meshStr = "Mesh";
    std::unordered_map<std::string, int> siblingNames;

    for (int idx : siblingIndices) {
        Mesh& sibling = all[idx];
        sibling.name = _makeValidPrimName(sibling.name, sibling.asPoints ? pointsStr : meshStr);
        _makeUniqueAndAdd(siblingNames, sibling.name);
    }
}

void
_uniquifyNode(UsdData& data, Node& node)
{
    _uniquifySiblings(data.nurbs, node.nurbs, "Nurb");
    _uniquifySiblingMeshes(data.meshes, node.staticMeshes);
    for (const auto& [skeletonIndex, meshIndices] : node.skinnedMeshes) {
        _uniquifySiblingMeshes(data.meshes, meshIndices);
    }
    _uniquifySiblings(data.nodes, node.children, "Node");

    for (int idx : node.children) {
        _uniquifyNode(data, data.nodes[idx]);
    }
}

// Ideally this function would also convert all prim names to tokens for efficiency
void
uniquifyNames(UsdData& data)
{
    // Cameras are (currently) always children of a node with a unique name, hence they don't need
    // unique names, just valid prim names
    for (Camera& camera : data.cameras) {
        camera.name = _makeValidPrimName(camera.name, "Camera");
    }
    _uniquifySiblings(data.materials, "Material");
    _uniquifySiblings(data.skeletons, "Skeleton");
    _uniquifySiblings(data.animations, "Animation");

    if (!data.rootNodes.empty()) {
        _uniquifySiblings(data.nodes, data.rootNodes, "Node");
        for (int idx : data.rootNodes) {
            _uniquifyNode(data, data.nodes[idx]);
        }
    } else {
        _uniquifySiblings(data.nodes, "Node");
        for (Node& node : data.nodes) {
            _uniquifyNode(data, node);
        }
    }
}

}
