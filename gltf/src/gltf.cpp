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
#include "gltf.h"
#include "debugCodes.h"
#include "neuralAssetsHelper.h"
#include <common.h>
#include <iostream>
#include <limits>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/utils.h>
#include <tiny_gltf.h>

// Defined in IMPLEMENTATION sector in tinygltf.h but needed here
namespace tinygltf {
std::string
base64_encode(unsigned char const*, unsigned int len);
std::string
base64_decode(std::string const& encoded_string);
}
using namespace PXR_NS;

namespace adobe::usd {
namespace {
const std::string base64Prefix = "data:application/octet-stream;base64,";
}

tinygltf::Buffer&
getBuffer(tinygltf::Model* gltf)
{
    if (gltf->buffers.empty()) {
        gltf->buffers.push_back(tinygltf::Buffer());
    }
    return gltf->buffers.back();
}

// Load image data as-is to improve load times
// It is only for the metallic-roughness texture that we will need to read images and modify them.
bool
CustomLoadImageData(tinygltf::Image* image,
                    const int imageIndex,
                    std::string* error,
                    std::string* warning,
                    int requiredWidth,
                    int requiredHeight,
                    const unsigned char* bytes,
                    int size,
                    void* userData)
{
    image->as_is = true;
    image->image.resize(static_cast<size_t>(size));
    memcpy(image->image.data(), bytes, size);
    return true;
}

bool
CustomWriteImageData(const std::string* basepathString,
                     const std::string* filenameString,
                     const tinygltf::Image* image,
                     bool embedImages,
                     const tinygltf::URICallbacks* uriCallbacks,
                     std::string* outUri,
                     void* userData)
{
    const std::string& basepath = *basepathString;
    const std::string& filename = *filenameString;
    // Only applies to gltf. Glb embedded images should have been previously saved in a buffer.
    if (embedImages) {
        if (image->image.size()) {
            const std::string extension = TfGetExtension(filename);
            std::string header;
            if (extension == "png") {
                header = "data:image/png;base64,";
            } else if (extension == "jpg" || extension == "jpeg") {
                header = "data:image/jpeg;base64,";
            } else if (extension == "bmp") {
                header = "data:image/bmp;base64,";
            } else {
                TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Unrecognized image format %s\n", extension.c_str());
                return false;
            }
            *outUri = header + tinygltf::base64_encode(image->image.data(), image->image.size());
        }
        // Both gltf and glb can save to a file
    } else {
        const std::string& outputFilename = basepath + "/" + filename;
        TfMakeDirs(basepath, -1, true); // maybe not needed
        std::ofstream file(outputFilename, std::ios::out | std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(image->image.data()), image->image.size());
            *outUri = filename;
            return true;
        }
    }
    return true;
}

bool
readGltf(tinygltf::Model& gltf, const std::string& filename)
{
    const std::string extension = TfGetExtension(filename);
    bool result = false;
    std::string err;
    std::string warn;
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(CustomLoadImageData, nullptr);
    if (extension == "gltf" || extension == "GLTF") {
        result = loader.LoadASCIIFromFile(&gltf, &err, &warn, filename);
    } else if (extension == "glb" || extension == "GLB") {
        result = loader.LoadBinaryFromFile(&gltf, &err, &warn, filename);
    } else {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "No glTF found at %s\n", filename.c_str());
    }
    if (!warn.empty()) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Warning: %s\n", warn.c_str());
    }
    if (!err.empty()) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Error: %s\n", err.c_str());
    }
    if (!result) {
        TF_DEBUG_MSG(FILE_FORMAT_GLTF, "Failed to read glTF\n");
        return false;
    }
    return true;
}

bool
readGltf(tinygltf::Model& gltf, std::string& str)
{
    return true;
}

bool
writeGltf(const WriteGltfOptions& options, tinygltf::Model& gltf, const std::string& filename)
{
    const std::string parentPath = TfGetPathName(filename);
    const std::string extension = TfGetExtension(filename);
    TfMakeDirs(parentPath, -1, true);
    bool binary = extension == "glb";
    tinygltf::TinyGLTF writer;
    writer.SetImageWriter(CustomWriteImageData, nullptr);
    return writer.WriteGltfSceneToFile(&gltf,
                                       filename,
                                       options.embedImages, // embedImages
                                       binary,              // embedBuffers
                                       true,                // prettyPrint
                                       binary               // writeBinary
    );
}

void
computeMinMax(const PXR_NS::VtArray<PXR_NS::GfVec3f>& values,
              PXR_NS::GfVec3f& minValues,
              PXR_NS::GfVec3f& maxValues)
{
    minValues = PXR_NS::GfVec3f(std::numeric_limits<float>::max());
    maxValues = PXR_NS::GfVec3f(std::numeric_limits<float>::lowest());
    for (size_t i = 0; i < values.size(); i++) {
        const PXR_NS::GfVec3f& value = values[i];
        minValues[0] = std::min(minValues[0], value[0]);
        minValues[1] = std::min(minValues[1], value[1]);
        minValues[2] = std::min(minValues[2], value[2]);
        maxValues[0] = std::max(maxValues[0], value[0]);
        maxValues[1] = std::max(maxValues[1], value[1]);
        maxValues[2] = std::max(maxValues[2], value[2]);
    }
}

void
printMatrix(const std::string& name, const PXR_NS::GfMatrix4d& m)
{
    std::cout << name << std::endl;
    std::cout << m[0][0] << ", " << m[1][0] << ", " << m[2][0] << ", " << m[3][0] << std::endl;
    std::cout << m[0][1] << ", " << m[1][1] << ", " << m[2][1] << ", " << m[3][1] << std::endl;
    std::cout << m[0][2] << ", " << m[1][2] << ", " << m[2][2] << ", " << m[3][2] << std::endl;
    std::cout << m[0][3] << ", " << m[1][3] << ", " << m[2][3] << ", " << m[3][3] << std::endl;
}

// USD stores in columns
void
copyMatrices(const PXR_NS::VtMatrix4dArray& matrices, std::vector<float>& values)
{
    values.resize(matrices.size() * 16);
    for (size_t i = 0; i < matrices.size(); i++) {
        const GfMatrix4d& m = matrices[i];
        values[16 * i] = m[0][0];
        values[16 * i + 1] = m[0][1];
        values[16 * i + 2] = m[0][2];
        values[16 * i + 3] = m[0][3];
        values[16 * i + 4] = m[1][0];
        values[16 * i + 5] = m[1][1];
        values[16 * i + 6] = m[1][2];
        values[16 * i + 7] = m[1][3];
        values[16 * i + 8] = m[2][0];
        values[16 * i + 9] = m[2][1];
        values[16 * i + 10] = m[2][2];
        values[16 * i + 11] = m[2][3];
        values[16 * i + 12] = m[3][0];
        values[16 * i + 13] = m[3][1];
        values[16 * i + 14] = m[3][2];
        values[16 * i + 15] = m[3][3];
    }
}

void
copyMatrix(const PXR_NS::GfMatrix4d& m, std::vector<double>& values)
{
    // std::cout << m[0][0] << ", " << m[0][1] << ", " << m[0][2] << ", " << m[0][3] << std::endl;
    // std::cout << m[1][0] << ", " << m[1][1] << ", " << m[1][2] << ", " << m[1][3] << std::endl;
    // std::cout << m[2][0] << ", " << m[2][1] << ", " << m[2][2] << ", " << m[2][3] << std::endl;
    // std::cout << m[3][0] << ", " << m[3][1] << ", " << m[3][2] << ", " << m[3][3] << std::endl;
    values.resize(16);
    values[0] = m[0][0];
    values[1] = m[0][1];
    values[2] = m[0][2];
    values[3] = m[0][3];
    values[4] = m[1][0];
    values[5] = m[1][1];
    values[6] = m[1][2];
    values[7] = m[1][3];
    values[8] = m[2][0];
    values[9] = m[2][1];
    values[10] = m[2][2];
    values[11] = m[2][3];
    values[12] = m[3][0];
    values[13] = m[3][1];
    values[14] = m[3][2];
    values[15] = m[3][3];
}

void
copyMatrix(const std::vector<double>& values, PXR_NS::GfMatrix4d& m)
{
    m[0][0] = values[0];
    m[0][1] = values[1];
    m[0][2] = values[2];
    m[0][3] = values[3];
    m[1][0] = values[4];
    m[1][1] = values[5];
    m[1][2] = values[6];
    m[1][3] = values[7];
    m[2][0] = values[8];
    m[2][1] = values[9];
    m[2][2] = values[10];
    m[2][3] = values[11];
    m[3][0] = values[12];
    m[3][1] = values[13];
    m[3][2] = values[14];
    m[3][3] = values[15];
}

void
decomposeMatrix(const PXR_NS::GfMatrix4d& m, tinygltf::Node& node)
{
    PXR_NS::GfVec3f translation;
    PXR_NS::GfQuatf rotation;
    PXR_NS::GfVec3h scale;
    PXR_NS::UsdSkelDecomposeTransform(m, &translation, &rotation, &scale);
    GfVec3f imaginary = rotation.GetImaginary();
    node.translation = std::vector<double>{ translation[0], translation[1], translation[2] };
    node.rotation =
      std::vector<double>{ imaginary[0], imaginary[1], imaginary[2], rotation.GetReal() };
    node.scale = std::vector<double>{ scale[0], scale[1], scale[2] };
}

template<typename T>
void
computeRange(tinygltf::Accessor& accessor, const void* data, int elementCount, int componentCount)
{
    std::vector<double> minValues(componentCount, std::numeric_limits<double>::max());
    std::vector<double> maxValues(componentCount, std::numeric_limits<double>::lowest());
    size_t entryCount = componentCount * elementCount;

    const T* values = reinterpret_cast<const T*>(data);
    for (size_t i = 0; i < entryCount; ++i) {
        double value = static_cast<double>(values[i]);
        size_t j = i % componentCount;
        minValues[j] = std::min(value, minValues[j]);
        maxValues[j] = std::max(value, maxValues[j]);
    }

    accessor.minValues = std::move(minValues);
    accessor.maxValues = std::move(maxValues);
}

template<typename T>
bool
suppressInvalidFloats(void* data, size_t numBytes)
{
    size_t numFloats = numBytes / sizeof(T);
    T* values = reinterpret_cast<T*>(data);
    bool foundInfiniteValue = false;
    for (size_t i = 0; i < numFloats; ++i) {
        if (!std::isfinite(values[i])) {
            values[i] = T(0);
            foundInfiniteValue = true;
        }
    }
    return foundInfiniteValue;
}

int
addAccessor(tinygltf::Model* gltf,
            const std::string& name,
            int target,
            int type,
            int componentType,
            int elementCount,
            const void* srcData,
            bool withRange)
{
    if (elementCount <= 0) {
        return -1;
    }

    int componentCount = tinygltf::GetNumComponentsInType(type);
    int componentSize = tinygltf::GetComponentSizeInBytes(componentType);
    tinygltf::Buffer& buffer = getBuffer(gltf);
    int currentSize = buffer.data.size();
    int extraBytes = currentSize % 4;
    int padding = extraBytes ? 4 - extraBytes : 0;
    int addedSize = componentCount * componentSize * elementCount;
    buffer.data.resize(currentSize + padding + addedSize);
    void* dstData = &buffer.data[currentSize + padding];
    memcpy(dstData, srcData, addedSize);

    // For float values we do a pass on the just copied data to suppress any non-finite values
    if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        if (suppressInvalidFloats<float>(dstData, addedSize)) {
            TF_WARN("Float data for %s had invalid values", name.c_str());
        }
    } else if (componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE) {
        if (suppressInvalidFloats<double>(dstData, addedSize)) {
            TF_WARN("Double data for %s had invalid values", name.c_str());
        }
    }

    tinygltf::BufferView bufferView;
    bufferView.name = name;
    bufferView.buffer = 0;
    bufferView.byteOffset = currentSize + padding;
    bufferView.byteLength = addedSize;
    bufferView.byteStride = 0; // tightly packed
    bufferView.target = target;
    int bufferViewIndex = gltf->bufferViews.size();
    gltf->bufferViews.push_back(bufferView);

    tinygltf::Accessor accessor;
    accessor.bufferView = bufferViewIndex;
    accessor.name = name;
    accessor.byteOffset = 0;
    accessor.normalized = false;
    accessor.componentType = componentType;
    accessor.count = elementCount;
    accessor.type = type;
    if (withRange) {
        // Note, we compute the range on the freshly copied data, since it might have been processed
        // relative to the source data
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                computeRange<int8_t>(accessor, dstData, elementCount, componentCount);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                computeRange<uint8_t>(accessor, dstData, elementCount, componentCount);
                break;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                computeRange<int16_t>(accessor, dstData, elementCount, componentCount);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                computeRange<uint16_t>(accessor, dstData, elementCount, componentCount);
                break;
            case TINYGLTF_COMPONENT_TYPE_INT:
                computeRange<int32_t>(accessor, dstData, elementCount, componentCount);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                computeRange<uint32_t>(accessor, dstData, elementCount, componentCount);
                break;
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                computeRange<float>(accessor, dstData, elementCount, componentCount);
                break;
            case TINYGLTF_COMPONENT_TYPE_DOUBLE:
                computeRange<double>(accessor, dstData, elementCount, componentCount);
                break;
            default:
                TF_RUNTIME_ERROR("Unexpected component type %d for range computation",
                                 componentType);
        }
    }
    int accessorIndex = gltf->accessors.size();
    gltf->accessors.push_back(accessor);
    // std::cout << "Add accessor [" << accessorIndex << "]: " << name << ": " << elementCount <<
    // std::endl;
    return accessorIndex;
}

int
addImageBufferView(tinygltf::Model* gltf, const std::string& name, int dataSize, const void* data)
{
    tinygltf::Buffer& buffer = getBuffer(gltf);
    int currentSize = buffer.data.size();
    int extraBytes = currentSize % 4;
    int padding = extraBytes ? 4 - extraBytes : 0;
    buffer.data.resize(currentSize + padding + dataSize);
    memcpy(&buffer.data[currentSize + padding], data, dataSize);

    tinygltf::BufferView bufferView;
    bufferView.name = name;
    bufferView.buffer = 0;
    bufferView.byteOffset = currentSize + padding;
    bufferView.byteLength = dataSize;
    bufferView.byteStride = 0;
    bufferView.target = 0;
    int bufferViewIndex = gltf->bufferViews.size();
    gltf->bufferViews.push_back(bufferView);
    return bufferViewIndex;
}

int
getPrimitiveAttribute(const tinygltf::Primitive& primitive, const std::string& name)
{
    auto it = primitive.attributes.find(name);
    return it != primitive.attributes.end() ? it->second : -1;
}

size_t
getAccessorElementCount(const tinygltf::Model& model, int accessorIndex)
{
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        return 0;
    }
    return model.accessors[accessorIndex].count;
}

void
readAccessorData(const tinygltf::Model& model, int accessorIndex, uint8_t* dst)
{
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        return;
    }
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    size_t componentCount = tinygltf::GetNumComponentsInType(accessor.type);
    size_t elementSize = componentSize * componentCount;
    size_t elementStride = accessor.ByteStride(bufferView);

    const uint8_t* src = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    if (elementStride == elementSize) {
        memcpy(dst, src, accessor.count * elementSize);
    } else {
        for (size_t i = 0; i < accessor.count; i++) {
            memcpy(dst, src, elementSize);
            dst += elementSize;
            src += elementStride;
        }
    }
}

// Converts integer typed values into normalized float values
// If the integer type is signed, the output range is [-1.0f, 1.0f], otherwise its [0.0f, 1.0f]
template<typename T>
float
normalizedFloat(T value)
{
    return value < 0 ? -static_cast<float>(value) / std::numeric_limits<T>::lowest()
                     : static_cast<float>(value) / std::numeric_limits<T>::max();
}

// Specialization for floating point source values. In this case we assume the floating point values
// are already normalized and we pass them through.
// Note, if we want to enforce the expected range we could clamp the values.
template<>
float
normalizedFloat<float>(float value)
{
    return value;
}

// This function copies/converts a buffer of an accessor component type to a buffer of floats
void
readAccessorDataToFloat(const tinygltf::Model& model, int accessorIndex, float* dst)
{
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        return;
    }
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
    size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    size_t componentCount = tinygltf::GetNumComponentsInType(accessor.type);
    size_t elementSize = componentSize * componentCount;
    size_t elementStride = accessor.ByteStride(bufferView);
    bool normalized = accessor.normalized;

    const uint8_t* src = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    const size_t elementCount = accessor.count;
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        if (elementStride == elementSize) {
            memcpy(dst, src, elementCount * elementSize);
        } else {
            for (size_t i = 0; i < elementCount; i++) {
                memcpy(dst, src, elementSize);
                src += elementStride;
                dst += componentCount;
            }
        }
    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
        if (normalized) {
            for (size_t i = 0; i < elementCount; i++) {
                int8_t* p = (int8_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = normalizedFloat(p[j]);
                }
                src += elementStride;
                dst += componentCount;
            }
        } else {
            for (size_t i = 0; i < elementCount; i++) {
                int8_t* p = (int8_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = p[j];
                }
                src += elementStride;
                dst += componentCount;
            }
        }
    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        if (normalized) {
            for (size_t i = 0; i < elementCount; i++) {
                uint8_t* p = (uint8_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = normalizedFloat(p[j]);
                }
                src += elementStride;
                dst += componentCount;
            }
        } else {
            for (size_t i = 0; i < elementCount; i++) {
                uint8_t* p = (uint8_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = p[j];
                }
                src += elementStride;
                dst += componentCount;
            }
        }
    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
        if (normalized) {
            for (size_t i = 0; i < elementCount; i++) {
                int16_t* p = (int16_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = normalizedFloat(p[j]);
                }
                src += elementStride;
                dst += componentCount;
            }
        } else {
            for (size_t i = 0; i < elementCount; i++) {
                int16_t* p = (int16_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = p[j];
                }
                src += elementStride;
                dst += componentCount;
            }
        }
    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        if (normalized) {
            for (size_t i = 0; i < elementCount; i++) {
                uint16_t* p = (uint16_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = normalizedFloat(p[j]);
                }
                src += elementStride;
                dst += componentCount;
            }
        } else {
            for (size_t i = 0; i < elementCount; i++) {
                uint16_t* p = (uint16_t*)src;
                for (size_t j = 0; j < componentCount; j++) {
                    dst[j] = p[j];
                }
                src += elementStride;
                dst += componentCount;
            }
        }
    } else {
        TF_WARN("Unsigned Int and Double component types are not supported when converting to "
                "float arrays");
    }
}

template<typename T>
void
_readVec4Color(const tinygltf::Model& model,
               int colorsIndex,
               int colorCount,
               VtArray<GfVec3f>& color,
               VtArray<float>& opacity)
{
    std::vector<T> temp(colorCount * 4);
    readAccessorData(model, colorsIndex, reinterpret_cast<uint8_t*>(temp.data()));
    color.resize(colorCount);
    opacity.resize(colorCount);
    for (int i = 0; i < colorCount; i++) {
        color[i] = GfVec3f(normalizedFloat(temp[4 * i]),
                           normalizedFloat(temp[4 * i + 1]),
                           normalizedFloat(temp[4 * i + 2]));
        opacity[i] = normalizedFloat(temp[4 * i + 3]);
    }
}

template<typename T>
void
_readVec3Color(const tinygltf::Model& model,
               int colorsIndex,
               int colorCount,
               VtArray<GfVec3f>& color)
{
    std::vector<T> temp(colorCount * 3);
    readAccessorData(model, colorsIndex, reinterpret_cast<uint8_t*>(temp.data()));
    color.resize(colorCount);
    for (int i = 0; i < colorCount; i++) {
        color[i] = GfVec3f(normalizedFloat(temp[3 * i]),
                           normalizedFloat(temp[3 * i + 1]),
                           normalizedFloat(temp[3 * i + 2]));
    }
}

void
readColor(const tinygltf::Model& model,
          const tinygltf::Primitive& primitive,
          PXR_NS::VtArray<PXR_NS::GfVec3f>& color,
          PXR_NS::VtArray<float>& opacity)
{
    int colorsIndex = getPrimitiveAttribute(primitive, "COLOR_0");
    if (colorsIndex < 0 || static_cast<size_t>(colorsIndex) >= model.accessors.size()) {
        return;
    }
    int colorCount = getAccessorElementCount(model, colorsIndex);
    if (colorCount <= 0) {
        return;
    }
    const tinygltf::Accessor& accessor = model.accessors[colorsIndex];
    if (!accessor.normalized && accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        TF_WARN(
          "COLOR_0 data has integer components, but is not normalized. This is not supported");
    }
    if (accessor.type == TINYGLTF_TYPE_VEC4) {
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            _readVec4Color<float>(model, colorsIndex, colorCount, color, opacity);
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            _readVec4Color<uint16_t>(model, colorsIndex, colorCount, color, opacity);
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            _readVec4Color<uint8_t>(model, colorsIndex, colorCount, color, opacity);
        } else {
            TF_WARN("Unexpected component type %d for VEC4 COLOR_0 accessor. Signed color?",
                    accessor.componentType);
        }
    } else if (accessor.type == TINYGLTF_TYPE_VEC3) {
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            // No conversion necessary. We can just read the data
            color.resize(colorCount);
            readAccessorData(model, colorsIndex, reinterpret_cast<uint8_t*>(color.data()));
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            _readVec3Color<uint16_t>(model, colorsIndex, colorCount, color);
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            _readVec3Color<uint8_t>(model, colorsIndex, colorCount, color);
        } else {
            TF_WARN("Unexpected component type %d for VEC3 COLOR_0 accessor. Signed color?",
                    accessor.componentType);
        }
    } else {
        TF_WARN("Unhandled accessor type when reading color data");
    }
}

// Incurs a double copy but handles reading accessors holding integer data with unknown size
void
readAccessorInts(const tinygltf::Model& model, int accessorIndex, PXR_NS::VtArray<int>& dst)
{
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        return;
    }
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize == 1) {
        PXR_NS::VtArray<uint8_t> temp(dst.size());
        readAccessorData(model, accessorIndex, reinterpret_cast<uint8_t*>(temp.data()));
        dst.assign(temp.begin(), temp.end());
    } else if (componentSize == 2) {
        PXR_NS::VtArray<uint16_t> temp(dst.size());
        readAccessorData(model, accessorIndex, reinterpret_cast<uint8_t*>(temp.data()));
        dst.assign(temp.begin(), temp.end());
    } else { // must be == 4
        readAccessorData(model, accessorIndex, reinterpret_cast<uint8_t*>(dst.data()));
    }
}

bool
readAccessorMinMax(const tinygltf::Model& model,
                   int accessorIndex,
                   PXR_NS::GfVec3f& minValues,
                   PXR_NS::GfVec3f& maxValues)
{
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        return false;
    }
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    if (accessor.minValues.size() && accessor.maxValues.size()) {
        minValues =
          PXR_NS::GfVec3f(accessor.minValues[0], accessor.minValues[1], accessor.minValues[2]);
        maxValues =
          PXR_NS::GfVec3f(accessor.maxValues[0], accessor.maxValues[1], accessor.maxValues[2]);
        return true;
    }
    return false;
}

void
addToTimeMap(std::vector<float>& globalTime, const PXR_NS::VtArray<float>& time)
{
    float epsilon = .00001;
    size_t i = 0;
    for (size_t j = 0; j < globalTime.size() && i < time.size(); j++) {
        float delta = time[i] - globalTime[j];
        if (abs(delta) <= epsilon) {
            i++;
        } else if (delta < 0) {
            globalTime.insert(globalTime.begin() + j, time[i]);
            i++;
        }
    }
    globalTime.insert(globalTime.end(), time.begin() + i, time.end());
}

// Defined in tinygltf but brought here for debug use
std::string
printValue(const std::string& name, const tinygltf::Value& value, const int indent, const bool tag)
{
    std::string tagText = tag ? name + " : " : "";
    std::stringstream ss;
    if (value.IsObject()) {
        const tinygltf::Value::Object& o = value.Get<tinygltf::Value::Object>();
        for (auto it = o.begin(); it != o.end(); it++) {
            ss << printValue(it->first, it->second, indent + 1) << "\n";
        }
    } else if (value.IsString()) {
        ss << std::string(indent, ' ') << tagText << value.Get<std::string>();
    } else if (value.IsBool()) {
        ss << std::string(indent, ' ') << tagText << value.Get<bool>();
    } else if (value.IsNumber()) {
        ss << std::string(indent, ' ') << tagText << value.Get<double>();
    } else if (value.IsInt()) {
        ss << std::string(indent, ' ') << tagText << value.Get<int>();
    } else if (value.IsArray()) {
        ss << std::string(indent, ' ') << name << " [ ";
        for (size_t i = 0; i < value.ArrayLen(); i++) {
            ss << printValue("", value.Get(int(i)), 0, false);
            if (i != (value.ArrayLen() - 1)) {
                ss << ", ";
            }
        }
        ss << " ]";
    }
    return ss.str();
}

bool
unpackBase64String(const std::string& b64Str,
                   bool compressed,
                   std::vector<std::uint8_t>& decodedData)
{
    std::string b64NoPrefix = b64Str.substr(b64Str.find(',') + 1);
    std::string decodedStr = tinygltf::base64_decode(b64NoPrefix);

    if (compressed) {
        if (!decompress(reinterpret_cast<const uint8_t*>(decodedStr.data()),
                        decodedStr.length(),
                        decodedData)) {
            decodedData.clear();
            return false;
        }
    } else {
        decodedData.resize(decodedStr.length());
        decodedStr.copy(reinterpret_cast<char*>(decodedData.data()), decodedStr.length());
    }
    return true;
}

bool
packBase64String(const std::uint8_t* inputData,
                 std::size_t inLen,
                 bool compressed,
                 std::string& b64Str)
{
    const uint8_t* rawData = inputData;
    std::vector<uint8_t> compressedData;
    if (compressed) {
        if (!compress(inputData, inLen, compressedData)) {
            b64Str.clear();
            return false;
        }
        rawData = compressedData.data();
        inLen = compressedData.size();
    }

    b64Str = base64Prefix + tinygltf::base64_encode(rawData, inLen);
    return true;
}
}
