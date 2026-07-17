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

#include <sandbox/protocol/hostProtocol.h>

#include <sandbox/debugCodes.h>

#include <pxr/base/tf/diagnostic.h>

#include <iomanip>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sandbox {

std::atomic<unsigned int> HostProtocol::sInstanceCount{ 0 };

bool
ValidateReportedAssetSize(size_t assetsSize, bool allowLargeAssets)
{
    if (assetsSize <= kMaxSharedMemorySize) {
        return true;
    }
    if (!allowLargeAssets) {
        TF_WARN("(HOST) Sandbox reported asset size %zu exceeding the %zu-byte limit; rejecting. "
                "Set the 'sandboxAllowLargeAssets' file format argument to allow larger assets.",
                assetsSize,
                kMaxSharedMemorySize);
        return false;
    }
    // Over the cap but explicitly allowed. The cap is the host's only defense against a compromised
    // sandbox over-reporting its asset size to force a large allocation, and the override disables
    // it for this import.
    TF_WARN("(HOST) 'sandboxAllowLargeAssets' is set: accepting worker-reported asset size %zu "
            "above the %zu-byte cap. The host is not protected against a compromised sandbox "
            "over-reporting its asset size to force a large allocation.",
            assetsSize,
            kMaxSharedMemorySize);
    return true;
}

HostProtocol::HostProtocol()
  : ProtocolBase("(HOST)")
{
    ++sInstanceCount;
}

HostProtocol::~HostProtocol()
{
    _toChildPipe.readEnd.Close();
    _toChildPipe.writeEnd.Close();
    _toParentPipe.readEnd.Close();
    _toParentPipe.writeEnd.Close();

    // If a worker was launched but never reaped on the normal path (e.g. an early-failure return
    // before WaitForCompletion), give it a bounded grace period to exit so its output drains to
    // the terminal, then force-kill it so a hung or crashed worker cannot wedge the host. On the
    // normal path WaitForCompletion has already reaped it and these calls are no-ops.
    if (_process && _state != HostState::Completed) {
        int exitCode = -1;
        if (!_process->WaitFor(kShutdownTimeoutMs, exitCode)) {
            TF_WARN("(HOST) Sandbox worker did not exit within %d ms during shutdown; terminating.",
                    kShutdownTimeoutMs);
            _process->Terminate();
        }
    }
}

bool
HostProtocol::LaunchProcess(const std::string& processPath,
                            const std::string& resolvedPath,
                            const std::string& unsafePluginRoot,
                            bool isExport,
                            ipc::Process::PosixPreExecHook preExecHook,
                            const std::string& commandPrefix)
{
    if (_state != HostState::Initialized) {
        TF_CODING_ERROR("(HOST) LaunchProcess called in wrong state");
        return false;
    }

    if (!ipc::CreatePipePair(_toChildPipe)) {
        TF_WARN("(HOST) Failed to create pipe for communicating to child");
        return false;
    }
    if (!ipc::CreatePipePair(_toParentPipe)) {
        TF_WARN("(HOST) Failed to create pipe for communicating from child");
        return false;
    }

    // Build command line: [prefix parts...] executable resolvedPath pluginRoot readPipe writePipe
    // isExport
    std::vector<std::string> commandAndArgs;

    if (!commandPrefix.empty()) {
        // Parse prefix into tokens (for sandbox-exec -p "profile")
        std::istringstream prefixStream(commandPrefix);
        std::string token;
        while (prefixStream >> std::quoted(token)) {
            commandAndArgs.push_back(token);
        }
    }

    commandAndArgs.push_back(processPath);
    commandAndArgs.push_back(resolvedPath);
    commandAndArgs.push_back(unsafePluginRoot);
    commandAndArgs.push_back(_toChildPipe.readEnd.ToString());
    commandAndArgs.push_back(_toParentPipe.writeEnd.ToString());
    commandAndArgs.push_back(isExport ? "true" : "false");

    _process = ipc::CreateSubprocess();
    if (!_process) {
        TF_WARN("(HOST) Failed to create process object");
        return false;
    }
    if (!_process->Launch(commandAndArgs, preExecHook)) {
        TF_WARN("(HOST) Failed to launch sandboxed process");
        return false;
    }

    // Close child-side pipe ends in the parent
    _toChildPipe.readEnd.Close();
    _toParentPipe.writeEnd.Close();

    _state = HostState::ProcessLaunched;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Process launched successfully\n");
    return true;
}

bool
HostProtocol::SendFileFormatArgs(const std::map<std::string, std::string>& fileFormatArgs)
{
    if (_state != HostState::ProcessLaunched) {
        TF_CODING_ERROR("(HOST) SendFileFormatArgs called in wrong state");
        return false;
    }

    FileFormatArgsMessage msg;
    msg.args = fileFormatArgs;

    serialization::BufferWriter writer;
    msg.WriteTo(writer);

    if (!WriteMessage(_toChildPipe.writeEnd, writer.Finish())) {
        TF_WARN("(HOST) Failed to send file format arguments");
        return false;
    }

    _state = HostState::ArgsSent;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) File format arguments sent\n");
    return true;
}

bool
HostProtocol::ReceiveAssetSize(size_t& assetsSize, bool allowLargeAssets)
{
    if (_state != HostState::ArgsSent) {
        TF_CODING_ERROR("(HOST) ReceiveAssetSize called in wrong state");
        return false;
    }

    std::vector<uint8_t> data;
    if (!ReadMessage(_toParentPipe.readEnd, data)) {
        TF_WARN("(HOST) Failed to receive asset size from sandbox");
        return false;
    }

    serialization::BufferReader reader(data);
    AssetSizeMessage msg = AssetSizeMessage::ReadFrom(reader);
    if (reader.HasError()) {
        TF_WARN("(HOST) Failed to decode asset size message");
        return false;
    }

    if (!ValidateReportedAssetSize(msg.assetsSize, allowLargeAssets)) {
        return false;
    }
    assetsSize = msg.assetsSize;
    _state = HostState::SizeReceived;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Received asset size: %zu\n", assetsSize);
    return true;
}

bool
HostProtocol::CreateSharedMemory(const std::string& shmNamePrefix, size_t dataSize)
{
    if (_state != HostState::SizeReceived && _state != HostState::ArgsSent) {
        TF_CODING_ERROR("(HOST) CreateSharedMemory called in wrong state");
        return false;
    }
    if (dataSize == 0) {
        TF_WARN("(HOST) Cannot create shared memory with size 0");
        return false;
    }
    if (!_sharedMemory) {
        TF_WARN("(HOST) Shared memory object not available");
        return false;
    }

    _shmName = shmNamePrefix + std::to_string(sInstanceCount.load());
    _shmSize = dataSize;

    if (!_sharedMemory->Create(_shmName, dataSize)) {
        TF_WARN("(HOST) Failed to create shared memory '%s'", _shmName.c_str());
        return false;
    }

    _state = HostState::ShmCreated;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Shared memory created: name=%s, size=%zu\n",
                 _shmName.c_str(),
                 dataSize);
    return true;
}

bool
HostProtocol::AnnounceSharedMemory()
{
    if (_state != HostState::ShmCreated) {
        TF_CODING_ERROR("(HOST) AnnounceSharedMemory called in wrong state");
        return false;
    }

    SharedMemoryInfoMessage msg;
    msg.name = _shmName;
    msg.size = _shmSize;

    serialization::BufferWriter writer;
    msg.WriteTo(writer);

    if (!WriteMessage(_toChildPipe.writeEnd, writer.Finish())) {
        TF_WARN("(HOST) Failed to send shared memory info to sandbox");
        return false;
    }

    _state = HostState::ShmReady;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Shared memory announced: name=%s, size=%zu\n",
                 _shmName.c_str(),
                 _shmSize);
    return true;
}

bool
HostProtocol::InitializeSharedMemory(const std::string& shmNamePrefix, size_t dataSize)
{
    // Import convenience: the worker is the writer and the host blocks on ReceiveAssetSize before
    // calling this, so creating and announcing together is race-free here. Export must instead use
    // CreateSharedMemory -> write payload -> AnnounceSharedMemory.
    if (_state != HostState::SizeReceived) {
        TF_CODING_ERROR("(HOST) InitializeSharedMemory called in wrong state");
        return false;
    }
    return CreateSharedMemory(shmNamePrefix, dataSize) && AnnounceSharedMemory();
}

bool
HostProtocol::WaitForCompletion()
{
    if (_state != HostState::ShmReady) {
        TF_CODING_ERROR("(HOST) WaitForCompletion called in wrong state");
        return false;
    }
    if (!_process) {
        TF_WARN("(HOST) No process to wait for");
        return false;
    }

    int exitCode = -1;
    if (!_process->Wait(exitCode)) {
        TF_WARN("(HOST) Failed to wait for sandboxed process");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Sandboxed process exit code: %d\n", exitCode);

    if (exitCode != 0) {
        TF_WARN("(HOST) Sandboxed process exited with code: %d", exitCode);
        return false;
    }

    _state = HostState::Completed;
    return true;
}

} // namespace adobe::usd::sandbox
