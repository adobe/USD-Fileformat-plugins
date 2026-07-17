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

#include "processPosix.h"

#include <pxr/base/tf/diagnostic.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::ipc {

// Interpret a reaped child's waitpid status. Returns true on clean exit (sets exitCode to the
// exit status); false if the child was signalled or exited abnormally (sets exitCode to -1).
static bool
interpretWaitStatus(int status, int& exitCode)
{
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
        return true;
    }
    if (WIFSIGNALED(status)) {
        TF_WARN("IPC: Child process terminated by signal %d", WTERMSIG(status));
    }
    exitCode = -1;
    return false;
}

bool
ProcessPosix::Launch(const std::vector<std::string>& commandAndArgs, PosixPreExecHook preExecHook)
{
    if (commandAndArgs.empty()) {
        TF_WARN("IPC: Cannot launch process with empty command");
        return false;
    }

    // Note: after the process calls fork(), the child process must only use async-signal-safe
    // functions until exec() is called. (See https://man7.org/linux/man-pages/man2/fork.2.html
    // and https://man7.org/linux/man-pages/man7/signal-safety.7.html).

    // Build the arguments used to exec the child before forking. std::vector allocation is not
    // async safe, since it may use already-held locks, deadlocking the child. The c_str()
    // pointers stay valid in the child via copy-on-write.
    std::vector<char*> argv;
    argv.reserve(commandAndArgs.size() + 1);
    for (const auto& arg : commandAndArgs) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    _pid = fork();
    if (_pid == -1) {
        TF_WARN("IPC: fork() failed: %s", strerror(errno));
        return false;
    }

    if (_pid == 0) {
        // Child process: between fork() and exec() only async-signal-safe calls are permitted.
        // Diagnostics use a bare write(); argv was built above.
        if (preExecHook && !preExecHook()) {
            const char msg[] = "IPC: pre-exec hook failed in child; not exec'ing\n";
            // "Use" the result (negating it) and then discard it (with void cast) to avoid unused
            // result compiler warning
            (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
            _exit(EXIT_FAILURE);
        }

        // execv: callers must pass an absolute path, since there can be no PATH search to resolve
        // commands. This is because a PATH search (i.e. execvp) can allocate and is not async-
        // signal-safe. The command will always be absolute (on Linux, the executable's resolved
        // path; on macOS, "/usr/bin/sandbox-exec" from hardeningMac).
        execv(argv[0], argv.data());

        // execv only returns on failure.
        const char msg[] = "IPC: exec failed in child; not launching worker\n";
        (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(EXIT_FAILURE);
    }

    // Parent process continues
    return true;
}

bool
ProcessPosix::Wait(int& exitCode)
{
    if (_pid <= 0) {
        TF_WARN("IPC: No child process to wait for");
        return false;
    }

    int status = 0;
    if (waitpid(_pid, &status, 0) == -1) {
        TF_WARN("IPC: waitpid() failed: %s", strerror(errno));
        return false;
    }

    _pid = -1;
    return interpretWaitStatus(status, exitCode);
}

bool
ProcessPosix::WaitFor(int timeoutMs, int& exitCode)
{
    if (_pid <= 0) {
        return false;
    }

    const int kPollIntervalMs = 10;
    for (int64_t elapsed = 0;; elapsed += kPollIntervalMs) {
        int status = 0;
        int waitResult = waitpid(_pid, &status, WNOHANG);
        if (waitResult == _pid) {
            _pid = -1;
            return interpretWaitStatus(status, exitCode);
        }
        if (waitResult == -1) {
            // EINTR: a signal interrupted the poll; retry. Anything else: the child is gone.
            if (errno == EINTR) {
                continue;
            }
            _pid = -1;
            return false;
        }
        if (elapsed >= timeoutMs) {
            return false; // still running
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
}

void
ProcessPosix::Terminate()
{
    if (_pid <= 0) {
        return;
    }
    kill(_pid, SIGKILL);
    waitpid(_pid, nullptr, 0);
    _pid = -1;
}

} // namespace adobe::usd::ipc
