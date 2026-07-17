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
#include <sandbox/protocol/messageIO.h>
#include <sandbox/protocol/messages.h>

#include <ipc/pipe.h>
#include <serialization/buffer.h>

#include <pxr/base/tf/errorMark.h>

#include <gtest/gtest.h>

using namespace adobe::usd;
using namespace adobe::usd::sandbox;
using namespace adobe::usd::serialization;

TEST(ProtocolMessageTests, FileFormatArgsMessageRoundTrip)
{
    FileFormatArgsMessage original;
    original.args = {
        { "format", "fbx" },
        { "quality", "high" },
        { "importMaterials", "true" },
    };

    BufferWriter writer;
    original.WriteTo(writer);
    auto data = writer.Finish();

    BufferReader reader(data);
    FileFormatArgsMessage decoded = FileFormatArgsMessage::ReadFrom(reader);
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(decoded.args, original.args);
}

TEST(ProtocolMessageTests, FileFormatArgsMessageEmpty)
{
    FileFormatArgsMessage original;

    BufferWriter writer;
    original.WriteTo(writer);
    auto data = writer.Finish();

    BufferReader reader(data);
    FileFormatArgsMessage decoded = FileFormatArgsMessage::ReadFrom(reader);
    EXPECT_FALSE(reader.HasError());
    EXPECT_TRUE(decoded.args.empty());
}

TEST(ProtocolMessageTests, AssetSizeMessageRoundTrip)
{
    AssetSizeMessage original;
    original.assetsSize = 1048576; // 1 MB

    BufferWriter writer;
    original.WriteTo(writer);
    auto data = writer.Finish();

    BufferReader reader(data);
    AssetSizeMessage decoded = AssetSizeMessage::ReadFrom(reader);
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(decoded.assetsSize, original.assetsSize);
}

TEST(ProtocolMessageTests, AssetSizeMessageZero)
{
    AssetSizeMessage original;
    original.assetsSize = 0;

    BufferWriter writer;
    original.WriteTo(writer);
    auto data = writer.Finish();

    BufferReader reader(data);
    AssetSizeMessage decoded = AssetSizeMessage::ReadFrom(reader);
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(decoded.assetsSize, 0u);
}

TEST(ProtocolMessageTests, SharedMemoryInfoMessageRoundTrip)
{
    SharedMemoryInfoMessage original;
    original.name = "/UsdSHM_42";
    original.size = 2097152; // 2 MB

    BufferWriter writer;
    original.WriteTo(writer);
    auto data = writer.Finish();

    BufferReader reader(data);
    SharedMemoryInfoMessage decoded = SharedMemoryInfoMessage::ReadFrom(reader);
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(decoded.name, original.name);
    EXPECT_EQ(decoded.size, original.size);
}

TEST(ProtocolMessageTests, SharedMemoryInfoMessageWindowsName)
{
    SharedMemoryInfoMessage original;
    original.name = "Local\\UsdSharedMemory_1";
    original.size = 4096;

    BufferWriter writer;
    original.WriteTo(writer);
    auto data = writer.Finish();

    BufferReader reader(data);
    SharedMemoryInfoMessage decoded = SharedMemoryInfoMessage::ReadFrom(reader);
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(decoded.name, original.name);
    EXPECT_EQ(decoded.size, original.size);
}

TEST(ProtocolMessageTests, AllMessagesSequential)
{
    // Simulate a full import protocol message sequence
    std::vector<uint8_t> argsData, sizeData, shmData;

    {
        FileFormatArgsMessage msg;
        msg.args = { { "format", "gltf" }, { "textureMode", "embedded" } };
        BufferWriter writer;
        msg.WriteTo(writer);
        argsData = writer.Finish();
    }

    {
        AssetSizeMessage msg;
        msg.assetsSize = 500000;
        BufferWriter writer;
        msg.WriteTo(writer);
        sizeData = writer.Finish();
    }

    {
        SharedMemoryInfoMessage msg;
        msg.name = "/UsdSHM_test";
        msg.size = 500000;
        BufferWriter writer;
        msg.WriteTo(writer);
        shmData = writer.Finish();
    }

    // Verify each can be independently decoded
    {
        BufferReader reader(argsData);
        auto msg = FileFormatArgsMessage::ReadFrom(reader);
        EXPECT_FALSE(reader.HasError());
        EXPECT_EQ(msg.args["format"], "gltf");
        EXPECT_EQ(msg.args["textureMode"], "embedded");
    }

    {
        BufferReader reader(sizeData);
        auto msg = AssetSizeMessage::ReadFrom(reader);
        EXPECT_FALSE(reader.HasError());
        EXPECT_EQ(msg.assetsSize, 500000u);
    }

    {
        BufferReader reader(shmData);
        auto msg = SharedMemoryInfoMessage::ReadFrom(reader);
        EXPECT_FALSE(reader.HasError());
        EXPECT_EQ(msg.name, "/UsdSHM_test");
        EXPECT_EQ(msg.size, 500000u);
    }
}

TEST(MessageIOTests, RoundTripThroughPipe)
{
    ipc::PipePair pipe;
    ASSERT_TRUE(ipc::CreatePipePair(pipe));

    std::vector<uint8_t> payload = { 1, 2, 3, 4, 5 };
    EXPECT_TRUE(WriteMessageToPipe(pipe.writeEnd, payload));

    std::vector<uint8_t> received;
    EXPECT_TRUE(ReadMessageFromPipe(pipe.readEnd, received));
    EXPECT_EQ(received, payload);
}

TEST(MessageIOTests, OversizedDeclaredSizeRejectedWithoutReadingBody)
{
    ipc::PipePair pipe;
    ASSERT_TRUE(ipc::CreatePipePair(pipe));

    // Write ONLY a 4-byte length prefix declaring a huge body, and no body at all.
    // ReadMessageFromPipe must reject after reading the prefix, without blocking on / allocating
    // the body.
    uint32_t hugeSize = kMaxMessageSize + 1;
    ASSERT_TRUE(pipe.writeEnd.Write(&hugeSize, sizeof(hugeSize)));
    pipe.writeEnd.Close(); // ensure no body is forthcoming

    std::vector<uint8_t> received;
    EXPECT_FALSE(ReadMessageFromPipe(pipe.readEnd, received));
    EXPECT_TRUE(received.empty());
}

TEST(MessageIOTests, ZeroLengthRejected)
{
    ipc::PipePair pipe;
    ASSERT_TRUE(ipc::CreatePipePair(pipe));

    uint32_t zero = 0;
    ASSERT_TRUE(pipe.writeEnd.Write(&zero, sizeof(zero)));
    pipe.writeEnd.Close();

    std::vector<uint8_t> received;
    EXPECT_FALSE(ReadMessageFromPipe(pipe.readEnd, received));
}

TEST(HostProtocolStateTests, AnnounceBeforeCreateIsRejected)
{
    HostProtocol host;
    // Fresh protocol is in Initialized; AnnounceSharedMemory requires ShmCreated.
    // A TF_CODING_ERROR is expected; suppress its abort and assert graceful failure.
    pxr::TfErrorMark mark;
    EXPECT_FALSE(host.AnnounceSharedMemory());
    EXPECT_FALSE(mark.IsClean()) << "expected a coding error to be posted";
    EXPECT_EQ(host.GetState(), HostState::Initialized);
    mark.Clear();
}

TEST(HostProtocolStateTests, CreateSharedMemoryRejectedInInitializedState)
{
    HostProtocol host;
    pxr::TfErrorMark mark;
    EXPECT_FALSE(host.CreateSharedMemory("/UsdSHM_test", 4096));
    EXPECT_FALSE(mark.IsClean()) << "expected a coding error to be posted";
    EXPECT_EQ(host.GetState(), HostState::Initialized);
    mark.Clear();
}

// Sizes up to the cap (4GB - 1) are accepted; above it they are rejected unless the caller opts
// into large assets. 5 GiB is only a number here — the cap is checked before any allocation, so
// the over-cap cases allocate nothing. (The over-cap cases intentionally emit a TF_WARN.)
TEST(ValidateReportedAssetSize, CapAndOverride)
{
    constexpr size_t fiveGiB = static_cast<size_t>(5) * 1024 * 1024 * 1024;
    EXPECT_TRUE(ValidateReportedAssetSize(1024, false));
    EXPECT_TRUE(ValidateReportedAssetSize(kMaxSharedMemorySize, false));      // boundary accepted
    EXPECT_FALSE(ValidateReportedAssetSize(kMaxSharedMemorySize + 1, false)); // just over: reject
    EXPECT_FALSE(ValidateReportedAssetSize(fiveGiB, false));
    EXPECT_TRUE(ValidateReportedAssetSize(fiveGiB, true)); // override lifts cap
    EXPECT_TRUE(ValidateReportedAssetSize(1024, true));
}
