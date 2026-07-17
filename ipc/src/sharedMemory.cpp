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

#include <ipc/sharedMemory.h>

#include <pxr/base/tf/diagnostic.h>

#include <cstring>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::ipc {

bool
SharedMemory::Read(size_t offset, void* buffer, size_t size)
{
    if (buffer == nullptr) {
        TF_WARN("IPC: Destination buffer is null in SharedMemory::Read");
        return false;
    }
    if (_buf == nullptr) {
        TF_WARN("IPC: Shared memory not mapped in SharedMemory::Read");
        return false;
    }
    // Overflow-safe: offset + size can wrap size_t for hostile/buggy values.
    if (offset > _size || size > _size - offset) {
        TF_WARN("IPC: Read of %zu bytes at offset %zu exceeds shared memory size %zu",
                size,
                offset,
                _size);
        return false;
    }
    memcpy(buffer, static_cast<char*>(_buf) + offset, size);
    return true;
}

bool
SharedMemory::Read(size_t offset, std::ostream& stream, size_t size)
{
    if (_buf == nullptr) {
        TF_WARN("IPC: Shared memory not mapped in SharedMemory::Read (stream)");
        return false;
    }
    if (offset > _size || size > _size - offset) {
        TF_WARN("IPC: Read of %zu bytes at offset %zu exceeds shared memory size %zu",
                size,
                offset,
                _size);
        return false;
    }
    stream.write(static_cast<char*>(_buf) + offset, size);
    return true;
}

bool
SharedMemory::Write(const void* buffer, size_t size, size_t offset)
{
    if (buffer == nullptr) {
        TF_WARN("IPC: Source buffer is null in SharedMemory::Write");
        return false;
    }
    if (_buf == nullptr) {
        TF_WARN("IPC: Shared memory not mapped in SharedMemory::Write");
        return false;
    }
    if (offset > _size || size > _size - offset) {
        TF_WARN("IPC: Write of %zu bytes at offset %zu exceeds shared memory size %zu",
                size,
                offset,
                _size);
        return false;
    }
    memcpy(static_cast<char*>(_buf) + offset, buffer, size);
    return true;
}

} // namespace adobe::usd::ipc
