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
#include <ipc/process.h>

#include <gtest/gtest.h>

#include <chrono>

using namespace adobe::usd::ipc;

namespace {
#if IPC_IS_WINDOWS
std::vector<std::string>
quickExitCommand()
{
    // A child that exits 0: `ping -n 1` to loopback sends one echo, succeeds, and exits 0.
    return { "ping", "-n", "1", "127.0.0.1" };
}
std::vector<std::string>
longSleepCommand()
{
    // Windows equivalent of the POSIX `sleep`: Windows has no plain sleep command, and `timeout`
    // needs a console (it exits immediately under CI's redirected stdin), so `ping` to loopback
    // provides the delay (-n 31 ≈ 30s).
    return { "ping", "-n", "31", "127.0.0.1" };
}
#else
std::vector<std::string>
quickExitCommand()
{
    return { "/bin/sh", "-c", "exit 0" };
}
std::vector<std::string>
longSleepCommand()
{
    return { "/bin/sleep", "30" };
}
#endif
} // namespace

TEST(ProcessTests, WaitReturnsExitCode)
{
    auto proc = CreateSubprocess();
    ASSERT_NE(proc, nullptr);
    ASSERT_TRUE(proc->Launch(quickExitCommand()));
    int exitCode = -1;
    EXPECT_TRUE(proc->Wait(exitCode));
    EXPECT_EQ(exitCode, 0);
}

#if !IPC_IS_WINDOWS
// The pre-exec hook is POSIX-only (runs in the child between fork and exec). A hook that returns
// false must abort the child before exec, so the target command never runs: Launch still succeeds
// (fork worked), but the child exits non-zero instead of running quickExitCommand()'s `exit 0`.
TEST(ProcessTests, PreExecHookFailureAbortsChildBeforeExec)
{
    auto proc = CreateSubprocess();
    ASSERT_NE(proc, nullptr);

    auto refusingHook = []() -> bool { return false; };
    ASSERT_TRUE(proc->Launch(quickExitCommand(), refusingHook));

    int exitCode = 0;
    EXPECT_TRUE(proc->Wait(exitCode)); // child was reaped
    EXPECT_NE(exitCode, 0);            // hook refusal aborted the child; the command's 0 never ran
}
#endif

TEST(ProcessTests, WaitForReturnsTrueWhenChildExits)
{
    auto proc = CreateSubprocess();
    ASSERT_NE(proc, nullptr);
    ASSERT_TRUE(proc->Launch(quickExitCommand()));
    int exitCode = -1;
    EXPECT_TRUE(proc->WaitFor(5000, exitCode)); // generous; child exits immediately
    EXPECT_EQ(exitCode, 0);
}

TEST(ProcessTests, TerminateLongRunningChild)
{
    auto proc = CreateSubprocess();
    ASSERT_NE(proc, nullptr);
    ASSERT_TRUE(proc->Launch(longSleepCommand()));

    int exitCode = -1;
    auto start = std::chrono::steady_clock::now();
    ASSERT_FALSE(proc->WaitFor(100, exitCode)); // still running after 100ms

    // Terminate must return promptly (not hang for the full 30s sleep).
    proc->Terminate();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    EXPECT_LT(elapsedMs, 10000) << "Terminate did not reap the worker promptly";
}

TEST(ProcessTests, LifecycleCallsAreSafeWithNoChild)
{
    auto proc = CreateSubprocess();
    ASSERT_NE(proc, nullptr);
    int exitCode = -1;
    EXPECT_FALSE(proc->WaitFor(0, exitCode)); // no child launched
    proc->Terminate();                        // no-op, must not crash
}
