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

#include <ipc/pipe.h>

#if IPC_IS_WINDOWS
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::ipc {

bool
CreatePipePair(PipePair& pair)
{
#if IPC_IS_WINDOWS
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE readEnd = INVALID_HANDLE_VALUE;
    HANDLE writeEnd = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        TF_WARN("IPC: Failed to create pipe pair (Windows error %lu)", GetLastError());
        return false;
    }

    pair.readEnd = PipeHandle(readEnd);
    pair.writeEnd = PipeHandle(writeEnd);
    return true;
#else
    int fds[2];
    if (pipe(fds) == -1) {
        TF_WARN("IPC: Failed to create pipe pair: %s", strerror(errno));
        return false;
    }
    // Clear FD_CLOEXEC so file descriptors survive exec()
    fcntl(fds[0], F_SETFD, 0);
    fcntl(fds[1], F_SETFD, 0);

    pair.readEnd = PipeHandle(fds[0]);
    pair.writeEnd = PipeHandle(fds[1]);
    return true;
#endif
}

bool
PipeHandle::Write(const void* data, size_t size) const
{
    if (size > 0 && data == nullptr) {
        TF_WARN("IPC: PipeHandle::Write called with null data and non-zero size %zu", size);
        return false;
    }
#if IPC_IS_WINDOWS
    DWORD bytesWritten;
    return WriteFile(_handle, data, (DWORD)size, &bytesWritten, NULL) && bytesWritten == size;
#else
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t n = ::write(_fd, ptr, remaining);
        if (n == -1) {
            if (errno == EINTR)
                continue; // signal interrupted, retry
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
#endif
}

bool
PipeHandle::Read(void* buffer, size_t size) const
{
    if (size > 0 && buffer == nullptr) {
        TF_WARN("IPC: PipeHandle::Read called with null buffer and non-zero size %zu", size);
        return false;
    }
#if IPC_IS_WINDOWS
    DWORD bytesRead;
    return ReadFile(_handle, buffer, (DWORD)size, &bytesRead, NULL) && bytesRead == size;
#else
    char* ptr = static_cast<char*>(buffer);
    size_t remaining = size;
    while (remaining > 0) {
        ssize_t n = ::read(_fd, ptr, remaining);
        if (n == 0)
            return false; // EOF: write end was closed
        if (n == -1) {
            if (errno == EINTR)
                continue; // signal interrupted, retry
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
#endif
}

} // namespace adobe::usd::ipc
