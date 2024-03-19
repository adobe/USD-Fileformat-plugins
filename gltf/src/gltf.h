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
#include <cstdint>
#include <tiny_gltf.h>

namespace adobe::usd {

struct WriteGltfOptions
{
    bool embedImages = true;
};

bool
readGltf(tinygltf::Model& gltf, const std::string& filename);
bool
readGltf(tinygltf::Model& gltf, std::string& str);
bool
writeGltf(const WriteGltfOptions& options, tinygltf::Model& gltf, const std::string& filename);

void
printMatrix(const std::string& name, const PXR_NS::GfMatrix4d& matrix);
void
copyMatrices(const PXR_NS::VtMatrix4dArray& matrices, std::vector<float>& values);
void
copyMatrix(const PXR_NS::GfMatrix4d& matrix, std::vector<double>& values);
void
copyMatrix(const std::vector<double>& values, PXR_NS::GfMatrix4d& matrix);
void
decomposeMatrix(const PXR_NS::GfMatrix4d& matrix, tinygltf::Node& node);
void
computeMinMax(const PXR_NS::VtArray<PXR_NS::GfVec3f>& values,
              PXR_NS::GfVec3f& minValues,
              PXR_NS::GfVec3f& maxValues);

int
addAccessor(tinygltf::Model* gltf,
            const std::string& name,
            int target,
            int type,
            int componentType,
            int elementCount,
            const void* data,
            bool withRange);

int
addImageBufferView(tinygltf::Model* gltf, const std::string& name, int dataSize, const void* data);

int
getPrimitiveAttribute(const tinygltf::Primitive& primitive, const std::string& name);
size_t
getAccessorElementCount(const tinygltf::Model& model, int accessorIndex);
void
readAccessorData(const tinygltf::Model& model, int accessorIndex, uint8_t* dst);
void
readAccessorDataToFloat(const tinygltf::Model& model, int accessorIndex, float* dst);
bool
readAccessorMinMax(const tinygltf::Model& model,
                   int accessorIndex,
                   PXR_NS::GfVec3f& min,
                   PXR_NS::GfVec3f& max);
void
readColor(const tinygltf::Model& model,
          const tinygltf::Primitive& primitive,
          PXR_NS::VtArray<PXR_NS::GfVec3f>& color,
          PXR_NS::VtArray<float>& opacity);
void
readAccessorInts(const tinygltf::Model& model, int accessorIndex, PXR_NS::VtArray<int>& dst);

void
addToTimeMap(std::vector<float>& globalTime, const PXR_NS::VtArray<float>& time);
template<typename T>
void
interpolateData(const std::vector<float>& globalTimes,
                const PXR_NS::VtArray<float>& times,
                const PXR_NS::VtArray<T>& data,
                PXR_NS::VtArray<T>& interpolatedData)
{
    size_t w0 = 0;
    size_t w1 = 1;
    for (size_t i = 0; i < globalTimes.size(); i++) {
        float t = globalTimes[i];
        float t0 = times[w0];
        float t1 = times[w1];
        while ((t > t1) && (w1 + 1 < times.size())) {
            t0 = times[++w0];
            t1 = times[++w1];
        }
        t = std::max(t, t0);
        t = std::min(t, t1);
        auto v0 = data[w0];
        auto v1 = data[w1];
        auto v2 = (v1 - v0) * (t - t0) / (t1 - t0) + v0;
        interpolatedData[i] = v2;
    }
}

std::string
printValue(const std::string& name,
           const tinygltf::Value& value,
           const int indent,
           const bool tag = true);

bool
unpackBase64String(const std::string& b64Str,
                   bool compressed,
                   std::vector<std::uint8_t>& decodedData);

bool
packBase64String(const std::uint8_t* inputData,
                 std::size_t inLen,
                 bool compressed,
                 std::string& b64Str);
}