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

#include <cstdint>
#include <vector>

namespace adobe::usd::sandbox {

/**
 * Maximum accepted size, in bytes, of a single control message. Control messages
 * (file-format args, asset size, shared-memory info) are small; the bulk asset payload travels
 * through shared memory and is never sent as a message. This cap bounds the allocation a receiver
 * performs in response to a length prefix that may be attacker-controlled (a buggy or compromised
 * peer across the sandbox boundary).
 */
constexpr uint32_t kMaxMessageSize = 16u * 1024u * 1024u; // 16 MiB

/**
 * Write a length-prefixed message to the pipe: a 4-byte native-endian uint32 size followed by the
 * bytes. Host and worker share an architecture (same machine, same build), so native byte order is
 * safe on the wire. Returns false on short write, or if the message exceeds kMaxMessageSize.
 */
USDSANDBOX_API bool
WriteMessageToPipe(const ipc::PipeHandle& pipe, const std::vector<uint8_t>& data);

/**
 * Read a length-prefixed message into out. Reads the 4-byte size first and rejects a declared
 * size of 0 or one exceeding kMaxMessageSize BEFORE allocating or reading any body, so a
 * hostile size cannot drive a large allocation. Returns false on read failure or a rejected size;
 * out is left empty on failure.
 */
USDSANDBOX_API bool
ReadMessageFromPipe(const ipc::PipeHandle& pipe, std::vector<uint8_t>& out);

} // namespace adobe::usd::sandbox
