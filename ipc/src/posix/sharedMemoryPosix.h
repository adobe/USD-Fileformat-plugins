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

namespace adobe::usd::ipc {

/**
 * POSIX implementation of SharedMemory backed by shm_open() + mmap() (the library links librt on
 * Linux for shm_open/shm_unlink). The creating process owns the region and unlinks it on Clean();
 * a connecting process maps it without owning it. Internal — construct via
 * ipc::CreateSharedMemory().
 */
class SharedMemoryPosix : public SharedMemory
{
public:
    SharedMemoryPosix();
    ~SharedMemoryPosix() override;

    bool Create(const std::string& name, size_t size) override;
    bool Connect(const std::string& name, size_t size) override;
    void Clean() override;

private:
    int _fd;
    bool _isOwner = false; // true only if this process called Create(); controls whether
                           // Clean/destructor unlinks the region
};

} // namespace adobe::usd::ipc
