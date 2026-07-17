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
#include <sandbox/protocol/messages.h>
#include <sandbox/protocol/protocolBase.h>

#include <ipc/pipe.h>

#include <map>
#include <string>

namespace adobe::usd::sandbox {

/// Tracks the state of the sandbox-side protocol.
enum class SandboxState
{
    Initialized,
    ArgsReceived,
    SizeSent,
    ShmConnected,
    Completed
};

/**
 * Manages the sandbox (child) side of the communication protocol. Uses pipe handles
 * received via command-line arguments to communicate with the host process.
 *
 * Usage (import):
 *   1. Construct with pipe handles from argv
 *   2. ReceiveFileFormatArgs(...)
 *   3. SendAssetSize(...)
 *   4. ReceiveAndConnectSharedMemory(...)
 *   5. Write assets to shared memory
 *
 * Usage (export):
 *   1. Construct with pipe handles from argv
 *   2. ReceiveFileFormatArgs(...)
 *   3. ReceiveAndConnectSharedMemory(...)
 *   4. Read assets from shared memory
 */
class USDSANDBOX_API SandboxProtocol : public ProtocolBase
{
public:
    /**
     * Construct the sandbox protocol with pipe handles from the command line.
     *
     * @param readPipeStr String representation of the read pipe handle (from argv).
     * @param writePipeStr String representation of the write pipe handle (from argv).
     */
    SandboxProtocol(const std::string& readPipeStr, const std::string& writePipeStr);
    ~SandboxProtocol();

    SandboxProtocol(const SandboxProtocol&) = delete;
    SandboxProtocol& operator=(const SandboxProtocol&) = delete;

    /// Receive file format arguments from the host process.
    bool ReceiveFileFormatArgs(std::map<std::string, std::string>& fileFormatArgs);

    /// Send the total asset size to the host process (import flow only).
    bool SendAssetSize(size_t assetsSize);

    /// Receive shared memory info from the host and connect to it.
    bool ReceiveAndConnectSharedMemory();

    SandboxState GetState() const { return _state; }

private:
    SandboxState _state = SandboxState::Initialized;

    ipc::PipeHandle _readPipe;
    ipc::PipeHandle _writePipe;
};

} // namespace adobe::usd::sandbox
