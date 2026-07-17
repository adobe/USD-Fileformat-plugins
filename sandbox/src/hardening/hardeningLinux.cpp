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

#include <sandbox/platformConfig.h>

#if SANDBOX_IS_LINUX

#include <sandbox/hardening/hardening.h>

#include <pxr/base/tf/diagnostic.h>

// Linux headers needed for namespace hardening
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <seccomp.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sandbox::hardening {

// Async-signal-safe calls are required (see
// https://man7.org/linux/man-pages/man7/signal-safety.7.html). After fork() in a multithreaded
// process, a lock held by another thread at fork() (malloc's arena, the stdio stream lock, locale
// state, etc...) stays locked forever in the child, so only async-signal-safe calls are safe until
// exec(). For that reason, all functions called by the preExecHook must be async-signal-safe.

// async-signal-safe:
// Writes msg to stderr with a single write(). fprintf/strerror can deadlock, while write() is a
// bare syscall with no such lock. Messages are fixed strings naming the failing step; errno
// detail is omitted (formatting it safely is not worth it for a should-never-happen failure, and
// the parent sees the hook fail anyway).
static void
writeStderrRaw(const char* msg)
{
    size_t len = 0;
    while (msg[len] != '\0') {
        ++len;
    }
    (void)!write(STDERR_FILENO, msg, len);
}

// Signal handler, run when the child process attempts to execute a syscall forbidden by seccomp
static void
sigsys_handler(int /*signo*/, siginfo_t* /*info*/, void* /*context*/)
{
    // Signal handler: only async-signal-safe calls here (no Logger / TF_* / buffered stdio).
    writeStderrRaw("hardening: a syscall blocked by the seccomp policy (SIGSYS); terminating.\n");
    _exit(1);
}

// async-signal-safe:
// Writes `len` bytes of `data` to the /proc file at `path` (open/write/close). Returns false only
// if the file cannot be OPENED (fatal for the caller); a failed WRITE is logged but tolerated
// (returns true).
//
// Callers should use this for the child's user-namespace ID-mapping files (/proc/self/setgroups,
// uid_map, gid_map), whose writes some containers / CI deny with EPERM for unprivileged user
// namespaces. Tolerating that keeps the sandbox usable there: the process then runs with no ID
// mapping (the unmapped "overflow" user), still confined by seccomp and the mount/network
// namespaces.
static bool
writeProcFile(const char* path, const char* data, size_t len)
{
    int fd = open(path, O_WRONLY);
    if (fd == -1) {
        writeStderrRaw("sandbox: post-fork hardening failed to open a /proc mapping file; "
                       "sandboxed process will not start\n");
        return false;
    }
    if (write(fd, data, len) != static_cast<ssize_t>(len)) {
        writeStderrRaw("sandbox: post-fork hardening could not write a /proc id-map file; "
                       "continuing without user/group ID mapping\n");
    }
    close(fd);
    return true;
}

// async-signal-safe:
// Format a string as expected for a /proc uid_map / gid_map line as such:
// "0 <id> 1\n"
//
// Writes into a given buffer (which must hold at least 32 bytes) and returns the number of bytes
// written. The decimal encoding is manually written here because snprintf is not
// async-signal-safe; a fixed 32-byte buffer always fits "0 " + a 64-bit decimal (<= 20 digits) +
// " 1\n", so there is no truncation to check.
static size_t
formatIdMapLine(char* buf, unsigned long id)
{
    size_t pos = 0;
    buf[pos++] = '0';
    buf[pos++] = ' ';

    char digits[20]; // fits any 64-bit unsigned value
    size_t n = 0;
    do {
        digits[n++] = static_cast<char>('0' + (id % 10));
        id /= 10;
    } while (id != 0);
    while (n > 0) {
        buf[pos++] = digits[--n];
    }

    buf[pos++] = ' ';
    buf[pos++] = '1';
    buf[pos++] = '\n';
    return pos;
}

LaunchHardening
BuildLaunchHardening(const LaunchHardeningArgs& /*args*/)
{
    LaunchHardening result;
    result.commandPrefix = "";

    // The pre-exec hook runs in the forked child after fork() but before exec(). Only
    // async-signal-safe calls are permitted here. open/close/write/getuid/getgid/_exit are on the
    // POSIX list; unshare/prctl are Linux syscall wrappers that take no userspace lock; id
    // formatting is done here (formatIdMapLine) to avoid snprintf, which is not on the list
    result.preExecHook = []() -> bool {
        // Enter a new user, mount, and network namespace.
        if (unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWNET) == -1) {
            writeStderrRaw("sandbox: post-fork hardening failed to unshare namespaces; "
                           "sandboxed process will not start\n");
            return false;
        }

        // Disable setgroups before writing gid_map (required by the kernel when running
        // unprivileged user namespaces).
        if (!writeProcFile("/proc/self/setgroups", "deny", 4)) {
            return false;
        }

        // Map user/group IDs: 0 inside == real UID/GID outside, formatted into a stack buffer with
        // an async-signal-safe encoder
        char idMap[32];
        size_t len = formatIdMapLine(idMap, static_cast<unsigned long>(getuid()));
        if (!writeProcFile("/proc/self/uid_map", idMap, len)) {
            return false;
        }

        len = formatIdMapLine(idMap, static_cast<unsigned long>(getgid()));
        if (!writeProcFile("/proc/self/gid_map", idMap, len)) {
            return false;
        }

        // Prevent the child from ever regaining privileges.
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
            writeStderrRaw("sandbox: post-fork hardening failed to set no-new-privileges; "
                           "sandboxed process will not start\n");
            return false;
        }

        return true;
    };

    return result;
}

std::string
GetShmNamePrefix()
{
    return "/UsdSHM_";
}

bool
ApplyProcessRestrictions(bool /*isExport*/)
{
    // Install a SIGSYS handler so blocked syscalls are logged before the process exits.
    struct sigaction act = {};
    act.sa_sigaction = sigsys_handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSYS, &act, nullptr);

    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (!ctx) {
        TF_RUNTIME_ERROR("hardening: Failed to initialize seccomp context: %s", strerror(errno));
        return false;
    }

    // Block critical system calls. seccomp_rule_add returns 0 on success or a negative errno if a
    // rule cannot be added (e.g. an action or syscall unsupported by this kernel/libseccomp).
    // Accumulate every result and fail below if any rule was dropped
    int result = 0;
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(execve), 0);
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(fork), 0);
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(vfork), 0);
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(ptrace), 0);
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(kill), 0);   // and tkill, tgkill
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(setuid), 0); // and setgid, etc.
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(capset), 0); // and capget
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(mount), 0);  // and umount, pivot_root
    result |= seccomp_rule_add(
      ctx, SCMP_ACT_KILL, SCMP_SYS(init_module), 0); // and delete_module, finit_module
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(ioperm), 0); // and iopl
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(reboot), 0);
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL, SCMP_SYS(open_by_handle_at), 0);
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(socket), 0);
    result |= seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(connect), 0);

    if (result != 0) {
        TF_RUNTIME_ERROR("hardening: Failed to add one or more seccomp rules; refusing to run "
                         "without a complete syscall filter");
        seccomp_release(ctx);
        return false;
    }

    if (seccomp_load(ctx) < 0) {
        TF_RUNTIME_ERROR("hardening: Failed to load seccomp rules: %s", strerror(errno));
        seccomp_release(ctx);
        return false;
    }

    seccomp_release(ctx); // Cleanup
    return true;
}

} // namespace adobe::usd::sandbox::hardening

#endif // SANDBOX_IS_LINUX
