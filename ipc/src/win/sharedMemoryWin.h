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

#include <ipc/sharedMemory.h>

#include <windows.h>

namespace adobe::usd::ipc {

/**
 * Windows implementation of SharedMemory backed by CreateFileMapping() + MapViewOfFile(). The
 * mapping is created with a low-integrity security descriptor (see utilitiesWin.h) so a
 * low-integrity peer process can open it. Internal — construct via ipc::CreateSharedMemory().
 */
class SharedMemoryWin : public SharedMemory
{
public:
    SharedMemoryWin();
    ~SharedMemoryWin() override;

    bool Create(const std::string& name, size_t size) override;
    bool Connect(const std::string& name, size_t size) override;
    void Clean() override;

private:
    HANDLE _mapFile;
};

} // namespace adobe::usd::ipc
