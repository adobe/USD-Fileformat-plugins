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
#include <serialization/buffer.h>

#include <cstdint>
#include <map>
#include <string>

namespace adobe::usd::sandbox {

/// Message containing file format arguments sent from host to sandbox.
struct USDSANDBOX_API FileFormatArgsMessage
{
    std::map<std::string, std::string> args;

    /// Serialize this message into @p writer.
    void WriteTo(serialization::BufferWriter& writer) const;
    /// Deserialize a message from @p reader; check reader.HasError() after calling.
    static FileFormatArgsMessage ReadFrom(serialization::BufferReader& reader);
};

/// Message containing the total asset size, sent from sandbox to host during import
/// so the host can allocate shared memory of the correct size.
struct USDSANDBOX_API AssetSizeMessage
{
    size_t assetsSize = 0;

    /// Serialize this message into @p writer.
    void WriteTo(serialization::BufferWriter& writer) const;
    /// Deserialize a message from @p reader; check reader.HasError() after calling.
    static AssetSizeMessage ReadFrom(serialization::BufferReader& reader);
};

/// Message containing shared memory name and size, sent from host to sandbox so it
/// can connect to the shared memory region.
struct USDSANDBOX_API SharedMemoryInfoMessage
{
    std::string name;
    size_t size = 0;

    /// Serialize this message into @p writer.
    void WriteTo(serialization::BufferWriter& writer) const;
    /// Deserialize a message from @p reader; check reader.HasError() after calling.
    static SharedMemoryInfoMessage ReadFrom(serialization::BufferReader& reader);
};

} // namespace adobe::usd::sandbox
