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

#include "sandbox/fileformat/fileFormat.h"

#include <fileformatutils/common.h>
#include <fileformatutils/layerRead.h>
#include <fileformatutils/layerWriteSdfData.h>
#include <fileformatutils/resolver.h>
#include <fileformatutils/usdData.h>
#include <sandbox/debugCodes.h>
#include <sandbox/hardening/hardening.h>
#include <sandbox/protocol/assetReader.h>
#include <sandbox/protocol/assetWriter.h>
#include <sandbox/protocol/hostProtocol.h>
#include <sandbox/resolver/inMemoryResolver.h>
#include <sandbox/resolver/inMemoryWritableAsset.h>
#include <sandbox/utilities/quarantine.h>
#include <sandbox/utilities/sandboxAssetCache.h>
#include <sandbox/utilities/utilities.h>

#include <pxr/base/arch/env.h>
#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/js/json.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/thisPlugin.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/pxr.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>

#include <cstdlib>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

using namespace adobe::usd;

TF_DEFINE_PUBLIC_TOKENS(UsdSandboxProxyFileFormatTokens, USDSANDBOXPROXY_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdSandboxProxyFileFormat, SdfFileFormat);
}

namespace {

// File format arguments handled on the host and erased before the argument map is sent to the
// sandboxed worker, so the worker never receives them.
const std::string kAssetsPathArg = "assetsPath";
const std::string kAllowLargeAssetsArg = "sandboxAllowLargeAssets";

/*
 * Simple class to override an environment variable on creation, and restore it to its original
 * state in the destructor
 *
 * Because this class works by restoring the original value (and emitting a debug message) in the
 * destructor, if added to a container, it should be added with emplace or emplace back so the
 * destructor doesn't run sooner than intended
 */
class EnvVarOverride
{
public:
    /*
     * Create an environment variable override, which will reset an environment variable for as
     * long as this object exists. This constructor will replace the value, which will be restored
     * by the destructor
     *
     * envVarToOverride: the name of the environment variable to be temporarily overridden
     * newValue: an optional parameter for the value that will be temporarily given to the
     *           environment variable
     */
    EnvVarOverride(const std::string& envVarToOverride, const std::string& newValue = "")
      : _paramName(envVarToOverride)
      , _originalValue(ArchGetEnv(_paramName))
    {
        ArchSetEnv(_paramName.c_str(), newValue.c_str(), true);
        TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                     "(HOST) Temporarily modifying environment variable %s.\n\tWas: \"%s\"\n\tNow "
                     "set to: \"%s\"\n",
                     _paramName.c_str(),
                     _originalValue.c_str(),
                     ArchGetEnv(_paramName).c_str());
    }

    ~EnvVarOverride()
    {
        ArchSetEnv(_paramName.c_str(), _originalValue.c_str(), true);
        TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                     "(HOST) Restored environment variable %s.\n\tValue: \"%s\"\n",
                     _paramName.c_str(),
                     ArchGetEnv(_paramName).c_str());
    }

private:
    const std::string _paramName;
    const std::string _originalValue;
};

/*
 * A helper class that sets all necessary environment variables for relevant operating systems,
 * and restores them when this object goes out of scope. This always updates PXR_PLUGINPATH_NAME
 * by replacing the proxy plugin's path entry with the unsafe plugin root, so the sandboxed
 * process finds the correct (sandboxed) plugins without discovering this proxy plugin.
 *
 * Different temp folders that the sandbox has access to are needed for different OS's. On Mac,
 * TMPDIR is set this way, while on Windows, TMP and TEMP are set. This is not currently needed on
 * Linux.
 */
class ScopedPluginPathOverride
{
private:
    // Store all environment variable overrides in this list. When this object goes out of scope,
    // the list will be destructed, and all elements' destructors will restore the original
    // environment variables
    std::list<EnvVarOverride> _envVarOverrides;

public:
    /*
     * Override relevant environment variables until the destructor is run.
     *
     * proxyPluginPath: the location where this plugin (SandboxProxy) is registered. This
     *                  is likely the directory containing plugin's plugInfo.json, or a
     *                  parent plugInfo that directs to the proxy plugin's. This entry will
     *                  be replaced with unsafePluginRoot. This must be the same path as
     *                  used in PXR_PLUGINPATH_NAME for finding this plugin.
     * unsafePluginRoot: the sandboxed plugin's directory to substitute in.
     */
    ScopedPluginPathOverride(const std::string& proxyPluginPath,
                             const std::string& unsafePluginRoot)
    {
        // Replace only the proxy plugin's entry in PXR_PLUGINPATH_NAME with the unsafe plugin
        // root, preserving all other path entries
        std::string newPluginPath = adobe::usd::sandbox::BuildNewPluginPath(
          ArchGetEnv("PXR_PLUGINPATH_NAME"), proxyPluginPath, unsafePluginRoot);
        _envVarOverrides.emplace_back("PXR_PLUGINPATH_NAME", newPluginPath);

        // Prevent the subprocess from registering the proxy plugin, when the proxy plugin is
        // co-located with other usd plugins (such as in bundled app builds, where the proxy may
        // be found with PXR_PLUGIN_BUILD_LOCATION preset. In other situations, this is redundant
        // but harmless.
#if SANDBOX_IS_WINDOWS
        const std::string pluginNameSep = ";";
#else
        const std::string pluginNameSep = ":";
#endif
        const std::string existingDisabledPlugins = ArchGetEnv("PXR_DISABLED_PLUGIN_NAMES");
        const std::string newDisabledPlugins =
          existingDisabledPlugins.empty()
            ? "usdSandboxProxy_plugin"
            : existingDisabledPlugins + pluginNameSep + "usdSandboxProxy_plugin";
        _envVarOverrides.emplace_back("PXR_DISABLED_PLUGIN_NAMES", newDisabledPlugins);

        // Override OS-specific temp directories so that plugins only use temp directories that
        // the sandbox has access to

#if SANDBOX_IS_WINDOWS
        // The sandboxed process has low integrity, which means it can only write to
        // %USERPROFILE/AppData/LocalLow, so that will be our temp directory
        const std::filesystem::path windowsTempDirPath =
          std::filesystem::path(ArchGetEnv("USERPROFILE")) / "AppData" / "LocalLow";
        const std::string windowsTempDir = windowsTempDirPath.string();

        // Windows can find a temp directory using either using TEMP or TMP environment variables
        _envVarOverrides.emplace_back("TEMP", windowsTempDir);
        _envVarOverrides.emplace_back("TMP", windowsTempDir);

#elif SANDBOX_IS_MACOS
        // The sandbox profile grants access to a specific temp directory (defined in
        // hardening.h). MacOS finds a temp directory using the TMPDIR environment variable.
        _envVarOverrides.emplace_back("TMPDIR", adobe::usd::sandbox::hardening::GetTempDirMacOS());

#endif
    }
};

// This should be using PLUG_THIS_PLUGIN but it seems like our build system does not
// support it yet, so we use the registry to get the plugin path instead.

static PlugPluginPtr sThisPlugin =
  PlugRegistry::GetInstance().GetPluginWithName("usdSandboxProxy_plugin");

JsObject
getPluginData()
{
    if (sThisPlugin) {
        JsObject pluginData =
          sThisPlugin->GetMetadataForType(TfType::Find<UsdSandboxProxyFileFormat>());
        return pluginData;
    }
    return {};
}

// Get a variable from the plugInfo.json file for the plugin. If it is not found or not a string,
// an empty string is returned.
std::string
getPlugInfoVar(const std::string& varName)
{
    std::string plugInfoVar = "";
    JsObject pluginData = getPluginData();
    if (pluginData.count(varName) > 0) {
        JsValue ext = pluginData[varName];
        if (ext.Is<std::string>()) {
            plugInfoVar = ext.Get<std::string>();
            TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                         "PlugInfo variable read: %s = %s\n",
                         varName.c_str(),
                         plugInfoVar.c_str());
        } else {
            TF_WARN("PlugInfo variable %s is not a string\n", varName.c_str());
        }
    }
    return plugInfoVar;
}

// Gets the extensions from the plugin metadata.
// at runtime to avoid a build time dependency on what plugins are
// sandboxed
std::vector<std::string>
getSandboxedExtensions()
{
    std::vector<std::string> extensions;
    JsObject pluginData = getPluginData();
    JsValue ext = pluginData["extensions"];
    if (ext.IsArrayOf<std::string>()) {
        extensions = ext.GetArrayOf<std::string>();
    }
    return extensions;
}

/*
 * Resolve a path that is specified relative to the plugin DLL's directory.
 *
 * pluginDllPath: the absolute path to the plugin library or its plugInfo.json; its
 *                parent directory is used as the base for resolution.
 * relativeLocation: the path to resolve relative to that base directory.
 *
 * Returns the weakly-canonicalized absolute path.
 */
std::filesystem::path
composeAbsoluteSandboxPath(const std::filesystem::path& pluginDllPath,
                           const std::filesystem::path& relativeLocation)
{
    return std::filesystem::weakly_canonical(std::filesystem::path(pluginDllPath.parent_path()) /
                                             relativeLocation);
}

/*
 * Resolve symlinks in an asset I/O path so the path the sandboxed worker opens matches the
 * canonical path the sandbox profile grants access to. Falls back to the input path if resolution
 * fails.
 *
 * path: the (possibly symlinked) absolute path the worker will read from or write to.
 *
 * Returns the symlink-resolved path, or path unchanged if resolution fails.
 */
std::string
canonicalizeSandboxIoPath(const std::string& path)
{
    std::error_code ec; // Non-throwing overload to not crash host process
    std::filesystem::path canonical =
      std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec) {
        // Falling back to the unresolved path may re-introduce a symlinked prefix that the sandbox
        // profile does not grant, causing the worker's open to be denied.
        TF_WARN("(HOST) Failed to canonicalize sandbox asset path \"%s\": %s. Falling back to the "
                "unresolved path.",
                path.c_str(),
                ec.message().c_str());
        return path;
    }
    return canonical.string();
}

std::filesystem::path
getSandboxExecutablePath()
{
    std::filesystem::path sandboxExecutablePath = "";
    JsObject pluginData = getPluginData();
    JsValue ext = pluginData["SandboxExecutableRelativePath"];
    if (ext.Is<std::string>()) {
        std::string relativePath = ext.Get<std::string>();
        sandboxExecutablePath = composeAbsoluteSandboxPath(sThisPlugin->GetPath(), relativePath);
    }
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Found sandbox executable path from plugInfo: %s\n",
                 sandboxExecutablePath.string().c_str());
    return sandboxExecutablePath;
}

std::filesystem::path
getProxyPluginPath()
{
    std::filesystem::path proxyPluginPath = "";
    JsObject pluginData = getPluginData();
    JsValue ext = pluginData["ProxyPluginPath"];
    if (ext.Is<std::string>()) {
        std::string path = ext.Get<std::string>();
        proxyPluginPath = std::filesystem::path(path);
        if (!proxyPluginPath.is_absolute()) {
            proxyPluginPath = composeAbsoluteSandboxPath(sThisPlugin->GetPath(), path);
        }
    }
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Found current proxy plugin path from plugInfo: %s\n",
                 proxyPluginPath.string().c_str());
    return proxyPluginPath;
}

std::filesystem::path
getUnsafePluginRoot()
{
    std::filesystem::path unsafePluginRoot = "";
    JsObject pluginData = getPluginData();
    JsValue ext = pluginData["UnsafePluginRelativePath"];
    if (ext.Is<std::string>()) {
        std::string relativePath = ext.Get<std::string>();
        unsafePluginRoot = composeAbsoluteSandboxPath(sThisPlugin->GetPath(), relativePath);
    }
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Found sandboxed plugins root from plugInfo: %s\n",
                 unsafePluginRoot.string().c_str());
    return unsafePluginRoot;
}

}
UsdSandboxProxyFileFormat::UsdSandboxProxyFileFormat()
  : SdfFileFormat(UsdSandboxProxyFileFormatTokens->Id,
                  UsdSandboxProxyFileFormatTokens->Version,
                  UsdSandboxProxyFileFormatTokens->Target,
                  // XXX This is coupled with what formats are sandboxed
                  // and we should ideally derive this from what is in the
                  // in pluginfo.json to have to inject this at build time
                  getSandboxedExtensions())
{
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "usdsandboxproxy %s\n", FILE_FORMATS_VERSION);
    _sandboxExecutablePath = getSandboxExecutablePath();
    _proxyPluginPath = getProxyPluginPath();
    _unsafePluginRoot = getUnsafePluginRoot();
}

UsdSandboxProxyFileFormat::~UsdSandboxProxyFileFormat() {}

void
UsdSandboxProxyFileFormat::ComposeFieldsForFileFormatArguments(
  const std::string& assetPath,
  const PcpDynamicFileFormatContext& context,
  FileFormatArguments* args,
  VtValue* dependencyContextData) const
{}

bool
UsdSandboxProxyFileFormat::CanRead(const std::string& filePath) const
{
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "CanRead: %s\n", filePath.c_str());
    return true;
}

namespace {

// Gather every item of an SdfListOp across all six of its list-op positions. A subverted worker
// may author a composition arc in any position -- including Deleted: a deleted-list arc composes
// to nothing, but a fail-closed gate rejects on any external arc regardless of position, so the
// enumeration must be complete.
template<typename T>
std::vector<T>
allListOpItems(const SdfListOp<T>& op)
{
    std::vector<T> items;
    for (SdfListOpType type : { SdfListOpTypeExplicit,
                                SdfListOpTypeAdded,
                                SdfListOpTypeDeleted,
                                SdfListOpTypePrepended,
                                SdfListOpTypeAppended,
                                SdfListOpTypeOrdered }) {
        const std::vector<T>& part = op.GetItems(type);
        items.insert(items.end(), part.begin(), part.end());
    }
    return items;
}

// True if the reference/payload list op holds any arc with a non-empty asset path (an *external*
// arc). An internal reference/payload (empty asset path) targets the same layer -- used for
// instancing -- and is allowed.
template<typename ListOpT>
bool
hasExternalArc(const VtValue& fieldValue)
{
    if (!fieldValue.IsHolding<ListOpT>()) {
        return false;
    }
    for (const auto& arc : allListOpItems(fieldValue.UncheckedGet<ListOpT>())) {
        if (!arc.GetAssetPath().empty()) {
            return true;
        }
    }
    return false;
}

// Keep the assetsPath export write inside assetsPath. The write below is `assetsPath / name`, and
// `name` derives from a worker-authored asset path that may be absolute or climb out via "..", so
// an unconstrained join is a path-traversal write of attacker-controlled bytes. Returns `name`
// unchanged when the resulting write stays within assetsPath -- including a clean nested name
// ("textures/img.png") or an absolute path that already lies inside assetsPath -- otherwise its
// bare filename, which is always contained. The test is on the normalized joined path (not on the
// name's shape), matching the std::filesystem semantics that perform the write.
std::string
containedAssetName(const std::string& assetsPath, const std::string& name)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::absolute(assetsPath, ec).lexically_normal();
    const fs::path joined = fs::absolute(fs::path(assetsPath) / name, ec).lexically_normal();
    const fs::path rel = joined.lexically_relative(base);
    const bool within = !rel.empty() && *rel.begin() != "..";
    if (within) {
        return name;
    }
    // The delivered asset's worker-authored name escaped assetsPath; contain it to its basename.
    // Log the event but not the name itself -- it is attacker-controlled and must stay out of logs.
    TF_WARN("(HOST) Contained an escaping delivered-asset name to its basename on extraction "
            "(traversing package key from the worker).");
    return fs::path(name).filename().string();
}

/*
 * Scrub a worker-produced layer before it is composed into the caller's stage, turning the host
 * from a confused deputy into an informed one.
 *
 * (1) Fail-closed structural gate: reject the whole import if the layer carries any external
 *     composition arc (reference, payload, sublayer, or value clips) or any variant set --
 *     shapes a by-the-book sandboxed import never emits, so their presence signals a subverted
 *     worker.
 * (2) Reference routing: make every asset value resolve only through the sandbox's own resolvers.
 *     InMemory:// and package-relative cache hits are kept (they route to InMemoryResolver /
 *     SandboxProxyResolver); everything else -- including a cache hit on a plain/absolute key,
 *     which would route to the host default resolver -- is quarantined into an inert BadAsset://
 *     URI.
 *
 * Returns false (import fails) on a structural-gate hit; otherwise rewrites in place and returns
 * true.
 */
static bool
scrubImportedLayer(const PXR_NS::SdfLayerRefPtr& tempLayer)
{
    using namespace adobe::usd::sandbox;

    static constexpr char kRejectFmt[] =
      "(HOST) Rejecting sandbox output: %s -- sandboxed imports never emit these; treating worker "
      "output as attacker-controlled.";

    // Sublayers are external composition arcs; check once at the layer level.
    if (!tempLayer->GetSubLayerPaths().empty()) {
        TF_CODING_ERROR(kRejectFmt, "sublayer");
        return false;
    }

    // Structural gate: walk every prim spec for external arcs, value clips, and variant sets.
    bool rejected = false;
    std::string rejectReason;
    tempLayer->Traverse(SdfPath::AbsoluteRootPath(), [&](const SdfPath& path) {
        if (rejected || !path.IsPrimPath()) {
            return;
        }
        if (hasExternalArc<SdfReferenceListOp>(
              tempLayer->GetField(path, SdfFieldKeys->References))) {
            rejected = true;
            rejectReason = "external reference";
        } else if (hasExternalArc<SdfPayloadListOp>(
                     tempLayer->GetField(path, SdfFieldKeys->Payload))) {
            rejected = true;
            rejectReason = "payload";
        } else if (tempLayer->HasField(path, SdfFieldKeys->Clips) ||
                   tempLayer->HasField(path, SdfFieldKeys->ClipSets)) {
            rejected = true;
            rejectReason = "value clips";
        } else if (SdfPrimSpecHandle prim = tempLayer->GetPrimAtPath(path);
                   prim && !prim->GetVariantSets().empty()) {
            rejected = true;
            rejectReason = "variant set";
        }
    });
    if (rejected) {
        TF_CODING_ERROR(kRejectFmt, rejectReason.c_str());
        return false;
    }

    // Reference routing: keep sandbox-routed values, quarantine everything else.
    SandboxAssetCache& cache = SandboxAssetCache::GetInstance();
    FindAndModifyAssetPaths(tempLayer, [&](const std::string& authoredPath, std::string& newName) {
        if (IsQuarantined(authoredPath)) {
            return false; // already inert -- never nest
        }
        if (TfStringStartsWith(authoredPath, "InMemory://")) {
            return false; // routes to InMemoryResolver
        }
        // A package-relative reference with a cache hit routes to SandboxProxyResolver; its bytes
        // crossed the boundary. A cache hit on a plain/absolute key does NOT qualify -- it would
        // resolve through the host default resolver -- so it is quarantined below.
        if (ArIsPackageRelativePath(authoredPath) && cache.FindCachedAsset(authoredPath)) {
            return false;
        }
        newName = QuarantineReference(authoredPath);
        // newName is the BadAsset://<base64url> form -- safe to log (no raw attacker bytes).
        TF_WARN("(HOST) Quarantined unmarshalled reference (asset did not cross the sandbox "
                "boundary): %s",
                newName.c_str());
        return true;
    });

    return true;
}

} // namespace

bool
UsdSandboxProxyFileFormat::Read(PXR_NS::SdfLayer* layer,
                                const std::string& resolvedPath,
                                bool metadataOnly) const

{
    using namespace adobe::usd::sandbox;
    using namespace std::filesystem;

    TfStopwatch hostSetupWatch, waitForConversionWatch, importWatch, transferDataWatch;

    // Warning message provides a reliable indicator in logs that sandboxing is active regardless
    // of whether debug prints are active or not (based on TF_DEBUG environment variable)
    TF_WARN("(HOST) Using sandbox to read asset: %s\n", resolvedPath.c_str());

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "Read: %s\n", resolvedPath.c_str());
    hostSetupWatch.Start();

    HostProtocol protocol;

    // TODO: This will need to be refactored. This was initially setup for absolute pathing but
    // all of these paths need to be relative for package builds. We need to either find these
    // paths at runtime or we need to make sure relative paths are being set to SandboxLibraryPath
    // at build time. Baked paths only work for local builds. This will prevent us from releasing
    // standalone packages of file formats.
    std::string sandboxAccessiblePaths = getPlugInfoVar("SandboxLibraryPath") + ":" +
                                         getUnsafePluginRoot().string() + ":" +
                                         getExecutableDirectory().parent_path().string();

    // Resolve symlinks (e.g. /var -> /private/var on macOS) so the path the sandboxed worker
    // reads matches the canonical path the sandbox profile grants access to.
    const std::string canonicalizedPath = canonicalizeSandboxIoPath(resolvedPath);

    hardening::LaunchHardening hardeningArgs = hardening::BuildLaunchHardening(
      { canonicalizedPath, sandboxAccessiblePaths, /*isExport=*/false });

    std::string processName = _sandboxExecutablePath.string();
    TF_DEBUG_MSG(
      FILE_FORMAT_SANDBOXPROXY, "(HOST) Creating process named %s\n", processName.c_str());

    {
        // This replaces the proxy plugin's entry in the PXR_PLUGINPATH_NAME environment
        // variable with the sandboxed plugins entry, so the sandboxed process can find the unsafe
        // plugins and not find the proxy plugin. This is reverted when the object is destructed,
        // when it goes out of scope after the sandboxed process is launched with the necessary
        // environment.
        ScopedPluginPathOverride pluginPathScope(_proxyPluginPath.string(),
                                                 _unsafePluginRoot.string());

        if (!protocol.LaunchProcess(processName,
                                    canonicalizedPath,
                                    _unsafePluginRoot.string(),
                                    /*isExport=*/false,
                                    hardeningArgs.preExecHook,
                                    hardeningArgs.commandPrefix)) {
            TF_WARN("(HOST) Failed to launch unsafe process.");
            return false;
        } else {
            hostSetupWatch.Stop();
            TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                         "(HOST) Launched unsafe Process. Init and Launch duration: %ld\n",
                         static_cast<long int>(hostSetupWatch.GetMilliseconds()));
        }
    }

    // Send and wait for conversion data

    // 1. Send file format arguments to sandboxed process so it can convert the asset
    // 2. Wait for the sandboxed process to calculate and request a shared memory size
    // 3. Initialize shared memory with the required size
    // 4. Send the shared memory name and size to the sandboxed process

    // Retrieve FileFormatArguments from the layer and send to sandboxed process
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Writing file format arguments to pipe\n");
    std::map<std::string, std::string> fileFormatArgs = layer->GetFileFormatArguments();

    // assetsPath is processed on the host, not sent to the sandbox. If set, it indicates where
    // to write image data during import; the sandboxed process cannot output images directly.
    std::string assetsPath = "";
    if (fileFormatArgs.find(kAssetsPathArg) != fileFormatArgs.end()) {
        assetsPath = fileFormatArgs[kAssetsPathArg];
        fileFormatArgs.erase(kAssetsPathArg);
    }

    // Host-side policy: lift the cap on the worker-reported asset size for legitimate large
    // assets. Consumed here (and erased) so it is not forwarded to the sandboxed worker.
    bool allowLargeAssets =
      adobe::usd::sandbox::ConsumeBoolArg(fileFormatArgs, kAllowLargeAssetsArg);

    if (!protocol.SendFileFormatArgs(fileFormatArgs)) {
        TF_WARN("(HOST) Failed to write file format arguments to sandbox.");
        return false;
    }

    // Wait for the sandboxed process to calculate and request a shared memory size
    size_t assetsSize = 0;
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Waiting for sandboxed process to request shared memory size\n");
    if (!protocol.ReceiveAssetSize(assetsSize, allowLargeAssets)) {
        TF_WARN("(HOST) Failed to read asset size from sandbox.");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Received assets size: %zu. Initializing shared memory.\n",
                 assetsSize);
    if (!protocol.InitializeSharedMemory(hardening::GetShmNamePrefix(), assetsSize)) {
        TF_WARN("(HOST) Failed to communicate size and allocate shared memory.");
        return false;
    }

    // Wait for the worker to write the data and exit before this process reads it.
    waitForConversionWatch.Start();
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Waiting for conversion process.\n");
    if (!protocol.WaitForCompletion()) {
        TF_WARN("(HOST) Failed to wait for conversion process.");
        return false;
    } else {
        waitForConversionWatch.Stop();
        TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                     "(HOST) File Converted. Duration: %ld\n",
                     static_cast<long int>(waitForConversionWatch.GetMilliseconds()));
    }

    // Read the assets from the binary data and add them to the cache
    AssetReader assetReader(protocol.GetSharedMemory());
    if (!assetReader.ProcessAssetsFromSharedMemory(
          [](const std::string& path, const std::shared_ptr<PXR_NS::ArAsset>& asset) {
              if (path == inMemoryURI) {
                  // This is the USDC data - copy it to InMemoryResolver
                  auto writableAsset = ArGetResolver().OpenAssetForWrite(
                    ArResolvedPath(path), ArResolver::WriteMode::Replace);

                  // TODO: REMOVE THIS ADDITIONAL COPY SOMEHOW!!
                  // Do we have to read the ArAsset into a writeable asset? Can we just add the
                  // ArAsset directly into the InMemoryResolver? We could use
                  // InMemoryResolver::SetData to set the data directly, but then we need to cast
                  // the resolver. What are the best alternatives?
                  if (writableAsset) {
                      // Read all data from the source asset
                      size_t size = asset->GetSize();
                      std::vector<char> buffer(size);
                      asset->Read(buffer.data(), size, 0);

                      // Write to InMemoryResolver
                      writableAsset->Write(buffer.data(), size, 0);

                      // TODO: Currently, inMemoryWritableAsset::Close always returns false, so this
                      // warning is emitted without being helpful. Uncomment this when that function
                      // properly returns true or false.
                      // if (!writableAsset->Close()) {
                      //     TF_WARN(
                      //       "(HOST) Failed to close USD writable asset while extracting \"%s\"
                      //       from " "shared memory.", path.c_str());
                      // }
                  } else {
                      TF_WARN("(HOST) Failed to open USD writable asset for extracting \"%s\" from "
                              "shared memory.",
                              path.c_str());
                  }
              } else {
                  // Texture assets - add to cache
                  SandboxAssetCache& sandboxAssetCache = SandboxAssetCache::GetInstance();
                  sandboxAssetCache.AddImageToCache(path, asset);
              }
          })) { // if (!ProcessAssetsFromSharedMemory)
        TF_WARN("(HOST) Error reading assets from shared memory.");
        return false;
    }

    importWatch.Start();
    SdfLayerRefPtr tempLayer = SdfLayer::FindOrOpen(inMemoryURI);
    if (!tempLayer) {
        TF_CODING_ERROR(
          "(HOST) Failed to load USD data with specified InMemory URI: %s. This error should have "
          "been caught earlier, when the asset was created in the sandbox.\n",
          inMemoryURI.c_str());
        return false;
    }
    importWatch.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Converted File Imported. Duration: %ld\n",
                 static_cast<long int>(importWatch.GetMilliseconds()));

    // Scrub the worker-produced layer before it is composed into the caller's stage: fail closed
    // on external composition arcs / variant sets, and route or quarantine every asset reference
    // so nothing resolves through the host's privileged resolver stack (confused-deputy fix). On
    // failure nothing is transferred into `layer`, so the caller sees a clean import failure.
    if (!scrubImportedLayer(tempLayer)) {
        return false;
    }

    if (!assetsPath.empty()) {
        std::function<bool(const std::string&, std::string&)> createAssetPath =
          [&](const std::string& authoredPath, std::string& newName) {
              // A quarantined reference is inert and must never be resolved or written; the scrub
              // pass already neutralized it. (It would also be a cache miss below, but guard the
              // invariant explicitly against future edits.)
              if (IsQuarantined(authoredPath)) {
                  return false;
              }
              // Update the asset reference to be the packaged asset name
              auto [outerPath, innerPath] = ArSplitPackageRelativePathInner(authoredPath);
              newName = !innerPath.empty() ? innerPath : authoredPath;

              // Contain the write within assetsPath. The name comes from a worker-authored path and
              // may be absolute or traverse via ".."; without this a subverted worker could write
              // its (attacker-controlled) bytes outside the caller's assetsPath. Names already
              // inside assetsPath are untouched; escaping names collapse to their basename. newName
              // also becomes the rewritten reference, so the reference and the file stay in sync.
              newName = containedAssetName(assetsPath, newName);

              SandboxAssetCache& sandboxAssetCache = SandboxAssetCache::GetInstance();
              if (std::shared_ptr<ArAsset> asset =
                    sandboxAssetCache.FindCachedAsset(authoredPath)) {
                  // There may be multiple references to the same asset in the USD data, but we
                  // don't want to write out the image each time. If we remove the image from the
                  // cache, later iterations on the same asset won't find it and we won't write out
                  // the image
                  sandboxAssetCache.RemoveImageFromCache(authoredPath);

                  // Create the assetsPath directory if it hasn't been created yet. This will only
                  // create the directory hierarchy if it doesn't exist, so it will only run once
                  if (!TfMakeDirs(assetsPath, -1, true)) {
                      TF_RUNTIME_ERROR("Failed to create directory for assetsPath: %s. Not using "
                                       "assetsPath for asset: %s",
                                       assetsPath.c_str(),
                                       authoredPath.c_str());
                      return false;
                  }

                  std::filesystem::path filepath = std::filesystem::path(assetsPath) / newName;
                  writeDataToDisk(filepath, asset->GetBuffer().get(), asset->GetSize());
              }
              return true;
          };

        // Iterate over all assets in the USD data, resolve and export all referenced assets, and
        // update each reference to use the new resolved name instead of a packaged path
        FindAndModifyAssetPaths(tempLayer, createAssetPath);
    }

    transferDataWatch.Start();
    layer->TransferContent(tempLayer);
    transferDataWatch.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                 "(HOST) Transfer data time:  %ld\n",
                 static_cast<long int>(transferDataWatch.GetMilliseconds()));

    return true;
}

bool
UsdSandboxProxyFileFormat::ReadFromString(SdfLayer* layer, const std::string& str) const
{
    // TODO: Implement reading from string.

    return false;
}

bool
UsdSandboxProxyFileFormat::WriteToFile(const SdfLayer& layer,
                                       const std::string& filename,
                                       const std::string& comment,
                                       const FileFormatArguments& args) const
{
    using namespace adobe::usd::sandbox;
    using namespace std::filesystem;

    TfStopwatch hostSetupWatch, waitForConversionWatch;

    // Normalize the path so the sandbox profile and the actual file write use the same
    // canonical form. Without this, `./` or `..` components cause macOS sandbox
    // `(subpath ...)` rules to not match the kernel-normalized paths.
    // Relative paths must also be made absolute so the sandboxed process (which may
    // have a different CWD) resolves the same file.
    std::filesystem::path filePath(filename);
    std::string absoluteFilename =
      (filePath.is_relative() ? std::filesystem::absolute(filePath) : filePath)
        .lexically_normal()
        .string();

    // Warning message provides a reliable indicator in logs that sandboxing is active regardless
    // of whether debug prints are active or not (based on TF_DEBUG environment variable)
    TF_WARN("(HOST) Using sandbox to write asset: %s\n", absoluteFilename.c_str());

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "Write: %s\n", absoluteFilename.c_str());

    // Ensure the output directory exists before launching the sandbox, since the
    // sandboxed process only has write permission to this directory, not its parent.
    std::filesystem::path outputDir = std::filesystem::path(absoluteFilename).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        TF_WARN("(HOST) Failed to create output directory %s: %s",
                outputDir.string().c_str(),
                ec.message().c_str());
        return false;
    }

    // Now that the output directory exists, resolve symlinks so the path the sandboxed worker
    // writes matches the canonical path the sandbox profile grants write access to. Without this,
    // the worker attempts to open the unresolved path and the kernel's symlink resolution is
    // denied by the profile, preventing the export file from being created.
    absoluteFilename = canonicalizeSandboxIoPath(absoluteFilename);

    hostSetupWatch.Start();

    // TODO: This will need to be refactored. This was initially setup for absolute pathing but
    // all of these paths need to be relative for package builds. We need to either find these
    // paths at runtime or we need to make sure relative paths are being set to SandboxLibraryPath
    // at build time. Baked paths only work for local builds. This will prevent us from releasing
    // standalone packages of file formats.
    std::string sandboxAccessiblePaths = getPlugInfoVar("SandboxLibraryPath") + ":" +
                                         getUnsafePluginRoot().string() + ":" +
                                         getExecutableDirectory().parent_path().string();

    std::string processName = _sandboxExecutablePath.string();

    // Load the USD before launching the process

    // The usdc data must be modified by changing asset paths to use the InMemory:// scheme, so
    // our resolvers can find the assets. We create a new anonymous layer to modify, since we
    // can't modify the const layer parameter.
    SdfLayerRefPtr modifiedLayer = SdfLayer::CreateAnonymous(".usdc");
    SdfLayerHandle layerHandle = SdfLayer::Find(layer.GetIdentifier());
    if (!layerHandle) {
        TF_WARN("(HOST) Failed to find layer handle for %s", layer.GetIdentifier().c_str());
        return false;
    }
    modifiedLayer->TransferContent(layerHandle);

    // Lambda to modify USD asset names to have InMemory:// prefix
    std::function<bool(const std::string&, std::string&)> createInMemoryAssetName =
      [](const std::string& authoredPath, std::string& newName) {
          // Update the asset reference to be the packaged asset name
          auto [outerPath, innerPath] = ArSplitPackageRelativePathInner(authoredPath);
          newName = "InMemory://" + (!innerPath.empty() ? innerPath : authoredPath);

          return true;
      };

    // Normally, FindAndModifyAssetPaths will resolve assets using the layer that is modified.
    // In this case, this layer is an anonymous layer with no associated path. (Created so it can
    // be modified with the necessary asset references). Because of this, it will not be able to
    // properly resolve assets. Instead, we must pass in the original layer as well, which will be
    // used to find and resolve referenced assets.

    // Get the asset paths that will be resolved, and modify them to use the InMemory:// scheme
    std::unordered_map<std::string, std::string> assets =
      FindAndModifyAssetPaths(modifiedLayer, createInMemoryAssetName, layer);
    assets.insert({ inMemoryURI, inMemoryURI });

    // Convert modified layer to an ArAsset
    ArGetResolver().OpenAssetForWrite(ArResolvedPath(inMemoryURI), ArResolver::WriteMode::Update);
    modifiedLayer->Export(inMemoryURI);

    // Get the ArAssets themselves and write them to shared memory
    AssetMap arAssets = GetArAssets(assets);

    HostProtocol protocol;

    hardening::LaunchHardening hardeningArgs = hardening::BuildLaunchHardening(
      { absoluteFilename, sandboxAccessiblePaths, /*isExport=*/true });

    // Build the asset writer against the protocol's shared memory (payload written after Create).
    AssetWriter assetWriter(protocol.GetSharedMemory(), arAssets);
    size_t assetsSize = assetWriter.GetSize();
    if (assetsSize == 0) {
        TF_WARN("(HOST) Failed to set assets to write and get size in bytes");
        return false;
    }

    // Launch the sandboxed process

    TF_DEBUG_MSG(
      FILE_FORMAT_SANDBOXPROXY, "(HOST) Creating process named %s\n", processName.c_str());

    {
        // This replaces the proxy plugin's entry in the PXR_PLUGINPATH_NAME environment
        // variable with the sandboxed plugins entry, so the sandboxed process can find the unsafe
        // plugins and not find the proxy plugin. This is reverted when the object is destructed,
        // when it goes out of scope after the sandboxed process is launched with the necessary
        // environment.
        ScopedPluginPathOverride pluginPathScope(_proxyPluginPath.string(),
                                                 _unsafePluginRoot.string());
        if (!protocol.LaunchProcess(processName,
                                    absoluteFilename,
                                    _unsafePluginRoot.string(),
                                    /*isExport=*/true,
                                    hardeningArgs.preExecHook,
                                    hardeningArgs.commandPrefix)) {
            TF_WARN("(HOST) Failed to launch unsafe process.");
            return false;
        } else {
            hostSetupWatch.Stop();
            TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                         "(HOST) Launched unsafe Process. Init and Launch duration: %ld\n",
                         static_cast<long int>(hostSetupWatch.GetMilliseconds()));
        }
    }

    // Export fileformat args flow to the worker.
    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Sending fileformat args to sandboxed process\n");
    if (!protocol.SendFileFormatArgs(args)) {
        TF_WARN("(HOST) Failed to write file format arguments to sandbox.");
        return false;
    }

    // Export: create -> write payload -> announce. The worker blocks on
    // ReceiveAndConnectSharedMemory until AnnounceSharedMemory, so it cannot read partial data
    TF_DEBUG_MSG(
      FILE_FORMAT_SANDBOXPROXY, "(HOST) Creating shared memory with size %zu.\n", assetsSize);
    if (!protocol.CreateSharedMemory(hardening::GetShmNamePrefix(), assetsSize)) {
        TF_WARN("(HOST) Failed to allocate shared memory.");
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Writing assets to shared memory\n");
    if (!assetWriter.WriteAssetsToSharedMemory()) {
        TF_WARN("(HOST) Failed to write assets for layer: %s", absoluteFilename.c_str());
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY, "(HOST) Announcing shared memory to subprocess\n");
    if (!protocol.AnnounceSharedMemory()) {
        TF_WARN("(HOST) Failed to send shared memory info to sandbox.");
        return false;
    }

    // The sandboxed process can now convert the asset

    waitForConversionWatch.Start();
    if (!protocol.WaitForCompletion()) {
        TF_WARN("(HOST) Error waiting for conversion process.");
        return false;
    } else {
        waitForConversionWatch.Stop();
        TF_DEBUG_MSG(FILE_FORMAT_SANDBOXPROXY,
                     "(HOST) File Converted. Duration: %ld\n",
                     static_cast<long int>(waitForConversionWatch.GetMilliseconds()));
    }

    return true;
}

bool
UsdSandboxProxyFileFormat::WriteToString(const SdfLayer& layer,
                                         std::string* str,
                                         const std::string& comment) const
{
    // Implement writing to string
    return false;
}

bool
UsdSandboxProxyFileFormat::WriteToStream(const SdfSpecHandle& spec,
                                         std::ostream& out,
                                         size_t indent) const
{
    out << "WriteToStream: Nothing to see." << std::endl;
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
