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

#include "sharedMemoryPosix.h"

#include <pxr/base/tf/diagnostic.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::ipc {

static constexpr int kInvalidFd = -1;

SharedMemoryPosix::SharedMemoryPosix()
  : _fd(kInvalidFd)
  , _isOwner(false)
{}

SharedMemoryPosix::~SharedMemoryPosix()
{
    if (_buf != nullptr) {
        munmap(_buf, _size);
        _buf = nullptr;
    }
    if (_fd != kInvalidFd) {
        close(_fd);
        _fd = kInvalidFd;
    }
    if (_isOwner && !_name.empty()) {
        shm_unlink(_name.c_str());
    }
}

bool
SharedMemoryPosix::Create(const std::string& name, size_t size)
{
    if (name == "") {
        TF_WARN("IPC: Cannot create shared memory with empty name.");
        return false;
    }

    _name = name;
    _size = size;
    _isOwner = true;

    shm_unlink(_name.c_str());

    _fd = shm_open(_name.c_str(), O_CREAT | O_RDWR, 0600);
    if (_fd == kInvalidFd) {
        TF_WARN("IPC: Failed to create shared memory '%s': %s", name.c_str(), strerror(errno));
        return false;
    }

    if (ftruncate(_fd, _size) == -1) {
        TF_WARN("IPC: Failed to set shared memory size to %zu: %s", _size, strerror(errno));
        return false;
    }

    // Clear FD_CLOEXEC so the fd is inheritable
    fcntl(_fd, F_SETFD, 0);

    _buf = mmap(NULL, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (_buf == MAP_FAILED) {
        TF_WARN("IPC: Failed to map shared memory '%s': %s", name.c_str(), strerror(errno));
        _buf = nullptr;
        return false;
    }

    return true;
}

bool
SharedMemoryPosix::Connect(const std::string& name, size_t size)
{
    if (name == "") {
        TF_WARN("IPC: Cannot connect to shared memory with empty name.");
        return false;
    }

    _name = name;
    _size = size;
    // _isOwner defaults to false

    _fd = shm_open(_name.c_str(), O_RDWR, 0600);
    if (_fd == kInvalidFd) {
        TF_WARN("IPC: Failed to open shared memory '%s': %s", name.c_str(), strerror(errno));
        return false;
    }

    _buf = mmap(NULL, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (_buf == MAP_FAILED) {
        TF_WARN("IPC: Failed to map shared memory '%s': %s", name.c_str(), strerror(errno));
        _buf = nullptr;
        return false;
    }

    return true;
}

void
SharedMemoryPosix::Clean()
{
    if (_buf != nullptr) {
        munmap(_buf, _size);
        _buf = nullptr;
    }
    if (_fd != kInvalidFd) {
        close(_fd);
        _fd = kInvalidFd;
    }
    if (_isOwner && !_name.empty()) {
        shm_unlink(_name.c_str());
    }
    _name.clear();
    _isOwner = false;
    _size = 0;
}

} // namespace adobe::usd::ipc
