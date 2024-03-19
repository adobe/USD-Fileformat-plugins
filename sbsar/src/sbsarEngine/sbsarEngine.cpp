/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include <pxr/base/arch/library.h>
#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/pxr.h>
#include <sbsarDebug.h>
#include <sbsarEngine/sbsarEngine.h>
#include <string>
#include <substance/framework/framework.h>

#ifdef _WIN32
// Windows engine selection code

#    include <codecvt>
#    include <windows.h>
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#else // _WIN32
// Linux and mac
#    include <dlfcn.h>
#endif // _WIN32

#ifdef _WIN32
const char* DYLIB_PREFIX = "substance_";
const char* DYLIB_SUFFIX = ".dll";
#elif defined(__APPLE__)
const char* DYLIB_PREFIX = "libsubstance_";
const char* DYLIB_SUFFIX = ".dylib";
#else
const char* DYLIB_PREFIX = "libsubstance_";
const char* DYLIB_SUFFIX = ".so";
#endif

PXR_NAMESPACE_USING_DIRECTIVE
// TODO: Is there some kind of standard library version of this
// it seems hacky
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

namespace {
// Borrowed from
// https://git.corp.adobe.com/substance-integrations/integrations-common

const char* const engineCreateContextSymbol = "substanceContextInitImpl";
const char* const engineReleaseContextSymbol = "substanceContextRelease";
typedef void (*engineVersionFunction)(SubstanceVersion* version, unsigned int apiVersion);
typedef unsigned int (*engineReleaseContextFunction)(SubstanceContext* context);
typedef unsigned int (*engineCreateContextFunction)(SubstanceContext** context,
                                                    SubstanceDevice* device,
                                                    unsigned int apiVersion,
                                                    SubstanceEngineIDEnum apiPlatform);

/** @brief Determine whether an engine binary can successfully be initialized
   @param module Loaded dynamic library
   @return True if the engine can be initialized, false otherwise
*/
bool
algIntegrationsRendererIsEngineValid(void* module)
{

    if (module == nullptr)
        return false;
    /* Acquire the context symbols */
    engineCreateContextFunction createSymbol =
      (engineCreateContextFunction)ArchLibraryGetSymbolAddress(module, engineCreateContextSymbol);
    engineReleaseContextFunction releaseSymbol =
      (engineReleaseContextFunction)ArchLibraryGetSymbolAddress(module, engineReleaseContextSymbol);
    if (createSymbol == nullptr || releaseSymbol == nullptr)
        return false;

    SubstanceContext* context = nullptr;
    SubstanceDevice device;
    /* Attempt to initialize context */

    const unsigned int initResult =
      createSymbol(&context, &device, SUBSTANCE_API_VERSION, SUBSTANCE_API_PLATFORM);
    bool result = false;
    if (initResult == Substance_Error_None) {
        /* Shut down context if successful and set return to true */
        result = (releaseSymbol(context) == Substance_Error_None);

        context = nullptr;
    }

    return result;
}
} // namespace

namespace {
#ifdef _WIN32
static std::string
getCurrentDllPath()
{
    // Gets the path to the current dll so we can locate files relative to it
    char dllPath[_MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, dllPath, _MAX_PATH);
    return dllPath;
}
static std::string
getDirectoryFromFile(const std::string& file_path)
{
    size_t last_slash = file_path.find_last_of("\\/");
    if (last_slash == std::string::npos) {
        // No directory present
        return "";
    } else {
        // +1 to include the slash
        return file_path.substr(0, last_slash + 1);
    }
}
#endif // _WIN32

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

std::vector<std::string>
splitString(const std::string& str, const char delimiter = ';')
{
    std::istringstream ss(str);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

static void*
getEngineDll(const std::string& searchName)
{
    static void* g_engineDLL = nullptr;
    if (g_engineDLL == nullptr) {
        // TODO: Mutex around this to avoid race condition
        // and then, anywhere you need it:
        std::vector<std::string> engineNames = splitString(TOSTRING(USDSBSAR_SUBSTANCE_ENGINES));

        std::vector<std::string> engineRoots;
        engineRoots.reserve(engineNames.size());

        for (const auto& engineName : engineNames) {
            if (!searchName.empty() && engineName.find(searchName) != std::string::npos) {
                TF_DEBUG(SBSAR_RENDER)
                  .Msg("SbsarEngine: Specific engine name is found for %s\n", engineName.c_str());
                engineRoots.clear();
                engineRoots.push_back(DYLIB_PREFIX + engineName + DYLIB_SUFFIX);
                break;
            }
            engineRoots.push_back(DYLIB_PREFIX + engineName + DYLIB_SUFFIX);
            TF_DEBUG(SBSAR_RENDER)
              .Msg("SbsarEngine: Looking for engine: %s\n", engineRoots.back().c_str());
        }

        std::vector<std::string> searchPaths;
#ifdef _WIN32
        // Add the plugin dll directory on windows for searching for dll's
        std::string dllPath = getCurrentDllPath();
        std::string dllDir = getDirectoryFromFile(dllPath);
        searchPaths.push_back(dllDir);
#else
        // We assume the executable is in a bin directory and that the sibling
        // lib directory contains the dynamic libraries with the engines we're
        // looking for.
        std::string exePath = ArchGetExecutablePath();
        std::string exeDirPath = TfGetPathName(exePath);
        std::string pluginDir = TfAbsPath(exeDirPath + "../lib");
        searchPaths.push_back(pluginDir + "/");
#endif // _WIN32

        // Add an empty path (for using global paths)
        searchPaths.push_back("");

        // Search for engines in the priority order
        for (const std::string& engineRoot : engineRoots) {
            // Search in engine locations
            for (const std::string& searchPath : searchPaths) {
                std::string dllFullPath = searchPath + engineRoot;
                TF_DEBUG(SBSAR_RENDER)
                  .Msg("SbsarEngine: Trying to load engine: %s\n", dllFullPath.c_str());
                g_engineDLL = ArchLibraryOpen(dllFullPath.c_str(), 1);
                if (g_engineDLL == nullptr) {
                    TF_DEBUG(SBSAR_RENDER)
                      .Msg("SbsarEngine: Failed to load engine: %s\n", dllFullPath.c_str());
                } else {
                    TF_DEBUG(SBSAR_RENDER)
                      .Msg("SbsarEngine: Loaded engine: %s\n", dllFullPath.c_str());
                    if (!algIntegrationsRendererIsEngineValid(g_engineDLL)) {
                        // TODO: unload
                        TF_WARN("SbsarEngine: Failed to initialize engine: %s",
                                dllFullPath.c_str());
                        ArchLibraryClose(g_engineDLL);
                        g_engineDLL = nullptr;
                    } else {
                        TF_STATUS("SbsarEngine: Using engine: %s", dllFullPath.c_str());
                        break;
                    }
                }
            }
            if (g_engineDLL != nullptr) {
                // Break out of the engine loop if we found a valid engine
                break;
            }
        }
    }
    if (g_engineDLL == nullptr) {
        TF_WARN("SbsarEngine: Failed to dynamically load a valid substance engine");
    }

    return g_engineDLL;
}
} // namespace

namespace adobe::usd::sbsar {
void*
getPreferredEngineDll(const std::string& searchName)
{
    return getEngineDll(searchName);
}
}
