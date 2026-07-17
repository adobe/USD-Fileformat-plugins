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

#include <sandbox/protocol/messages.h>

namespace adobe::usd::sandbox {

void
FileFormatArgsMessage::WriteTo(serialization::BufferWriter& writer) const
{
    writer.WriteMap(args);
}

FileFormatArgsMessage
FileFormatArgsMessage::ReadFrom(serialization::BufferReader& reader)
{
    FileFormatArgsMessage msg;
    msg.args = reader.ReadMap();
    return msg;
}

void
AssetSizeMessage::WriteTo(serialization::BufferWriter& writer) const
{
    writer.WriteSizeT(assetsSize);
}

AssetSizeMessage
AssetSizeMessage::ReadFrom(serialization::BufferReader& reader)
{
    AssetSizeMessage msg;
    msg.assetsSize = reader.ReadSizeT();
    return msg;
}

void
SharedMemoryInfoMessage::WriteTo(serialization::BufferWriter& writer) const
{
    writer.WriteString(name);
    writer.WriteSizeT(size);
}

SharedMemoryInfoMessage
SharedMemoryInfoMessage::ReadFrom(serialization::BufferReader& reader)
{
    SharedMemoryInfoMessage msg;
    msg.name = reader.ReadString();
    msg.size = reader.ReadSizeT();
    return msg;
}

} // namespace adobe::usd::sandbox
