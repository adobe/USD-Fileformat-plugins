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
#include <ipc/process.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>

namespace adobe::usd::sandbox {

/**
 * Ceiling (4 GiB - 1) on the shared-memory size the host will allocate for a worker-reported asset
 * size, unless the caller opts into larger transfers. The reported size is untrusted input from the
 * sandboxed worker, so bounding it stops a malicious or buggy worker from driving an unbounded host
 * allocation.
 */
constexpr size_t kMaxSharedMemorySize = std::numeric_limits<uint32_t>::max();

/**
 * Validate an untrusted worker-reported asset size against kMaxSharedMemorySize and return whether
 * it may be accepted. When the size is over the cap this warns either way: rejecting it, or (with
 * allowLargeAssets) accepting it while noting the host is no longer guarded against a compromised
 * worker inflating its size.
 *
 * @param assetsSize The size reported by the sandboxed worker.
 * @param allowLargeAssets Accept sizes above kMaxSharedMemorySize for legitimate large assets.
 *
 * @return true if the size may be accepted.
 */
USDSANDBOX_API bool
ValidateReportedAssetSize(size_t assetsSize, bool allowLargeAssets);

/**
 * Tracks the state of the host-side sandbox protocol. Each method enforces at runtime
 * that the protocol is in the correct state before proceeding, posting a coding error
 * and returning false otherwise.
 */
enum class HostState
{
    Initialized,
    ProcessLaunched,
    ArgsSent,
    SizeReceived,
    ShmCreated,
    ShmReady,
    Completed
};

/**
 * Manages the host side of the sandbox communication protocol. Owns pipe handles,
 * shared memory, and the child process. Enforces correct ordering of protocol steps.
 *
 * Usage (import):
 *   1. Construct
 *   2. LaunchProcess(...)
 *   3. SendFileFormatArgs(...)
 *   4. ReceiveAssetSize(...)
 *   5. InitializeSharedMemory(...)
 *   6. WaitForCompletion()
 *
 * Usage (export):
 *   1. Construct
 *   2. LaunchProcess(...)
 *   3. SendFileFormatArgs(...)
 *   4. CreateSharedMemory(...)   [allocate only]
 *   5. host writes the payload into GetSharedMemory()
 *   6. AnnounceSharedMemory()    [now safe for the worker to read]
 *   7. WaitForCompletion()
 */
class USDSANDBOX_API HostProtocol : public ProtocolBase
{
public:
    HostProtocol();
    ~HostProtocol();

    HostProtocol(const HostProtocol&) = delete;
    HostProtocol& operator=(const HostProtocol&) = delete;

    /**
     * Launch the sandboxed child process.
     *
     * @param processPath Path to the SandboxedProcess executable.
     * @param resolvedPath Path to the asset being converted.
     * @param unsafePluginRoot Root directory for unsafe plugins.
     * @param isExport True if this is an export operation.
     * @param preExecHook Optional hook called in the child (POSIX only) before exec.
     * @param commandPrefix Optional prefix for the command (e.g. "sandbox-exec -p ...").
     * @return true if the process was launched successfully.
     */
    bool LaunchProcess(const std::string& processPath,
                       const std::string& resolvedPath,
                       const std::string& unsafePluginRoot,
                       bool isExport,
                       ipc::Process::PosixPreExecHook preExecHook = nullptr,
                       const std::string& commandPrefix = "");

    /// Send file format arguments to the sandbox process.
    bool SendFileFormatArgs(const std::map<std::string, std::string>& fileFormatArgs);

    /**
     * Receive the total asset size from the sandbox process (import flow only). The size is
     * untrusted input from the worker and is rejected if it exceeds kMaxSharedMemorySize, unless
     * allowLargeAssets lifts that cap.
     *
     * @param assetsSize Set to the received size on success.
     * @param allowLargeAssets Accept sizes above kMaxSharedMemorySize (for legitimate large
     *        assets). Defaults to false.
     *
     * @return true if a size within the accepted range was received.
     */
    bool ReceiveAssetSize(size_t& assetsSize, bool allowLargeAssets = false);

    /**
     * Allocate shared memory without announcing it to the worker. Valid from state SizeReceived
     * (import) or ArgsSent (export). Transitions to ShmCreated.
     *
     * @param shmNamePrefix Platform-specific prefix for the shared memory name.
     * @param dataSize Size in bytes of the shared memory to allocate.
     * @return true if shared memory was created successfully.
     */
    bool CreateSharedMemory(const std::string& shmNamePrefix, size_t dataSize);

    /**
     * Send the stored shared memory name/size to the worker. Valid from state ShmCreated.
     * Transitions to ShmReady.
     *
     * @return true if the announcement was sent successfully.
     */
    bool AnnounceSharedMemory();

    /**
     * Import convenience: allocate shared memory and immediately announce it to the worker.
     * Equivalent to CreateSharedMemory + AnnounceSharedMemory. Valid from state SizeReceived.
     * On export, use CreateSharedMemory -> write payload -> AnnounceSharedMemory instead.
     *
     * @param shmNamePrefix Platform-specific prefix for the shared memory name.
     * @param dataSize Size in bytes of the shared memory to allocate.
     * @return true if shared memory was created and info sent successfully.
     */
    bool InitializeSharedMemory(const std::string& shmNamePrefix, size_t dataSize);

    /// Wait for the sandbox process to finish and return success/failure.
    bool WaitForCompletion();

    HostState GetState() const { return _state; }

private:
    static constexpr int kShutdownTimeoutMs = 5000;

    HostState _state = HostState::Initialized;

    ipc::PipePair _toChildPipe;
    ipc::PipePair _toParentPipe;

    std::unique_ptr<ipc::Process> _process;

    std::string _shmName;
    size_t _shmSize = 0;

    static std::atomic<unsigned int> sInstanceCount;
};

} // namespace adobe::usd::sandbox
