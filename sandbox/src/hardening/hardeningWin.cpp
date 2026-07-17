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

#if SANDBOX_IS_WINDOWS

#include <sandbox/hardening/hardening.h>

#include <pxr/base/tf/diagnostic.h>

// windows.h must precede sddl.h: sddl.h's declarations (e.g. ConvertStringSidToSid) are gated
// behind SDK-version macros that windows.h establishes first. The blank line keeps them in
// separate include blocks so clang-format does not reorder them alphabetically.
#include <windows.h>

#include <sddl.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sandbox::hardening {

/* Windows launch hardening is a no-op: restrictions are applied worker-side via
 * ApplyProcessRestrictions(), not at launch time. */
LaunchHardening
BuildLaunchHardening(const LaunchHardeningArgs& /*args*/)
{
    return {};
}

// Return the shared-memory name prefix for Windows: a Local namespace object name.
std::string
GetShmNamePrefix()
{
    return "Local\\UsdSharedMemory_";
}

/* Apply Windows worker-side restrictions. Import: lower the process integrity to Low.
 * Export: currently a no-op (sandboxed export on Windows is not yet implemented). */
bool
ApplyProcessRestrictions(bool isExport)
{
    if (isExport) {
        // TODO: Add support for export with the sandbox. This can be done by writing to a temp
        // output folder that low permissions allows, and then having the host process copy all
        // files to the target location
        TF_WARN("Export is not currently sandboxed on Windows!\n");
        return true;
    }

    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &hToken)) {
        TF_RUNTIME_ERROR("OpenProcessToken failed in ApplyProcessRestrictions. Error: %lu",
                         GetLastError());
        return false;
    }

    // Convert string SID to PSID (Low Integrity SID: S-1-16-4096)
    PSID pLowIntegritySid = NULL;
    if (!ConvertStringSidToSid("S-1-16-4096", &pLowIntegritySid)) {
        TF_RUNTIME_ERROR("ConvertStringSidToSid failed in ApplyProcessRestrictions. Error: %lu",
                         GetLastError());
        CloseHandle(hToken);
        return false;
    }

    TOKEN_MANDATORY_LABEL TIL = { 0 };
    TIL.Label.Attributes = SE_GROUP_INTEGRITY;
    TIL.Label.Sid = pLowIntegritySid;

    if (!SetTokenInformation(hToken,
                             TokenIntegrityLevel,
                             &TIL,
                             sizeof(TOKEN_MANDATORY_LABEL) + GetLengthSid(pLowIntegritySid))) {
        TF_RUNTIME_ERROR("SetTokenInformation failed in ApplyProcessRestrictions. Error: %lu",
                         GetLastError());
        LocalFree(pLowIntegritySid);
        CloseHandle(hToken);
        return false;
    }

    // Clean up
    LocalFree(pLowIntegritySid);
    CloseHandle(hToken);
    return true;
}

} // namespace adobe::usd::sandbox::hardening

#endif // SANDBOX_IS_WINDOWS
