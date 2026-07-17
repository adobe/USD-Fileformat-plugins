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

#include <sandbox/protocol/sandboxProtocol.h>

#include <sandbox/debugCodes.h>

#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sandbox {

SandboxProtocol::SandboxProtocol(const std::string& readPipeStr, const std::string& writePipeStr)
  : ProtocolBase("(SANDBOX)")
  , _readPipe(ipc::PipeHandle::FromString(readPipeStr))
  , _writePipe(ipc::PipeHandle::FromString(writePipeStr))
{}

SandboxProtocol::~SandboxProtocol()
{
    _readPipe.Close();
    _writePipe.Close();
}

bool
SandboxProtocol::ReceiveFileFormatArgs(std::map<std::string, std::string>& fileFormatArgs)
{
    if (_state != SandboxState::Initialized) {
        TF_CODING_ERROR("(SANDBOX) ReceiveFileFormatArgs called in wrong state");
        return false;
    }

    std::vector<uint8_t> data;
    if (!ReadMessage(_readPipe, data)) {
        TF_WARN("(SANDBOX) Failed to receive file format arguments from host");
        return false;
    }

    serialization::BufferReader reader(data);
    FileFormatArgsMessage msg = FileFormatArgsMessage::ReadFrom(reader);
    if (reader.HasError()) {
        TF_WARN("(SANDBOX) Failed to decode file format arguments message");
        return false;
    }

    fileFormatArgs = msg.args;
    _state = SandboxState::ArgsReceived;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Received file format arguments\n");
    return true;
}

bool
SandboxProtocol::SendAssetSize(size_t assetsSize)
{
    if (_state != SandboxState::ArgsReceived) {
        TF_CODING_ERROR("(SANDBOX) SendAssetSize called in wrong state");
        return false;
    }

    AssetSizeMessage msg;
    msg.assetsSize = assetsSize;

    serialization::BufferWriter writer;
    msg.WriteTo(writer);

    if (!WriteMessage(_writePipe, writer.Finish())) {
        TF_WARN("(SANDBOX) Failed to send asset size to host");
        return false;
    }

    _state = SandboxState::SizeSent;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Sent asset size: %zu\n", assetsSize);
    return true;
}

bool
SandboxProtocol::ReceiveAndConnectSharedMemory()
{
    if (_state != SandboxState::SizeSent && _state != SandboxState::ArgsReceived) {
        TF_CODING_ERROR("(SANDBOX) ReceiveAndConnectSharedMemory called in wrong state");
        return false;
    }
    if (!_sharedMemory) {
        TF_WARN("(SANDBOX) Shared memory object not available");
        return false;
    }

    std::vector<uint8_t> data;
    if (!ReadMessage(_readPipe, data)) {
        TF_WARN("(SANDBOX) Failed to receive shared memory info from host");
        return false;
    }

    serialization::BufferReader reader(data);
    SharedMemoryInfoMessage msg = SharedMemoryInfoMessage::ReadFrom(reader);
    if (reader.HasError()) {
        TF_WARN("(SANDBOX) Failed to decode shared memory info message");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(SANDBOX) Connecting to shared memory: name=%s, size=%zu\n",
                 msg.name.c_str(),
                 msg.size);

    if (!_sharedMemory->Connect(msg.name, msg.size)) {
        TF_WARN("(SANDBOX) Failed to connect to shared memory '%s'", msg.name.c_str());
        return false;
    }

    _state = SandboxState::ShmConnected;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(SANDBOX) Connected to shared memory\n");
    return true;
}

} // namespace adobe::usd::sandbox
