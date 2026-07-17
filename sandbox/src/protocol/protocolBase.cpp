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

#include <sandbox/protocol/protocolBase.h>

#include <sandbox/protocol/messageIO.h>

#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sandbox {

ProtocolBase::ProtocolBase(const char* sideTag)
  : _sideTag(sideTag)
{
    _sharedMemory = ipc::CreateSharedMemory();
    if (!_sharedMemory) {
        TF_WARN("%s Shared memory object not available", _sideTag);
    }
}

ProtocolBase::~ProtocolBase() = default;

ipc::SharedMemory&
ProtocolBase::GetSharedMemory()
{
    // The shared-memory object is created in the constructor and is required for every asset
    // transfer. A null here means construction failed (see the constructor's warning) and there
    // is no recovery path.
    if (!_sharedMemory) {
        TF_FATAL_ERROR("%s Shared memory object is null; exiting", _sideTag);
    }
    return *_sharedMemory;
}

void
ProtocolBase::CleanSharedMemory()
{
    if (!_sharedMemory) {
        TF_WARN("%s Shared memory object not available", _sideTag);
        return;
    }
    _sharedMemory->Clean();
}

bool
ProtocolBase::WriteMessage(const ipc::PipeHandle& pipe, const std::vector<uint8_t>& data)
{
    if (!WriteMessageToPipe(pipe, data)) {
        TF_WARN("%s Failed to send message", _sideTag);
        return false;
    }
    return true;
}

bool
ProtocolBase::ReadMessage(const ipc::PipeHandle& pipe, std::vector<uint8_t>& data)
{
    if (!ReadMessageFromPipe(pipe, data)) {
        TF_WARN("%s Failed to receive message", _sideTag);
        return false;
    }
    return true;
}

} // namespace adobe::usd::sandbox
