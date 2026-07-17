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

#include <windows.h>

namespace adobe::usd::ipc {

/**
 * Windows implementation of Process: launches the child with CreateProcess() and waits on its
 * handle. Internal to the ipc library — construct it through ipc::CreateSubprocess(). The
 * PosixPreExecHook is ignored here, as Windows has no fork/exec window to run it in.
 */
class ProcessWin : public Process
{
public:
    ProcessWin();
    ~ProcessWin() override;

    bool Launch(const std::vector<std::string>& commandAndArgs,
                PosixPreExecHook preExecHook = nullptr) override;
    bool Wait(int& exitCode) override;
    bool WaitFor(int timeoutMs, int& exitCode) override;
    void Terminate() override;

private:
    PROCESS_INFORMATION _pi;
};

} // namespace adobe::usd::ipc
