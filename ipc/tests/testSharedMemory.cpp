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

#include <ipc/platformConfig.h>
#include <ipc/sharedMemory.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

using namespace adobe::usd::ipc;

namespace {
// Build a platform-appropriate shared-memory name from a base label. POSIX (macOS + Linux) uses a
// leading '/' for shm_open; Windows uses a "Local\" named-object prefix.
std::string
shmName(const std::string& base)
{
#if IPC_IS_WINDOWS
    return "Local\\" + base;
#else
    return "/" + base;
#endif
}
} // namespace

TEST(SharedMemoryTests, CreateAndWrite)
{
    auto shm = CreateSharedMemory();
    ASSERT_TRUE(shm != nullptr);

    std::string name = shmName("TestSHM_CreateAndWrite");

    ASSERT_TRUE(shm->Create(name, 1024));
    EXPECT_EQ(shm->GetSize(), 1024u);
    EXPECT_NE(shm->GetBuffer(), nullptr);

    const char* data = "Hello, shared memory!";
    size_t len = strlen(data) + 1;
    ASSERT_TRUE(shm->Write(data, len, 0));

    char buffer[64] = {};
    ASSERT_TRUE(shm->Read(0, buffer, len));
    EXPECT_STREQ(buffer, data);

    shm->Clean();
}

TEST(SharedMemoryTests, ReadToStream)
{
    auto shm = CreateSharedMemory();

    std::string name = shmName("TestSHM_ReadToStream");

    ASSERT_TRUE(shm->Create(name, 256));

    const char* message = "stream test";
    size_t len = strlen(message);
    ASSERT_TRUE(shm->Write(message, len, 0));

    std::ostringstream oss;
    ASSERT_TRUE(shm->Read(0, oss, len));
    EXPECT_EQ(oss.str(), "stream test");

    shm->Clean();
}

TEST(SharedMemoryTests, WriteAtOffset)
{
    auto shm = CreateSharedMemory();

    std::string name = shmName("TestSHM_WriteAtOffset");

    ASSERT_TRUE(shm->Create(name, 256));

    const char* part1 = "AAAA";
    const char* part2 = "BBBB";
    ASSERT_TRUE(shm->Write(part1, 4, 0));
    ASSERT_TRUE(shm->Write(part2, 4, 4));

    char buffer[8] = {};
    ASSERT_TRUE(shm->Read(0, buffer, 8));
    EXPECT_EQ(memcmp(buffer, "AAAABBBB", 8), 0);

    shm->Clean();
}

#if IPC_IS_WINDOWS
// A size above 4 GiB must map a region of the full 64-bit size. Writing and reading a payload at
// the 4 GiB boundary only succeeds when CreateFileMapping's high DWORD is honored, so this
// exercises the high/low split directly.
TEST(SharedMemoryTests, CreateMapsFullSizeAboveFourGiB)
{
    auto shm = CreateSharedMemory();
    ASSERT_TRUE(shm != nullptr);

    std::string name = shmName("TestSHM_LargeMapping");

    // 4 GiB + one page, so the mapping size needs a nonzero high DWORD. Accessing at the 4 GiB
    // boundary then confirms the full size was mapped.
    const uint64_t fourGiB = static_cast<uint64_t>(1) << 32;
    const size_t size = static_cast<size_t>(fourGiB) + 4096;

    ASSERT_TRUE(shm->Create(name, size));
    EXPECT_EQ(shm->GetSize(), size);

    const char payload[] = "past-4GiB";
    const size_t len = sizeof(payload);
    ASSERT_TRUE(shm->Write(payload, len, static_cast<size_t>(fourGiB)));

    char buffer[sizeof(payload)] = {};
    ASSERT_TRUE(shm->Read(static_cast<size_t>(fourGiB), buffer, len));
    EXPECT_STREQ(buffer, payload);

    shm->Clean();
}
#endif

TEST(SharedMemoryTests, BoundsCheck)
{
    auto shm = CreateSharedMemory();

    std::string name = shmName("TestSHM_BoundsCheck");

    ASSERT_TRUE(shm->Create(name, 16));

    char buffer[32] = {};
    // Reading past end should fail
    EXPECT_FALSE(shm->Read(0, buffer, 32));
    // Writing past end should fail
    EXPECT_FALSE(shm->Write(buffer, 32, 0));
    // Offset + size overflow
    EXPECT_FALSE(shm->Read(10, buffer, 10));
    EXPECT_FALSE(shm->Write(buffer, 10, 10));

    shm->Clean();
}
