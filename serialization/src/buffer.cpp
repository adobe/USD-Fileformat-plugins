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

#include <serialization/buffer.h>

#include <cstring>

namespace adobe::usd::serialization {

// -- BufferWriter --

void
BufferWriter::AppendRaw(const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    _buffer.insert(_buffer.end(), bytes, bytes + size);
}

void
BufferWriter::WriteUint32(uint32_t value)
{
    AppendRaw(&value, sizeof(value));
}

void
BufferWriter::WriteUint64(uint64_t value)
{
    AppendRaw(&value, sizeof(value));
}

void
BufferWriter::WriteSizeT(size_t value)
{
    // Always serialize as uint64_t for cross-platform consistency
    uint64_t v = static_cast<uint64_t>(value);
    AppendRaw(&v, sizeof(v));
}

void
BufferWriter::WriteString(const std::string& str)
{
    WriteSizeT(str.size());
    if (!str.empty()) {
        AppendRaw(str.data(), str.size());
    }
}

void
BufferWriter::WriteMap(const std::map<std::string, std::string>& map)
{
    WriteSizeT(map.size());
    for (const auto& [key, value] : map) {
        WriteString(key);
        WriteString(value);
    }
}

void
BufferWriter::WriteBytes(const void* data, size_t size)
{
    if (data != nullptr && size > 0) {
        AppendRaw(data, size);
    }
}

std::vector<uint8_t>
BufferWriter::Finish()
{
    return std::move(_buffer);
}

// -- BufferReader --

BufferReader::BufferReader(const std::vector<uint8_t>& data)
  : _data(data.data())
  , _size(data.size())
{}

BufferReader::BufferReader(const uint8_t* data, size_t size)
  : _data(data)
  , _size(size)
{}

bool
BufferReader::ReadRaw(void* dest, size_t size)
{
    // Overflow-safe: _offset <= _size is an invariant, so compare against the
    // remaining space rather than computing _offset + size (which can wrap when
    // `size` is a length prefix near SIZE_MAX).
    if (_error || size > _size - _offset) {
        _error = true;
        return false;
    }
    memcpy(dest, _data + _offset, size);
    _offset += size;
    return true;
}

uint32_t
BufferReader::ReadUint32()
{
    uint32_t value = 0;
    ReadRaw(&value, sizeof(value));
    return value;
}

uint64_t
BufferReader::ReadUint64()
{
    uint64_t value = 0;
    ReadRaw(&value, sizeof(value));
    return value;
}

size_t
BufferReader::ReadSizeT()
{
    uint64_t value = 0;
    ReadRaw(&value, sizeof(value));
    return static_cast<size_t>(value);
}

std::string
BufferReader::ReadString()
{
    size_t len = ReadSizeT();
    if (_error || len == 0) {
        return {};
    }
    if (len > _size - _offset) { // overflow-safe; see ReadRaw
        _error = true;
        return {};
    }
    std::string result(reinterpret_cast<const char*>(_data + _offset), len);
    _offset += len;
    return result;
}

std::map<std::string, std::string>
BufferReader::ReadMap()
{
    std::map<std::string, std::string> result;
    size_t count = ReadSizeT();
    for (size_t i = 0; i < count && !_error; ++i) {
        std::string key = ReadString();
        std::string value = ReadString();
        if (!_error) {
            result[std::move(key)] = std::move(value);
        }
    }
    return result;
}

bool
BufferReader::ReadBytes(void* buffer, size_t size)
{
    return ReadRaw(buffer, size);
}

} // namespace adobe::usd::serialization
