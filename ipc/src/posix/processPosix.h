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

#include <ipc/process.h>

#include <sys/types.h>

namespace adobe::usd::ipc {

/**
 * POSIX implementation of Process: launches the child with fork() + execvp() and reaps it with
 * waitpid(). Internal to the ipc library — construct it through ipc::CreateSubprocess(), which
 * hands back a Process pointer; callers never name this type directly.
 */
class ProcessPosix : public Process
{
public:
    ProcessPosix() = default;
    ~ProcessPosix() override = default;

    bool Launch(const std::vector<std::string>& commandAndArgs,
                PosixPreExecHook preExecHook = nullptr) override;
    bool Wait(int& exitCode) override;
    bool WaitFor(int timeoutMs, int& exitCode) override;
    void Terminate() override;

private:
    pid_t _pid = -1;
};

} // namespace adobe::usd::ipc
