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

#include <sandbox/api.h>

#include <ipc/pipe.h>
#include <ipc/sharedMemory.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace adobe::usd::sandbox {

/**
 * Shared implementation for the two ends of the sandbox protocol. Owns the shared-memory object
 * and provides length-prefixed message send/receive over a caller-supplied pipe. Not instantiated
 * directly (protected constructor); HostProtocol and SandboxProtocol derive from it and add their
 * own pipe ownership, state machines, and handshake steps.
 */
class USDSANDBOX_API ProtocolBase
{
public:
    /** Get the shared memory object for reading/writing assets. Never null: the object is
     *  created at construction, and this fatally errors if it is somehow null. */
    ipc::SharedMemory& GetSharedMemory();

    /// Clean up shared memory resources.
    void CleanSharedMemory();

protected:
    // sideTag: short label for diagnostics, e.g. "(HOST)" or "(SANDBOX)".
    explicit ProtocolBase(const char* sideTag);
    ~ProtocolBase();

    ProtocolBase(const ProtocolBase&) = delete;
    ProtocolBase& operator=(const ProtocolBase&) = delete;

    // Write a length-prefixed message over the given pipe. Returns false on failure.
    bool WriteMessage(const ipc::PipeHandle& pipe, const std::vector<uint8_t>& data);

    // Read a length-prefixed message from the given pipe into data. Returns false on failure.
    bool ReadMessage(const ipc::PipeHandle& pipe, std::vector<uint8_t>& data);

    const char* _sideTag;
    std::unique_ptr<ipc::SharedMemory> _sharedMemory;
};

} // namespace adobe::usd::sandbox
