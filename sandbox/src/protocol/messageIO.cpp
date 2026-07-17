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

#include <sandbox/protocol/messageIO.h>

#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sandbox {

bool
WriteMessageToPipe(const ipc::PipeHandle& pipe, const std::vector<uint8_t>& data)
{
    if (data.size() > kMaxMessageSize) {
        TF_WARN("Message size %zu exceeds maximum %u; refusing to send",
                data.size(),
                static_cast<unsigned>(kMaxMessageSize));
        return false;
    }
    uint32_t size = static_cast<uint32_t>(data.size());
    if (!pipe.Write(&size, sizeof(size))) {
        TF_WARN("Failed to write message size to pipe");
        return false;
    }
    if (!pipe.Write(data.data(), size)) {
        TF_WARN("Failed to write message data to pipe");
        return false;
    }
    return true;
}

bool
ReadMessageFromPipe(const ipc::PipeHandle& pipe, std::vector<uint8_t>& out)
{
    out.clear();

    uint32_t size = 0;
    if (!pipe.Read(&size, sizeof(size))) {
        TF_WARN("Failed to read message size from pipe. Peer may have crashed.");
        return false;
    }
    if (size == 0) {
        TF_WARN("Received zero-length message from pipe");
        return false;
    }
    if (size > kMaxMessageSize) {
        TF_WARN("Declared message size %u exceeds maximum %u; rejecting", size, kMaxMessageSize);
        return false;
    }

    out.resize(size);
    if (!pipe.Read(out.data(), size)) {
        TF_WARN("Failed to read message data from pipe. Peer may have crashed.");
        out.clear();
        return false;
    }
    return true;
}

} // namespace adobe::usd::sandbox
