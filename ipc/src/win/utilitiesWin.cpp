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

#include "utilitiesWin.h"

#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::ipc {

std::string
BuildLowLevelSDDL(const std::string& userSid)
{
    return "D:(A;;GRGW;;;" + userSid + ")S:(ML;;NW;;;S-1-16-4096)";
}

bool
GetCurrentProcessUserSid(std::string& userSid)
{
    HANDLE tokenHandle = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tokenHandle)) {
        TF_WARN("IPC: OpenProcessToken failed (error %lu)", GetLastError());
        return false;
    }

    DWORD tokenInfoLength = 0;
    GetTokenInformation(tokenHandle, TokenUser, nullptr, 0, &tokenInfoLength);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        TF_WARN("IPC: GetTokenInformation size query failed (error %lu)", GetLastError());
        CloseHandle(tokenHandle);
        return false;
    }

    PTOKEN_USER tokenUser = reinterpret_cast<PTOKEN_USER>(malloc(tokenInfoLength));
    if (!tokenUser) {
        TF_WARN("IPC: Memory allocation failed for token user info");
        CloseHandle(tokenHandle);
        return false;
    }

    if (!GetTokenInformation(
          tokenHandle, TokenUser, tokenUser, tokenInfoLength, &tokenInfoLength)) {
        TF_WARN("IPC: GetTokenInformation failed (error %lu)", GetLastError());
        free(tokenUser);
        CloseHandle(tokenHandle);
        return false;
    }

    LPSTR sidCString = nullptr;
    if (!ConvertSidToStringSidA(tokenUser->User.Sid, &sidCString)) {
        TF_WARN("IPC: ConvertSidToStringSid failed (error %lu)", GetLastError());
        free(tokenUser);
        CloseHandle(tokenHandle);
        return false;
    }

    userSid = sidCString;
    LocalFree(sidCString);
    free(tokenUser);
    CloseHandle(tokenHandle);
    return true;
}

bool
CreateSecurityAttributes(const std::string& sddl, SECURITY_ATTRIBUTES& sa)
{
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
          sddl.c_str(), SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL)) {
        TF_WARN("IPC: Failed to create security descriptor from SDDL (error %lu)", GetLastError());
        return false;
    }
    return true;
}

} // namespace adobe::usd::ipc
