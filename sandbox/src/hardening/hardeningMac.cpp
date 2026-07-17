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

#if SANDBOX_IS_MACOS

#include <sandbox/hardening/hardening.h>

#include "sandboxProfile.h"

#include <pxr/base/tf/diagnostic.h>

#include <filesystem>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sandbox::hardening {

/* Build the macOS sandbox-exec command prefix from args. Canonicalizes the asset path
 * to resolve symlinks, then generates the sandbox profile string. */
LaunchHardening
BuildLaunchHardening(const LaunchHardeningArgs& args)
{
    // Canonicalize so the sandboxed path contains no symlinks. Each symlink
    // the kernel resolves during path traversal needs its own file-read-metadata
    // allow in the profile. For example, /var/folders/X/file would otherwise
    // require metadata-read on /var (/var -> /private/var on macOS).
    std::error_code ec; // Non-throwing overload to not crash host process
    std::filesystem::path canonical =
      std::filesystem::weakly_canonical(std::filesystem::path(args.resolvedPath), ec);
    std::string canonicalPath;
    if (ec) {
        TF_WARN("(HOST) Failed to canonicalize path \"%s\" for executing the sandboxed process. "
                "Falling back to original path: %s",
                args.resolvedPath.c_str(),
                ec.message().c_str());
        canonicalPath = args.resolvedPath;
    } else {
        canonicalPath = canonical.string();
    }

    std::string profile = adobe::usd::sandbox::GenerateSandboxProfile(
      std::filesystem::path(canonicalPath), args.sandboxAccessiblePaths, args.isExport);
    // Absolute path (not the bare "sandbox-exec") so the launcher can exec it without a PATH
    // search, for async signal safety: see ProcessPosix::Launch. sandbox-exec ships at /usr/bin
    // on every macOS.
    std::string commandPrefix = "/usr/bin/sandbox-exec -p \"" + profile + "\"";

    return { commandPrefix, nullptr };
}

// macOS no-op: process restrictions are applied at launch time via sandbox-exec.
bool
ApplyProcessRestrictions(bool /*isExport*/)
{
    return true; // macOS: no-op (hardening applied at launch via sandbox-exec)
}

// Return the shared-memory name prefix for macOS: a path under /private/tmp.
std::string
GetShmNamePrefix()
{
    return (GetTempDirMacOS() / "UsdSHM_").string();
}

} // namespace adobe::usd::sandbox::hardening

#endif // SANDBOX_IS_MACOS
