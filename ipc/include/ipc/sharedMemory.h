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

#include <memory>
#include <ostream>
#include <string>

namespace adobe::usd::ipc {

/**
 * Abstract interface for platform-independent shared memory. Allows two processes to exchange
 * binary data through a named shared memory region.
 *
 * One process creates the shared memory (typically the host), and another connects to it by name
 * (typically the sandboxed/child process).
 */
class IPC_API SharedMemory
{
public:
    virtual ~SharedMemory() = default;

    /**
     * Create a new shared memory region and map it into this process's address space.
     *
     * @param name Platform-specific name for the shared memory region. This should not be empty.
     * @param size Size in bytes of the shared memory region.
     * @return true if the shared memory was created and mapped successfully.
     */
    virtual bool Create(const std::string& name, size_t size) = 0;

    /**
     * Connect to an existing shared memory region by name and map it into this process's address
     * space.
     *
     * @param name The name of the shared memory region to connect to.
     * @param size The expected size of the shared memory region.
     * @return true if the connection and mapping were successful.
     */
    virtual bool Connect(const std::string& name, size_t size) = 0;

    /**
     * Read data from the shared memory region into a buffer.
     *
     * @param offset Byte offset into the shared memory region.
     * @param buffer Destination buffer (must have at least @p size bytes available).
     * @param size Number of bytes to read.
     * @return true if the read was successful.
     */
    virtual bool Read(size_t offset, void* buffer, size_t size);

    /**
     * Read data from the shared memory region into an output stream.
     *
     * @param offset Byte offset into the shared memory region.
     * @param stream Destination output stream.
     * @param size Number of bytes to read.
     * @return true if the read was successful.
     */
    virtual bool Read(size_t offset, std::ostream& stream, size_t size);

    /**
     * Write data to the shared memory region from a buffer.
     *
     * @param buffer Source buffer.
     * @param size Number of bytes to write.
     * @param offset Byte offset into the shared memory region.
     * @return true if the write was successful.
     */
    virtual bool Write(const void* buffer, size_t size, size_t offset);

    /**
     * Get a raw pointer to the mapped shared memory region. Backed by `_buf`, which every
     * implementation sets in Create()/Connect() and clears in Clean() — concrete here so that
     * member is the single source of truth. Override only if a backend maps memory differently.
     *
     * @return Pointer to the mapped region, or nullptr before Create()/Connect() (or after
     *         Clean()).
     */
    virtual void* GetBuffer() { return _buf; }

    /**
     * Get the size of the shared memory region in bytes. Backed by `_size`, which every
     * implementation sets in Create()/Connect() — concrete here so that member is the single
     * source of truth. Override only if a backend tracks its size differently.
     *
     * @return The size of the mapped region in bytes (0 before Create()/Connect()).
     */
    virtual size_t GetSize() const { return _size; }

    /**
     * Unmap and release the shared memory region. After this call, the shared memory is no longer
     * accessible from this process.
     */
    virtual void Clean() = 0;

protected:
    std::string _name;
    size_t _size = 0;
    void* _buf = nullptr;
};

/// Factory function that creates a platform-appropriate SharedMemory implementation.
IPC_API std::unique_ptr<SharedMemory>
CreateSharedMemory();

} // namespace adobe::usd::ipc
