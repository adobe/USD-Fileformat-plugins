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

#include <ipc/platformConfig.h>
#include <ipc/process.h>
#include <ipc/sharedMemory.h>

#if IPC_IS_WINDOWS
#include "win/processWin.h"
#include "win/sharedMemoryWin.h"
#elif IPC_IS_MACOS || IPC_IS_LINUX
#include "posix/processPosix.h"
#include "posix/sharedMemoryPosix.h"
#endif

namespace adobe::usd::ipc {

std::unique_ptr<SharedMemory>
CreateSharedMemory()
{
#if IPC_IS_WINDOWS
    return std::make_unique<SharedMemoryWin>();
#elif IPC_IS_MACOS || IPC_IS_LINUX
    return std::make_unique<SharedMemoryPosix>();
#else
    return nullptr;
#endif
}

std::unique_ptr<Process>
CreateSubprocess()
{
#if IPC_IS_WINDOWS
    return std::make_unique<ProcessWin>();
#elif IPC_IS_MACOS || IPC_IS_LINUX
    return std::make_unique<ProcessPosix>();
#else
    return nullptr;
#endif
}

} // namespace adobe::usd::ipc
