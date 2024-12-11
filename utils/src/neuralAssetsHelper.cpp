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
#include <fileformatutils/neuralAssetsHelper.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <zlib.h>

namespace adobe::usd {

namespace {
union Fp32
{
    std::uint32_t u = 0;
    float f;
};

constexpr Fp32 f32Infty = { 255 << 23 };
constexpr Fp32 f16Infty = { 31 << 23 };
constexpr Fp32 magic = { 15 << 23 };

constexpr Fp32 magic2 = { (254 - 15) << 23 };
constexpr Fp32 wasInfNan = { (127 + 16) << 23 };

inline std::uint16_t
float32ToFloat16(const float fl)
{
    constexpr unsigned int signMask = 0x80000000u;
    constexpr unsigned int roundMask = ~0xfffu;

    std::uint16_t o = 0;

    Fp32 f;
    f.f = fl;

    const unsigned int sign = f.u & signMask;
    f.u ^= sign;

    // NOTE all the integer compares in this function can be safely
    // compiled into signed compares since all operands are below
    // 0x80000000. Important if you want fast straight SSE2 code
    // (since there's no unsigned PCMPGTD).

    if (f.u >= f32Infty.u)                        // Inf or NaN (all exponent bits set)
        o = (f.u > f32Infty.u) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
    else                                          // (de)normalized number or zero
    {
        f.u &= roundMask;
        f.f *= magic.f;
        f.u -= roundMask;
        if (f.u > f16Infty.u) // clamp to signed infinity if overflowed
            f.u = f16Infty.u;

        o = static_cast<std::uint16_t>(f.u >> 13); // take the bits!
    }

    o |= sign >> 16;
    return o;
}

inline float
float16ToFloat32(const std::uint16_t h)
{
    Fp32 o;
    o.u = (h & 0x7fff) << 13; // exponent/mantissa bits
    o.f *= magic2.f;          // exponent adjust
    if (o.f >= wasInfNan.f)   // make sure Inf/NaN survive
        o.u |= 255 << 23;
    o.u |= (h & 0x8000) << 16; // sign bit

    return o.f;
}
}

bool
decompress(const std::uint8_t* inputData,
           std::size_t inLen,
           std::vector<std::uint8_t>& decompressedData)
{
    if (!inLen) {
        return false;
    }
    decompressedData.clear();

    z_stream strm = {};
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(inputData));
    strm.avail_in = static_cast<uInt>(inLen);

    // Initialize the zlib decompression stream.
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
        return false;
    }

    int ret;
    const std::size_t bufferSize = 4096; // Temporary buffer size
    std::vector<std::uint8_t> buffer(bufferSize);

    // Decompress the data.
    do {
        strm.avail_out = bufferSize;
        strm.next_out = buffer.data();

        ret = inflate(&strm, Z_NO_FLUSH);

        switch (ret) {
            case Z_NEED_DICT:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
            case Z_STREAM_ERROR:
                inflateEnd(&strm);
                return false;
        }

        std::size_t have = bufferSize - strm.avail_out;
        decompressedData.insert(decompressedData.end(), buffer.begin(), buffer.begin() + have);
    } while (ret != Z_STREAM_END);

    // Clean up and return.
    inflateEnd(&strm);
    return true;
}

bool
compress(const std::uint8_t* inputData, std::size_t inLen, std::vector<std::uint8_t>& outputData)
{
    if (!inLen) {
        return false;
    }
    outputData.clear();

    z_stream strm = {};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(inputData));
    strm.avail_in = static_cast<uInt>(inLen);

    // Initialize zlib compression stream.
    if (deflateInit2(
          &strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        return false;
    }

    const std::size_t bufferSize = 4096;
    std::vector<std::uint8_t> buffer(bufferSize);

    int ret;
    do {
        strm.avail_out = bufferSize;
        strm.next_out = buffer.data();

        ret = deflate(&strm, Z_FINISH);

        switch (ret) {
            case Z_NEED_DICT:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
            case Z_STREAM_ERROR:
                deflateEnd(&strm);
                return false;
        }

        std::size_t have = bufferSize - strm.avail_out;
        outputData.insert(outputData.end(), buffer.begin(), buffer.begin() + have);
    } while (strm.avail_out == 0);

    // Clean up and return.
    deflateEnd(&strm);

    return true;
}

void
float16ToFloat32(const std::uint16_t* inputData, float* outputData, std::size_t numElements)
{
    for (std::size_t i = 0; i < numElements; ++i)
        outputData[i] = float16ToFloat32(inputData[i]);
}

void
float32ToFloat16(const float* inputData, std::uint16_t* outputData, std::size_t numElements)
{
    for (std::size_t i = 0; i < numElements; ++i)
        outputData[i] = float32ToFloat16(inputData[i]);
}

template<typename T>
T
maxOfFloatArray(const T* inputData, std::size_t numElements)
{
    T fMax = -std::numeric_limits<T>::max();
    for (std::size_t i = 0; i < numElements; ++i)
        fMax = std::max(fMax, inputData[i]);
    return fMax;
}

template<typename T>
T
infNormOfFloatArray(const T* inputData, std::size_t numElements)
{
    T fMax = static_cast<T>(0.0);
    for (std::size_t i = 0; i < numElements; ++i)
        fMax = std::max(fMax, std::abs(inputData[i]));
    return fMax;
}

// Unpack the 4x4 matrix on NGP's weights
void
unpackMLPWeight(const float* in, float* out, const std::size_t d1, const std::size_t d2)
{
    std::size_t numColMat = d1 / 4;
    std::size_t numRowMat = d2 / 4;
    for (std::size_t i = 0; i < numColMat; i++) {
        for (std::size_t j = 0; j < numRowMat; j++) {
            for (std::size_t k = 0; k < 4; k++) {
                for (std::size_t l = 0; l < 4; l++) {
                    const std::size_t in_idx = (((i * numRowMat + j) * 4) + k) * 4 + l;
                    const std::size_t out_idx = ((i * 4 + k) * numRowMat + j) * 4 + l;

                    out[out_idx] = in[in_idx];
                }
            }
        }
    }
}

// Pack the 4x4 matrix on NGP's weights
void
packMLPWeight(const float* in, float* out, const std::size_t d1, const std::size_t d2)
{
    std::size_t numColMat = d1 / 4;
    std::size_t numRowMat = d2 / 4;
    for (std::size_t i = 0; i < numColMat; i++) {
        for (std::size_t k = 0; k < 4; k++) {
            for (std::size_t j = 0; j < numRowMat; j++) {
                for (std::size_t l = 0; l < 4; l++) {
                    const std::size_t in_idx = ((i * 4 + k) * numRowMat + j) * 4 + l;
                    const std::size_t out_idx = (((i * numRowMat + j) * 4) + k) * 4 + l;

                    out[out_idx] = in[in_idx];
                }
            }
        }
    }
}

const char*
getNerfExtString()
{
    return "ADOBE_nerf_asset";
}

template USDFFUTILS_API float
maxOfFloatArray<float>(const float* inputData, std::size_t numElements);
template USDFFUTILS_API double
maxOfFloatArray<double>(const double* inputData, std::size_t numElements);
template USDFFUTILS_API float
infNormOfFloatArray<float>(const float* inputData, std::size_t numElements);
template USDFFUTILS_API double
infNormOfFloatArray<double>(const double* inputData, std::size_t numElements);
}
