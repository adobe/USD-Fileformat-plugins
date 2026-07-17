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

#include "sharedMemoryWin.h"
#include "utilitiesWin.h"

#include <pxr/base/tf/diagnostic.h>

#include <cstring>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::ipc {

SharedMemoryWin::SharedMemoryWin()
  : _mapFile(nullptr)
{}

SharedMemoryWin::~SharedMemoryWin()
{
    if (_buf != nullptr) {
        UnmapViewOfFile(_buf);
        _buf = nullptr;
    }
    if (_mapFile != nullptr) {
        CloseHandle(_mapFile);
        _mapFile = nullptr;
    }
}

bool
SharedMemoryWin::Create(const std::string& name, size_t size)
{
    if (name.empty()) {
        TF_WARN("IPC: Cannot create shared memory with empty name");
        return false;
    }

    _name = name;
    _size = size;

    if (_mapFile != nullptr) {
        TF_WARN("IPC: Shared memory already created");
        return false;
    }

    std::string userSid;
    if (!GetCurrentProcessUserSid(userSid)) {
        TF_WARN("IPC: Failed to get current process user SID");
        return false;
    }

    SECURITY_ATTRIBUTES sa;
    if (!CreateSecurityAttributes(BuildLowLevelSDDL(userSid), sa)) {
        TF_WARN("IPC: Failed to create security attributes");
        return false;
    }

    // CreateFileMapping takes the maximum size as a high/low DWORD pair that together form a
    // 64-bit value. Split the full _size across both halves so the mapping spans the entire
    // region: the host-side bounds checks (SharedMemory::Read/Write) validate against the full
    // 64-bit _size, so the mapping must be at least that large or a >4 GiB asset would read or
    // write out of bounds. Casting through uint64_t keeps the >> 32 shift well-defined. 64-bit
    // process only (see doc/build/windows.md).
    const uint64_t size64 = static_cast<uint64_t>(_size);
    _mapFile = CreateFileMapping(INVALID_HANDLE_VALUE,
                                 &sa,
                                 PAGE_READWRITE,
                                 static_cast<DWORD>(size64 >> 32),
                                 static_cast<DWORD>(size64 & 0xFFFFFFFFu),
                                 _name.c_str());

    if (_mapFile == nullptr) {
        TF_WARN("IPC: CreateFileMapping failed (error %lu)", GetLastError());
        return false;
    }

    _buf = MapViewOfFile(_mapFile, FILE_MAP_WRITE, 0, 0, 0);
    if (_buf == nullptr) {
        TF_WARN("IPC: MapViewOfFile failed (error %lu)", GetLastError());
        CloseHandle(_mapFile);
        _mapFile = nullptr;
        return false;
    }

    return true;
}

bool
SharedMemoryWin::Connect(const std::string& name, size_t size)
{
    if (name.empty()) {
        TF_WARN("IPC: Cannot connect to shared memory with empty name");
        return false;
    }

    _name = name;
    _size = size;

    _mapFile = OpenFileMapping(FILE_MAP_WRITE, FALSE, _name.c_str());
    if (_mapFile == nullptr) {
        TF_WARN("IPC: OpenFileMapping failed (error %lu)", GetLastError());
        return false;
    }

    _buf = MapViewOfFile(_mapFile, FILE_MAP_WRITE, 0, 0, 0);
    if (_buf == nullptr) {
        TF_WARN("IPC: MapViewOfFile failed on connect (error %lu)", GetLastError());
        CloseHandle(_mapFile);
        _mapFile = nullptr;
        return false;
    }

    return true;
}

void
SharedMemoryWin::Clean()
{
    if (_buf != nullptr) {
        UnmapViewOfFile(_buf);
        _buf = nullptr;
    }
    if (_mapFile != nullptr) {
        CloseHandle(_mapFile);
        _mapFile = nullptr;
    }
    _size = 0;
    _name.clear();
}

} // namespace adobe::usd::ipc
