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
#include "api.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace adobe::usd {
USDFFUTILS_API void
float16ToFloat32(const std::uint16_t* inputData, float* outputData, std::size_t numElements);

USDFFUTILS_API void
float32ToFloat16(const float* inputData, std::uint16_t* outputData, std::size_t numElements);

template<typename T>
T maxOfFloatArray(const T* inputData, std::size_t numElements);

template<typename T>
T infNormOfFloatArray(const T* inputData, std::size_t numElements);

USDFFUTILS_API void
unpackMLPWeight(const float* in, float* out, const std::size_t d1, const std::size_t d2);

USDFFUTILS_API void
packMLPWeight(const float* in, float* out, const std::size_t d1, const std::size_t d2);

USDFFUTILS_API bool
decompress(const std::uint8_t* inputData, std::size_t inLen, std::vector<std::uint8_t>& decompressedData);

USDFFUTILS_API bool
compress(const std::uint8_t* inputData, std::size_t inLen, std::vector<std::uint8_t>& outputData);

USDFFUTILS_API const char*
getNerfExtString();
}
