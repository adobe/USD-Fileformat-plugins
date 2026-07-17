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

#include <serialization/buffer.h>

#include <gtest/gtest.h>

using namespace adobe::usd::serialization;

TEST(BufferTests, Uint32RoundTrip)
{
    BufferWriter writer;
    writer.WriteUint32(0);
    writer.WriteUint32(42);
    writer.WriteUint32(UINT32_MAX);
    auto data = writer.Finish();

    BufferReader reader(data);
    EXPECT_EQ(reader.ReadUint32(), 0u);
    EXPECT_EQ(reader.ReadUint32(), 42u);
    EXPECT_EQ(reader.ReadUint32(), UINT32_MAX);
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(reader.BytesRemaining(), 0u);
}

TEST(BufferTests, Uint64RoundTrip)
{
    BufferWriter writer;
    writer.WriteUint64(0);
    writer.WriteUint64(UINT64_MAX);
    writer.WriteUint64(123456789012345ULL);
    auto data = writer.Finish();

    BufferReader reader(data);
    EXPECT_EQ(reader.ReadUint64(), 0u);
    EXPECT_EQ(reader.ReadUint64(), UINT64_MAX);
    EXPECT_EQ(reader.ReadUint64(), 123456789012345ULL);
    EXPECT_FALSE(reader.HasError());
}

TEST(BufferTests, SizeTRoundTrip)
{
    BufferWriter writer;
    writer.WriteSizeT(0);
    writer.WriteSizeT(999999);
    auto data = writer.Finish();

    // size_t is always serialized as uint64_t
    EXPECT_EQ(data.size(), 2 * sizeof(uint64_t));

    BufferReader reader(data);
    EXPECT_EQ(reader.ReadSizeT(), 0u);
    EXPECT_EQ(reader.ReadSizeT(), 999999u);
    EXPECT_FALSE(reader.HasError());
}

TEST(BufferTests, StringRoundTrip)
{
    BufferWriter writer;
    writer.WriteString("hello");
    writer.WriteString("");
    writer.WriteString("world");
    auto data = writer.Finish();

    BufferReader reader(data);
    EXPECT_EQ(reader.ReadString(), "hello");
    EXPECT_EQ(reader.ReadString(), "");
    EXPECT_EQ(reader.ReadString(), "world");
    EXPECT_FALSE(reader.HasError());
}

TEST(BufferTests, StringWithSpecialChars)
{
    BufferWriter writer;
    writer.WriteString("line1\nline2");
    writer.WriteString("key=value");
    writer.WriteString("path/to/file.usd");
    auto data = writer.Finish();

    BufferReader reader(data);
    EXPECT_EQ(reader.ReadString(), "line1\nline2");
    EXPECT_EQ(reader.ReadString(), "key=value");
    EXPECT_EQ(reader.ReadString(), "path/to/file.usd");
    EXPECT_FALSE(reader.HasError());
}

TEST(BufferTests, MapRoundTrip)
{
    std::map<std::string, std::string> original = {
        { "alpha", "one" },
        { "beta", "two" },
        { "gamma", "three" },
    };

    BufferWriter writer;
    writer.WriteMap(original);
    auto data = writer.Finish();

    BufferReader reader(data);
    auto decoded = reader.ReadMap();
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(decoded, original);
}

TEST(BufferTests, EmptyMap)
{
    std::map<std::string, std::string> empty;

    BufferWriter writer;
    writer.WriteMap(empty);
    auto data = writer.Finish();

    BufferReader reader(data);
    auto decoded = reader.ReadMap();
    EXPECT_FALSE(reader.HasError());
    EXPECT_TRUE(decoded.empty());
}

TEST(BufferTests, BytesRoundTrip)
{
    std::vector<uint8_t> original = { 0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD };

    BufferWriter writer;
    writer.WriteSizeT(original.size());
    writer.WriteBytes(original.data(), original.size());
    auto data = writer.Finish();

    BufferReader reader(data);
    size_t len = reader.ReadSizeT();
    EXPECT_EQ(len, original.size());

    std::vector<uint8_t> decoded(len);
    EXPECT_TRUE(reader.ReadBytes(decoded.data(), len));
    EXPECT_EQ(decoded, original);
    EXPECT_FALSE(reader.HasError());
}

TEST(BufferTests, MixedTypes)
{
    BufferWriter writer;
    writer.WriteUint32(1);
    writer.WriteString("mixed");
    writer.WriteSizeT(100);
    writer.WriteUint64(999);
    writer.WriteMap({ { "x", "y" } });
    auto data = writer.Finish();

    BufferReader reader(data);
    EXPECT_EQ(reader.ReadUint32(), 1u);
    EXPECT_EQ(reader.ReadString(), "mixed");
    EXPECT_EQ(reader.ReadSizeT(), 100u);
    EXPECT_EQ(reader.ReadUint64(), 999u);
    auto map = reader.ReadMap();
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map["x"], "y");
    EXPECT_FALSE(reader.HasError());
}

TEST(BufferTests, ReadPastEndSetsError)
{
    BufferWriter writer;
    writer.WriteUint32(42);
    auto data = writer.Finish();

    BufferReader reader(data);
    EXPECT_EQ(reader.ReadUint32(), 42u);
    EXPECT_FALSE(reader.HasError());

    // Now there's no data left
    uint32_t val = reader.ReadUint32();
    EXPECT_EQ(val, 0u);
    EXPECT_TRUE(reader.HasError());
}

TEST(BufferTests, ErrorStatePersists)
{
    std::vector<uint8_t> data = { 0x01 }; // Only 1 byte
    BufferReader reader(data);

    // Trying to read 4 bytes should fail
    reader.ReadUint32();
    EXPECT_TRUE(reader.HasError());

    // Subsequent reads should also fail
    reader.ReadString();
    EXPECT_TRUE(reader.HasError());
    EXPECT_EQ(reader.BytesRemaining(), 0u);
}

TEST(BufferTests, EmptyBuffer)
{
    std::vector<uint8_t> empty;
    BufferReader reader(empty);
    EXPECT_EQ(reader.BytesRemaining(), 0u);

    reader.ReadUint32();
    EXPECT_TRUE(reader.HasError());
}

TEST(BufferTests, FinishResetsWriter)
{
    BufferWriter writer;
    writer.WriteUint32(1);
    auto data1 = writer.Finish();
    EXPECT_EQ(writer.Size(), 0u);

    writer.WriteUint32(2);
    auto data2 = writer.Finish();

    BufferReader reader1(data1);
    EXPECT_EQ(reader1.ReadUint32(), 1u);

    BufferReader reader2(data2);
    EXPECT_EQ(reader2.ReadUint32(), 2u);
}

TEST(BufferTests, LargeMap)
{
    std::map<std::string, std::string> large;
    for (int i = 0; i < 100; ++i) {
        large["key_" + std::to_string(i)] = "value_" + std::to_string(i * 10);
    }

    BufferWriter writer;
    writer.WriteMap(large);
    auto data = writer.Finish();

    BufferReader reader(data);
    auto decoded = reader.ReadMap();
    EXPECT_FALSE(reader.HasError());
    EXPECT_EQ(decoded, large);
}

TEST(BufferTests, OversizedStringLengthDoesNotOverread)
{
    // A hostile message: a size_t length prefix of UINT64_MAX, then no payload.
    BufferWriter writer;
    writer.WriteUint64(UINT64_MAX); // becomes the string's length prefix
    auto data = writer.Finish();

    BufferReader reader(data);
    std::string result = reader.ReadString();
    EXPECT_TRUE(reader.HasError());
    EXPECT_TRUE(result.empty());
}

TEST(BufferTests, OversizedReadBytesLengthDoesNotOverread)
{
    std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04 }; // 4 bytes available
    BufferReader reader(data);

    std::vector<uint8_t> dest(8, 0);
    // Request a near-SIZE_MAX read; the overflow-prone check must reject it.
    bool ok = reader.ReadBytes(dest.data(), SIZE_MAX - 1);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(reader.HasError());
}

TEST(BufferTests, MapWithHugeCountTerminatesOnError)
{
    // count = UINT64_MAX, but no entries follow: must error out, not spin or overread.
    BufferWriter writer;
    writer.WriteUint64(UINT64_MAX);
    auto data = writer.Finish();

    BufferReader reader(data);
    auto decoded = reader.ReadMap();
    EXPECT_TRUE(reader.HasError());
    EXPECT_TRUE(decoded.empty());
}
