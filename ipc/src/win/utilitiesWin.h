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

#include <windows.h>

#include <sddl.h>
#include <string>

namespace adobe::usd::ipc {

// Windows-only helpers for building the security descriptor applied to the shared-memory mapping,
// so a low-integrity peer process is permitted to open it. Used by SharedMemoryWin.

/**
 * Constructs a dynamic SDDL string that grants read/write access to a specific user SID
 * and enforces a low integrity level (S-1-16-4096).
 *
 * @param userSid The user SID string (e.g., "S-1-5-21-...").
 * @return The constructed SDDL string.
 */
std::string
BuildLowLevelSDDL(const std::string& userSid);

/**
 * Retrieves the SID string for the current process's user.
 *
 * @param userSid Output string set to the SID (e.g., "S-1-5-21-...").
 * @return true if the SID was retrieved successfully.
 */
bool
GetCurrentProcessUserSid(std::string& userSid);

/**
 * Initializes a SECURITY_ATTRIBUTES structure from an SDDL string.
 *
 * @param sddl The SDDL string defining security settings.
 * @param sa Output SECURITY_ATTRIBUTES structure.
 * @return true if initialization succeeded.
 */
bool
CreateSecurityAttributes(const std::string& sddl, SECURITY_ATTRIBUTES& sa);

} // namespace adobe::usd::ipc
