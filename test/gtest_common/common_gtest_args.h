/*
Copyright 2026 Adobe. All rights reserved.
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
#include <string>
#include <vector>

// default when run directly from the command line and parameters aren't passed in
#ifdef TEST_ASSETS_DIR
inline std::string assetDir = TEST_ASSETS_DIR;
#else
inline std::string assetDir = "./";
#endif

#ifdef TEST_EXEC_DIR
inline std::string exeDir = TEST_EXEC_DIR;
#else
inline std::string exeDir = "./";
#endif

inline std::string
getOption(const std::vector<std::string>& args, const std::string& option_name)
{
    for (auto it = args.begin(), end = args.end(); it != end; ++it) {
        if (*it == option_name)
            if (it + 1 != end)
                return *(it + 1);
    }

    return "";
}

inline void
parseArgs(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);

    std::string exeDirOption = getOption(args, "-e");
    if (exeDirOption != "") {
        exeDir = exeDirOption;
    }

    std::string assetDirOption = getOption(args, "-a");
    if (assetDirOption != "") {
        std::filesystem::path assetDirPath = assetDirOption;
        std::filesystem::path normalizedAssetDir = assetDirPath.lexically_normal();
        assetDir = normalizedAssetDir.generic_string();
    }
}

#define TEST_CMAKE_ONLY(a, b) TEST_F(a, b)
