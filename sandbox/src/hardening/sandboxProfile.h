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

#include <sandbox/hardening/hardening.h>

#include <filesystem>
#include <string>

namespace adobe::usd::sandbox {

/**
 * Generate a sandbox profile for a given source asset. The resulting string can be used for the
 * sandbox-exec command on MacOS to allow the sandbox to access a temporary directory, the directory
 * of the source asset, and the libraries needed to run the process.
 *
 * The sandboxLibraryPath variable may be a single path to the directory containing the libraries,
 * or a list of paths separated by ':'. If a list of paths is provided, the paths will be added to
 * the sandbox profile as separate rules. The latter is needed if not using libraries that are all
 * bundled, such as when running the fileformats as a standalone application. In this case, it is
 * recommended to use the environment variable LD_LIBRARY_PATH, concatenated with the current
 * Python path (separated by ':').
 *
 * @param sourceAsset The source asset to generate a sandbox profile for
 * @param sandboxLibraryPath The path to the library/libraries to be used by the sandboxed process
 *                           (may be a single path or a list of paths separated by ':')
 * @param isWritingToPath Whether the asset is being written to a path. If true, the @EXPORT_DIR@
 *                        placeholder will be replaced with the source directory of the asset. If
 *                        false, the @SOURCE_DIR@ placeholder will be replaced with the source
 *                        directory of the asset. This should be false for import and true for
 *                        export.
 *
 * @return A sandbox profile string that can be used to sandbox the process.
 */
std::string
GenerateSandboxProfile(const std::filesystem::path& sourceAsset,
                       const std::string& sandboxLibraryPath,
                       bool isWritingToPath);
}
