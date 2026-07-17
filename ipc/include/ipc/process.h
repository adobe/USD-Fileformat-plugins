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

#include <ipc/api.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace adobe::usd::ipc {

/**
 * Abstract interface for launching and waiting on a child process. Platform-specific
 * implementations handle the differences between fork/exec (POSIX) and CreateProcess (Windows).
 */
class IPC_API Process
{
public:
    /**
     * Optional callback run in the child process after fork() but before exec() — POSIX only
     * (hence the name). This is the window POSIX gives you to harden or reconfigure the child
     * before the target binary starts: enter namespaces, install a seccomp filter, drop
     * privileges, etc. Return true to proceed to exec(); return false to abort, in which case
     * the implementation must terminate the child (_exit) rather than unwind back into the
     * parent.
     *
     * Windows has no fork/exec model — CreateProcess launches the target binary directly — so
     * there is no equivalent in-child window, and implementations ignore this callback there.
     * Callers needing the same effect on Windows use another mechanism (e.g. a restricted token
     * or job object at creation time, or the child reconfiguring itself at the start of main()).
     */
    using PosixPreExecHook = std::function<bool()>;

    virtual ~Process() = default;

    /**
     * Launch a child process.
     *
     * @param commandAndArgs The executable path followed by its arguments.
     *                       commandAndArgs[0] is the executable.
     * @param preExecHook Optional POSIX-only hook (see PosixPreExecHook) run in the child after
     *                    fork, before exec. Ignored on Windows.
     * @return true if the process was launched successfully.
     */
    virtual bool Launch(const std::vector<std::string>& commandAndArgs,
                        PosixPreExecHook preExecHook = nullptr) = 0;

    /**
     * Block until the child process has exited.
     *
     * @param exitCode Output parameter set to the child's exit code.
     * @return true if the wait succeeded and the exit code was retrieved.
     */
    virtual bool Wait(int& exitCode) = 0;

    /**
     * Wait up to timeoutMs for the child to exit.
     *
     * @param timeoutMs Maximum time to wait, in milliseconds. 0 polls once (non-blocking).
     * @param exitCode  Set to the child's exit code if it exited within the timeout.
     * @return true if the child exited within the timeout (exitCode is valid); false if it is
     *         still running after timeoutMs, on error, or if there is no child to wait for.
     */
    virtual bool WaitFor(int timeoutMs, int& exitCode) = 0;

    /**
     * Forcibly terminate the child (SIGKILL / TerminateProcess) and reap it. A last resort for a
     * worker that will not exit on its own. No-op if no child was launched or it was already
     * reaped, so it is safe to call unconditionally during cleanup.
     */
    virtual void Terminate() = 0;
};

/// Factory function that creates a platform-appropriate Process implementation.
IPC_API std::unique_ptr<Process>
CreateSubprocess();

} // namespace adobe::usd::ipc
