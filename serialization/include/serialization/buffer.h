/*
Copyright 2025 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#pragma once

#include <serialization/api.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace adobe::usd::serialization {

/**
 * Binary buffer writer for serializing primitive types into a byte buffer.
 * Data is written in native byte order.
 */
class SERIALIZATION_API BufferWriter
{
public:
    /// Append a 32-bit unsigned integer in native byte order.
    void WriteUint32(uint32_t value);
    /// Append a 64-bit unsigned integer in native byte order.
    void WriteUint64(uint64_t value);
    /// Append a size_t value encoded as uint64_t for cross-platform consistency.
    void WriteSizeT(size_t value);
    /// Append a string: length prefix (via WriteSizeT) followed by the UTF-8 bytes.
    void WriteString(const std::string& str);
    /// Append a string-to-string map: entry count followed by key/value string pairs.
    void WriteMap(const std::map<std::string, std::string>& map);
    /// Append raw bytes. No-op if data is null or size is zero.
    void WriteBytes(const void* data, size_t size);

    /// Return the serialized byte buffer. After calling this, the writer is reset.
    std::vector<uint8_t> Finish();

    /// Return the current size of the buffer without finishing.
    size_t Size() const { return _buffer.size(); }

private:
    std::vector<uint8_t> _buffer;

    // Append raw bytes to _buffer without any length prefix.
    void AppendRaw(const void* data, size_t size);
};

/**
 * Binary buffer reader for deserializing primitive types from a byte buffer.
 * Reads must match the order and types used during writing. If any read exceeds the buffer,
 * the reader enters an error state (HasError() returns true) and subsequent reads return
 * zero/empty values.
 */
class SERIALIZATION_API BufferReader
{
public:
    explicit BufferReader(const std::vector<uint8_t>& data);
    BufferReader(const uint8_t* data, size_t size);

    /// Read a 32-bit unsigned integer. Sets the error state if insufficient bytes remain.
    uint32_t ReadUint32();
    /// Read a 64-bit unsigned integer. Sets the error state if insufficient bytes remain.
    uint64_t ReadUint64();
    /// Read a size_t value encoded as uint64_t. Sets the error state on underflow.
    size_t ReadSizeT();
    /// Read a length-prefixed string. Returns empty and sets the error state on underflow.
    std::string ReadString();
    /// Read a string-to-string map written by WriteMap. Sets the error state on underflow.
    std::map<std::string, std::string> ReadMap();
    /// Copy @p size bytes into @p buffer. Returns false and sets error state on underflow.
    bool ReadBytes(void* buffer, size_t size);

    bool HasError() const { return _error; }
    size_t BytesRead() const { return _offset; }
    size_t BytesRemaining() const { return _error ? 0 : _size - _offset; }

private:
    const uint8_t* _data;
    size_t _size;
    size_t _offset = 0;
    bool _error = false;

    // Consume size bytes from _data into dest. Sets _error and returns false on underflow.
    bool ReadRaw(void* dest, size_t size);
};

} // namespace adobe::usd::serialization
