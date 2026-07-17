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

#include "processWin.h"

#include <pxr/base/tf/diagnostic.h>

#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::ipc {

ProcessWin::ProcessWin()
{
    ZeroMemory(&_pi, sizeof(_pi));
}

ProcessWin::~ProcessWin()
{
    if (_pi.hProcess) {
        CloseHandle(_pi.hProcess);
    }
    if (_pi.hThread) {
        CloseHandle(_pi.hThread);
    }
}

bool
ProcessWin::Launch(const std::vector<std::string>& commandAndArgs, PosixPreExecHook /*preExecHook*/)
{
    if (commandAndArgs.empty()) {
        TF_WARN("IPC: Cannot launch process with empty command");
        return false;
    }

    // Build command line string with quoting
    std::ostringstream cmdLineStream;
    for (size_t i = 0; i < commandAndArgs.size(); ++i) {
        if (i > 0)
            cmdLineStream << " ";
        cmdLineStream << "\"" << commandAndArgs[i] << "\"";
    }
    std::string cmdLine = cmdLineStream.str();

    STARTUPINFOA si = {};
    si.cb = sizeof(si);

    ZeroMemory(&_pi, sizeof(_pi));

    BOOL success = CreateProcess(NULL,
                                 const_cast<char*>(cmdLine.c_str()),
                                 NULL,
                                 NULL,
                                 TRUE, // Inherit handles
                                 0,
                                 NULL,
                                 NULL,
                                 &si,
                                 &_pi);

    if (!success) {
        TF_WARN("IPC: CreateProcess failed (error %lu)", GetLastError());
        return false;
    }

    return true;
}

bool
ProcessWin::Wait(int& exitCode)
{
    if (_pi.hProcess == NULL) {
        TF_WARN("IPC: No child process to wait for");
        return false;
    }

    WaitForSingleObject(_pi.hProcess, INFINITE);

    DWORD dwExitCode = 0;
    if (!GetExitCodeProcess(_pi.hProcess, &dwExitCode)) {
        TF_WARN("IPC: GetExitCodeProcess failed (error %lu)", GetLastError());
        return false;
    }

    exitCode = static_cast<int>(dwExitCode);
    return true;
}

bool
ProcessWin::WaitFor(int timeoutMs, int& exitCode)
{
    if (_pi.hProcess == NULL) {
        return false;
    }

    DWORD waitResult = WaitForSingleObject(_pi.hProcess, static_cast<DWORD>(timeoutMs));
    if (waitResult == WAIT_TIMEOUT) {
        return false; // still running
    }
    if (waitResult != WAIT_OBJECT_0) {
        TF_WARN("IPC: WaitForSingleObject failed (error %lu)", GetLastError());
        return false;
    }

    DWORD dwExitCode = 0;
    if (!GetExitCodeProcess(_pi.hProcess, &dwExitCode)) {
        TF_WARN("IPC: GetExitCodeProcess failed (error %lu)", GetLastError());
        return false;
    }
    exitCode = static_cast<int>(dwExitCode);
    return true;
}

void
ProcessWin::Terminate()
{
    if (_pi.hProcess == NULL) {
        return;
    }
    TerminateProcess(_pi.hProcess, 1);
    WaitForSingleObject(_pi.hProcess, INFINITE);
}

} // namespace adobe::usd::ipc
