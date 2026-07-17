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

#include "sandboxProfile.h"

#include <sandbox/utilities/utilities.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>

#include <fstream>
#include <vector>

using namespace PXR_NS;

namespace adobe::usd::sandbox {

/*
The sandbox profile is a string that is used to sandbox the process. It is a string literal that
is used in a sandbox-exec command on MacOS to allow the sandbox to access a temporary directory,
the directory to read or write the asset, and the libraries needed to run the process.

This string uses unresolved placeholders to be filled in with information from the plug info file,
that may vary based on the build environment. The following placeholders are used for reading in:

@LIBRARY_PATHS@: The library paths required to run the plugins
@SOURCE_DIR@: The source directory where the asset will be read from. On export, this can be blank
@EXPORT_DIR@: The export directory where the asset will be written. On import, this can be blank
@TEMP_DIR@: A temporary dir on MacOS that the sandbox can access for scratch work

Each of these must be replaced by a rule or a list of rules, where each rule is:
  (subpath \\\"PATH\\\")
When copied to the command line, it will be escaped as such:
  (subpath \"PATH\")

The allowed path must be an absolute path, and must be normalized to remove any "..".

LIBRARY_PATHS may be a single path or a list of paths, depending on if all the libraries are in a
single directory, or scattered across multiple directories (likely to occur if being found with
LD_LIBRARY_PATH). If it is multiple paths, each must have its own rule.

Note: since this string is being used in a sandbox-exec command, it must be escaped properly. So
even though it's a string literal, each double quote must be escaped with a backslash.
*/
const std::string sandboxProfileTemplate = R"((version 1)
(deny default)
(debug allow)

; Allow process execution, execvp else error 71 is thrown
(allow process-exec)
(allow sysctl-read)
(allow ipc-posix-shm-read*)
(allow ipc-posix-shm-write*)

(allow file-read*
  (literal \"/\")
)

(allow file-read-metadata
  (subpath \"/Users\")
)

; Allow read and write access to the current working directory
(allow file-read*
@SOURCE_DIR@
@TEMP_DIR@
)

(allow file-write*
@EXPORT_DIR@
@TEMP_DIR@
)

; Allow paths to required libraries
(allow file-read*
@LIBRARY_PATHS@
)

; Allow sandbox to access internationalization (needed by Hoops Exchange)
(allow file-read*
  (subpath \"/usr/share/i18n\")
)

)";

// Internal substitution helper functions

/*
 * Create a sandbox profile path from a string. If the path is relative, it will be resolved
 * relative to the current directory. This will also resolve any ".." that would not work in a
 * sandbox profile. Symlinks are resolved so that rules match what the kernel sees. For example,
 * /var/folders/../path... becomes /private/var/path... on macOS (/var symlinks to /private/var).
 *
 * pathStr: the path to create a sandbox profile path from
 * executableDirectory: the directory that the executable is in
 *
 * Returns a normalized, absolute path that can be used in the sandbox profile
 */
std::filesystem::path
createSandboxProfilePath(const std::string& pathStr,
                         const std::filesystem::path& executableDirectory)
{
    std::filesystem::path path(pathStr);
    if (path.is_relative()) {
        path = executableDirectory / path;
    }
    // Resolve any ".." and symlinks that would not work in a sandbox profile
    std::error_code ec; // Non-throwing overload to not crash host process
    std::filesystem::path canonical =
      std::filesystem::weakly_canonical(path.lexically_normal(), ec);
    if (ec) {
        TF_WARN("(HOST) Failed to canonicalize path \"%s\" for the sandbox profile. "
                "Falling back to lexically_normal: %s",
                path.string().c_str(),
                ec.message().c_str());
        return path.lexically_normal();
    }
    return canonical;
}

/*
 * Convert a path into a rule. A rule is:
 *   (subpath \\\"" + ALLOWED_PATH + "\\\")
 *
 * path: the path to convert into a rule
 * executableDirectory: the directory that the executable is in
 *
 * Returns a rule that can be added to the sandbox profile
 */
std::string
convertPathToRule(const std::filesystem::path& path,
                  const std::filesystem::path& executableDirectory)
{
    // Escape the double quotes. Since the command is executed, the string itself must contain
    // escape characters. For the string to contain \" it must be written as \\ and \"
    // The path is not escaped, because it is in quotes
    std::filesystem::path absoluteNormalizedPath =
      createSandboxProfilePath(path, executableDirectory);
    return "  (subpath \\\"" + absoluteNormalizedPath.string() + "\\\")\n";
}

/*
 * Convert a list of paths into a list of rules. Each rule is:
 *   (subpath \\\"" + ALLOWED_PATH + "\\\")
 *
 * paths: the list of paths to convert into rules
 * executableDirectory: the directory that the executable is in
 *
 * Returns a string of rules that can be added to the sandbox profile
 */
std::string
concatPathsIntoRules(const std::vector<std::string>& paths,
                     const std::filesystem::path& executableDirectory)
{
    std::string result = "";
    for (const auto& path : paths) {
        result += convertPathToRule(path, executableDirectory);
    }
    return result;
}

/*
 * Replace all instances of a placeholder string in the sandbox profile with a new string.
 *
 * sandboxProfile: the sandbox profile string to replace the string in. This string may be
 *                 modified in place!
 * word: the placeholder string to replace
 * replacement: the new string to be added
 */
void
replaceUnresolvedString(std::string& sandboxProfile,
                        const std::string& word,
                        const std::string& replacement)
{
    for (size_t strPos = sandboxProfile.find(word); strPos != std::string::npos;
         strPos = sandboxProfile.find(word, strPos + replacement.length())) {
        sandboxProfile.replace(strPos, word.length(), replacement);
    }
}

// Externally visible utility functions

// TODO: Consider if sandboxLibraryPath should be a vector of strings instead

std::string
GenerateSandboxProfile(const std::filesystem::path& resolvedPath,
                       const std::string& sandboxLibraryPath,
                       bool isWritingToPath)
{
    std::filesystem::path executableDirectory = getExecutableDirectory();

    std::string sandboxProfileString = sandboxProfileTemplate;

    std::string assetDirRules = convertPathToRule(resolvedPath.parent_path(), executableDirectory);
    std::string tempDirRules = convertPathToRule(hardening::GetTempDirMacOS(), executableDirectory);
    if (isWritingToPath) {
        // No source directory is needed to write out the asset. Replace the @EXPORT_DIR@
        // placeholder with the export location of the asset
        replaceUnresolvedString(sandboxProfileString, "@SOURCE_DIR@", "");
        replaceUnresolvedString(sandboxProfileString, "@EXPORT_DIR@", assetDirRules);
    } else {
        // Replace the @SOURCE_DIR@ placeholder with the source directory of the asset. No export
        // directory is needed to read in the asset
        replaceUnresolvedString(sandboxProfileString, "@SOURCE_DIR@", assetDirRules);
        replaceUnresolvedString(sandboxProfileString, "@EXPORT_DIR@", "");
    }

    replaceUnresolvedString(sandboxProfileString, "@TEMP_DIR@", tempDirRules);

    // Replace the @LIBRARY_PATHS@ placeholder with the paths to required libraries
    // Note that the library path may be a list of paths separated by ':', from the environment.
    std::string libraryPathRules =
      concatPathsIntoRules(TfStringTokenize(sandboxLibraryPath, ":"), executableDirectory);
    if (!libraryPathRules.empty()) {
        replaceUnresolvedString(sandboxProfileString, "@LIBRARY_PATHS@", libraryPathRules);
    }

    // FOR DEBUGGING:
    // When running in projects, it sometimes can be a hassle to access logs. This has been added
    // as a temporary debug workaround to view the sandbox profile that has been generated. Set
    // the following string to a local filepath where the sandbox profile will be written, and so
    // it can be easily viewed.
    std::string debugSandboxProfileOutputPath = "";
    if (!debugSandboxProfileOutputPath.empty()) {
        std::ofstream sandboxProfileOutput(debugSandboxProfileOutputPath);
        if (sandboxProfileOutput.is_open()) {
            sandboxProfileOutput << sandboxProfileString;
            sandboxProfileOutput.close();
        } else {
            TF_WARN("Could not open sandbox profile output file at %s",
                    debugSandboxProfileOutputPath.c_str());
        }
    }

    return sandboxProfileString;
}
}

#endif // SANDBOX_IS_MACOS
