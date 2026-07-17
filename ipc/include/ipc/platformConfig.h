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

#if defined(_WIN32) || defined(_WIN64)
#define IPC_PLATFORM_NAME "Windows"
#define IPC_IS_WINDOWS 1
#define IPC_IS_MACOS 0
#define IPC_IS_LINUX 0
#elif defined(__APPLE__) && defined(__MACH__)
#define IPC_PLATFORM_NAME "macOS"
#define IPC_IS_WINDOWS 0
#define IPC_IS_MACOS 1
#define IPC_IS_LINUX 0
#elif defined(__linux__)
#define IPC_PLATFORM_NAME "Linux"
#define IPC_IS_WINDOWS 0
#define IPC_IS_MACOS 0
#define IPC_IS_LINUX 1
#else
#define IPC_PLATFORM_NAME "Unknown"
#define IPC_IS_WINDOWS 0
#define IPC_IS_MACOS 0
#define IPC_IS_LINUX 0
#endif
