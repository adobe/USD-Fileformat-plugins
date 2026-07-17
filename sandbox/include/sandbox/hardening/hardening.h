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
#include <filesystem>
#include <ipc/process.h>
#include <sandbox/api.h>
#include <sandbox/platformConfig.h>
#include <string>

namespace adobe::usd::sandbox::hardening {

#if SANDBOX_IS_MACOS
// Temp directory the macOS sandbox profile grants access to (read + write). Lives in this public
// header so host-side code (e.g. ScopedPluginPathOverride) can reach it without pulling in the
// src-level sandboxProfile.h.
inline std::filesystem::path
GetTempDirMacOS()
{
    return std::filesystem::path("/private/tmp");
}
#endif // SANDBOX_IS_MACOS

// Inputs to build launch-time hardening. All three fields are consumed only by macOS sandbox
// profile generation; Linux and Windows ignore the entire struct (their BuildLaunchHardening
// takes no launch-time inputs).
struct USDSANDBOX_API LaunchHardeningArgs
{
    std::string resolvedPath;           // asset path being converted
    std::string sandboxAccessiblePaths; // colon-separated lib paths (macOS)
    bool isExport = false;
};

// Data the host needs to be able to launch and sandbox the child process.
struct USDSANDBOX_API LaunchHardening
{
    std::string commandPrefix;                  // macOS: sandbox-exec -p "<profile>"; else ""
    ipc::Process::PosixPreExecHook preExecHook; // Linux: namespaces hook; else nullptr
};

// HOST: produce the platform-appropriate hardening for launching the child process
USDSANDBOX_API LaunchHardening
BuildLaunchHardening(const LaunchHardeningArgs& args);

// HOST: get the platform-appropriate shared-memory name prefix. The host appends a
// per-process disambiguator
USDSANDBOX_API std::string
GetShmNamePrefix();

// For child process: apply in-process restrictions at startup, after plugin registration but
// before any untrusted parsing.
// Linux: seccomp restrictions on dangerous calls
// Windows: integrity lowering
// macOS: no-op (hardening already applied at launch via sandbox-exec).
// Returns false on failure.
USDSANDBOX_API bool
ApplyProcessRestrictions(bool isExport);

} // namespace adobe::usd::sandbox::hardening
