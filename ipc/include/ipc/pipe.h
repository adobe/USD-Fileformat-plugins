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

#include <ipc/api.h>
#include <ipc/platformConfig.h>

#include <string>

#if IPC_IS_WINDOWS
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace adobe::usd::ipc {

/**
 * Platform-independent wrapper for a single end of a pipe (POSIX file descriptor or Windows
 * HANDLE). Provides read, write, and close operations.
 */
class IPC_API PipeHandle
{
public:
    /**
     * Wrap an existing OS pipe end — a Windows HANDLE or a POSIX file descriptor. Defaults to an
     * invalid/closed handle.
     */
#if IPC_IS_WINDOWS
    explicit PipeHandle(HANDLE h = INVALID_HANDLE_VALUE)
      : _handle(h)
    {}
#else
    explicit PipeHandle(int fd = -1)
      : _fd(fd)
    {}
#endif

    // Move-only: a pipe handle owns an OS resource
    PipeHandle(const PipeHandle&) = delete;
    PipeHandle& operator=(const PipeHandle&) = delete;

    PipeHandle(PipeHandle&& other) noexcept
#if IPC_IS_WINDOWS
      : _handle(other._handle)
    {
        other._handle = INVALID_HANDLE_VALUE;
    }
#else
      : _fd(other._fd)
    {
        other._fd = -1;
    }
#endif

    PipeHandle& operator=(PipeHandle&& other) noexcept
    {
        if (this != &other) {
            Close();
#if IPC_IS_WINDOWS
            _handle = other._handle;
            other._handle = INVALID_HANDLE_VALUE;
#else
            _fd = other._fd;
            other._fd = -1;
#endif
        }
        return *this;
    }

    /**
     * Reconstruct a PipeHandle from the decimal string produced by ToString(). Used to hand an
     * inherited pipe end to a child process: the parent serializes the handle/fd into an argv
     * entry and the child rebuilds the PipeHandle from it.
     *
     * @param str The raw OS value as text — a Windows HANDLE or a POSIX fd number. This does not
     *            dup or validate the handle, so the descriptor must actually be inherited by the
     *            child for it to be usable.
     * @return A PipeHandle wrapping that handle/fd.
     */
    static PipeHandle FromString(const std::string& str)
    {
#if IPC_IS_WINDOWS
        unsigned long long handleValue = std::stoull(str);
        return PipeHandle((HANDLE)(uintptr_t)handleValue);
#else
        return PipeHandle(std::stoi(str));
#endif
    }

    /**
     * Serialize the raw handle/fd to a decimal string, for passing this pipe end to a child
     * process on its command line (see FromString).
     *
     * @return The handle/fd as a decimal string.
     */
    std::string ToString() const
    {
#if IPC_IS_WINDOWS
        return std::to_string((uintptr_t)_handle);
#else
        return std::to_string(_fd);
#endif
    }

    /**
     * Write exactly `size` bytes from `data`.
     *
     * @param data Source buffer; must hold at least `size` bytes. Passing null with a non-zero
     *             `size` is a programming error: it is rejected (and warned about), not written.
     * @param size Number of bytes to write.
     * @return true on success; false on I/O error or a null-with-non-zero-size misuse.
     */
    bool Write(const void* data, size_t size) const;

    /**
     * Read exactly `size` bytes into `buffer`.
     *
     * @param buffer Destination buffer; must hold at least `size` bytes. Passing null with a
     *               non-zero `size` is a programming error: it is rejected (and warned about).
     * @param size Number of bytes to read.
     * @return true if all `size` bytes were read; false on I/O error, EOF, or a null misuse.
     */
    bool Read(void* buffer, size_t size) const;

    /**
     * Close the underlying handle/fd if open, and reset it to the invalid value. Safe to call
     * more than once.
     */
    void Close()
    {
#if IPC_IS_WINDOWS
        if (_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(_handle);
            _handle = INVALID_HANDLE_VALUE;
        }
#else
        if (_fd != -1) {
            ::close(_fd);
            _fd = -1;
        }
#endif
    }

    /**
     * @return true if this wraps an open handle/fd (i.e. not the default invalid value).
     */
    bool IsValid() const
    {
#if IPC_IS_WINDOWS
        return _handle != INVALID_HANDLE_VALUE;
#else
        return _fd != -1;
#endif
    }

private:
#if IPC_IS_WINDOWS
    HANDLE _handle = INVALID_HANDLE_VALUE;
#else
    int _fd = -1;
#endif
};

/// A pair of pipe handles representing the read and write ends of a unidirectional pipe.
struct IPC_API PipePair
{
    PipeHandle readEnd;
    PipeHandle writeEnd;
};

/**
 * Create a unidirectional pipe pair. The pipe handles are created as inheritable so they survive
 * fork/exec (POSIX) or CreateProcess (Windows).
 *
 * @param pair Output parameter that receives the read and write pipe handles.
 * @return true if the pipe pair was created successfully, false otherwise.
 */
IPC_API bool
CreatePipePair(PipePair& pair);

} // namespace adobe::usd::ipc
