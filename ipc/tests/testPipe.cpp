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

#include <ipc/pipe.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <thread>

using namespace adobe::usd::ipc;

TEST(PipeTests, CreatePipePair)
{
    PipePair pair;
    ASSERT_TRUE(CreatePipePair(pair));
    EXPECT_TRUE(pair.readEnd.IsValid());
    EXPECT_TRUE(pair.writeEnd.IsValid());
    pair.readEnd.Close();
    pair.writeEnd.Close();
}

TEST(PipeTests, WriteAndRead)
{
    PipePair pair;
    ASSERT_TRUE(CreatePipePair(pair));

    const char* message = "Hello, pipe!";
    size_t len = strlen(message) + 1;

    ASSERT_TRUE(pair.writeEnd.Write(message, len));
    pair.writeEnd.Close();

    char buffer[64] = {};
    ASSERT_TRUE(pair.readEnd.Read(buffer, len));
    EXPECT_STREQ(buffer, message);

    pair.readEnd.Close();
}

TEST(PipeTests, WriteAndReadUint32)
{
    PipePair pair;
    ASSERT_TRUE(CreatePipePair(pair));

    uint32_t value = 42;
    ASSERT_TRUE(pair.writeEnd.Write(&value, sizeof(value)));

    uint32_t result = 0;
    ASSERT_TRUE(pair.readEnd.Read(&result, sizeof(result)));
    EXPECT_EQ(result, value);

    pair.readEnd.Close();
    pair.writeEnd.Close();
}

TEST(PipeTests, WriteAndReadLargeData)
{
    PipePair pair;
    ASSERT_TRUE(CreatePipePair(pair));

    const size_t dataSize = 65536;
    std::vector<uint8_t> sendData(dataSize);
    for (size_t i = 0; i < dataSize; ++i) {
        sendData[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Write in a separate thread to avoid blocking on large data
    std::thread writer([&]() {
        pair.writeEnd.Write(sendData.data(), dataSize);
        pair.writeEnd.Close();
    });

    std::vector<uint8_t> recvData(dataSize);
    ASSERT_TRUE(pair.readEnd.Read(recvData.data(), dataSize));
    EXPECT_EQ(sendData, recvData);

    writer.join();
    pair.readEnd.Close();
}

TEST(PipeTests, HandleToStringAndFromString)
{
    PipePair pair;
    ASSERT_TRUE(CreatePipePair(pair));

    std::string readStr = pair.readEnd.ToString();
    std::string writeStr = pair.writeEnd.ToString();

    EXPECT_FALSE(readStr.empty());
    EXPECT_FALSE(writeStr.empty());
    EXPECT_NE(readStr, writeStr);

    // Round-trip: reconstruct from string and verify the handles work
    PipeHandle reconstructedWrite = PipeHandle::FromString(writeStr);
    const char* testMsg = "test";
    ASSERT_TRUE(reconstructedWrite.Write(testMsg, 5));

    char buffer[5] = {};
    ASSERT_TRUE(pair.readEnd.Read(buffer, 5));
    EXPECT_STREQ(buffer, "test");

    pair.readEnd.Close();
    pair.writeEnd.Close();
}

TEST(PipeTests, InvalidHandle)
{
    PipeHandle invalid;
    EXPECT_FALSE(invalid.IsValid());
}

TEST(PipeTests, CloseIdempotent)
{
    PipePair pair;
    ASSERT_TRUE(CreatePipePair(pair));
    pair.readEnd.Close();
    pair.readEnd.Close(); // second close should not crash
    pair.writeEnd.Close();
}
