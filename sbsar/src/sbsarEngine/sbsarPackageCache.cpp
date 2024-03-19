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
#include <sbsarDebug.h>
#include <sbsarEngine/sbsarPackageCache.h>
#include <sbsarEngine/sbsarRenderThread.h>
#include <usdGeneration/usdGenerationHelpers.h>

#include <substance/framework/package.h>

#include <pxr/base/arch/hash.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/ar/resolver.h>

#include <mutex>
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace SubstanceAir;
namespace adobe::usd::sbsar {
namespace { // anonymous namespace

static std::string _default_graph_identifier = "__default__";

std::string
findSelectedOutput(const ParsePathResult& parseResult, const SubstanceAir::GraphDesc& selectedGraph)
{
    for (const auto& o : selectedGraph.mOutputs) {
        std::string c_identifierString = o.mIdentifier.c_str();
        if (parseResult.bt == ParsePathResult::BT_IDENTIFIER &&
            c_identifierString == parseResult.usage.c_str()) {
            return o.mIdentifier.c_str();
        }
        if (parseResult.bt == ParsePathResult::BT_USAGE) {
            for (const auto& c : o.mChannelsStr) {
                std::string c_usageString = c.c_str();
                if (c_usageString == parseResult.usage) {
                    return o.mIdentifier.c_str();
                }
            }
        }
    }
    return "";
}

std::shared_ptr<GraphInstanceData>
createInstance(const std::shared_ptr<PackageDesc>& package,
               const adobe::usd::sbsar::ParsePathResult& sbsarParameters)
{
    const SubstanceAir::PackageDesc::Graphs& graphs = package->getGraphs();
    // Find the graph with the right label
    const SubstanceAir::GraphDesc* selectedGraph =
      findSelectedGraph(sbsarParameters.graphName, graphs);
    if (selectedGraph == nullptr) {
        TF_RUNTIME_ERROR("PackageCache: No suitable graph found");
        return nullptr;
    }
    // Check if the identifier existe for the selected ouput
    std::string selectedOutput_identifier = findSelectedOutput(sbsarParameters, *selectedGraph);
    if (selectedOutput_identifier.empty()) {
        TF_RUNTIME_ERROR("PackageCache: No suitable output found");
        return nullptr;
    }
    getCacheStats().graphInstanceCreated++;
    return std::make_shared<GraphInstanceData>(
      package, *selectedGraph, sbsarParameters.inputParameters);
}

std::shared_ptr<PackageDesc>
_readSbsar(const std::string& resolvedPackagePath, size_t* outContentHash)
{
    TfStopwatch w;
    w.Start();
    auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedPackagePath));
    if (!asset) {
        TF_RUNTIME_ERROR("PackageCache: Couldn't open SBSAR asset %s", resolvedPackagePath.c_str());
        return nullptr;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    if (!buffer) {
        TF_RUNTIME_ERROR("PackageCache: Could not retrieve buffer from asset");
        return nullptr;
    }

    if (outContentHash != nullptr) {
        *outContentHash = ArchHash64(buffer.get(), asset->GetSize());
    }

    std::shared_ptr<PackageDesc> packageDesc =
      std::make_shared<PackageDesc>(buffer.get(), asset->GetSize());
    w.Stop();

    TF_DEBUG_MSG(SBSAR_RENDER,
                 "PackageCache: Reading %s took: %lld ms\n",
                 resolvedPackagePath.c_str(),
                 w.GetMilliseconds());

    if (!packageDesc->isValid()) {
        TF_RUNTIME_ERROR("PackageCache: SBSAR asset %s is not a valid package",
                         resolvedPackagePath.c_str());
        return nullptr;
    }

    return packageDesc;
}

std::string
_normalizePath(const std::string& path)
{
    std::string normPath, innerPath;
    // If we have a package path like '/some/file.usdz[material.sbsar]' we split it into the
    // outerPath = '/some/file.usdz' and the innerPath = 'material.sbsar'. The inner part we don't
    // touch
    bool isPackagePath = ArIsPackageRelativePath(path);
    if (isPackagePath) {
        std::tie(normPath, innerPath) = ArSplitPackageRelativePathOuter(path);
    } else {
        normPath = path;
    }

    normPath = TfNormPath(normPath);

    if (isPackagePath) {
        normPath = ArJoinPackageRelativePath(normPath, innerPath);
    }

    return normPath;
}

using adobe::usd::sbsar::ParameterListPtr;

ParameterListPtr
_findSbsarParameters(const std::shared_ptr<PackageDesc>& packageDesc)
{
    ParameterListPtr parameters =
      std::make_shared<std::vector<const SubstanceAir::InputDescBase*>>();
    if (packageDesc && packageDesc->isValid()) {
        std::vector<const SubstanceAir::InputDescBase*>& params = *parameters.get();
        for (const SubstanceAir::GraphDesc& graph : packageDesc->getGraphs()) {
            params.reserve(params.size() + graph.mInputs.size());
            for (const SubstanceAir::InputDescBase* input : graph.mInputs) {
                params.emplace_back(input);
            }
        }
    }
    return parameters;
}

struct PackageCacheData
{
    std::shared_ptr<PackageDesc> package;
    std::unordered_map<std::string, std::shared_ptr<GraphInstanceData>> instanceCache;
    ParameterListPtr parameters;
    size_t contentHash;
    std::chrono::time_point<std::chrono::steady_clock> lastAccessTime;

    explicit PackageCacheData()
      : lastAccessTime(std::chrono::steady_clock::now())
    {
    }

    void updateLastAccessTime() { lastAccessTime = std::chrono::steady_clock::now(); }
};
using PackageCache = std::unordered_map<std::string, PackageCacheData>;

PackageCacheData&
_loadPackage(PackageCache& packageCache,
             const std::string& resolvedPackagePath,
             size_t* outContentHash = nullptr)
{
    // On Windows we sometimes get paths with either types of slashes. To make sure we always hit
    // the cache we normalize the paths.
    std::string normPath = _normalizePath(resolvedPackagePath);
    auto [it, inserted] = packageCache.insert({ normPath, PackageCacheData() });
    if (inserted) {
        it->second.package = _readSbsar(normPath, &it->second.contentHash);
        TF_DEBUG_MSG(SBSAR_RENDER, "PackageCache: added %s\n", normPath.c_str());
        getCacheStats().packageCreated++;
    } else {
        it->second.updateLastAccessTime();
    }

    if (packageCache.size() > getCacheSize().getMaxPackageCacheSize()) {
        // Remove the oldest entry
        auto oldest = std::min_element(
          packageCache.begin(), packageCache.end(), [](const auto& a, const auto& b) {
              return a.second.lastAccessTime < b.second.lastAccessTime;
          });
        TF_DEBUG(SBSAR_RENDER)
          .Msg("PackageCache: removing oldest entry %s\n", oldest->first.c_str());
        getCacheStats().packageDeleted++;
        getCacheStats().graphInstanceDeleted += oldest->second.instanceCache.size();
        packageCache.erase(oldest);
    }

    if (outContentHash != nullptr) {
        *outContentHash = it->second.contentHash;
    }

    return it->second;
}

struct GlobalPackageCache
{
    std::mutex mutex;
    PackageCache packageCache;
};

GlobalPackageCache&
_getGlobalPackageCache()
{
    static GlobalPackageCache globalPackageCache;
    return globalPackageCache;
}

} // end anonymous namespace

std::shared_ptr<SubstanceAir::PackageDesc>
getSbsarFromPackageCache(const std::string& resolvedPackagePath, size_t* outContentHash)
{
    GlobalPackageCache& globalPackageCache = _getGlobalPackageCache();
    std::lock_guard guard(globalPackageCache.mutex);
    auto& entry =
      _loadPackage(globalPackageCache.packageCache, resolvedPackagePath, outContentHash);
    return entry.package;
}

ParameterListPtr
getParameterListFromPackageCache(const std::string& resolvedPackagePath)
{
    GlobalPackageCache& globalPackageCache = _getGlobalPackageCache();
    std::lock_guard guard(globalPackageCache.mutex);
    auto& entry = _loadPackage(globalPackageCache.packageCache, resolvedPackagePath);
    // Compute the parameter list on demand
    if (!entry.parameters) {
        entry.parameters = _findSbsarParameters(entry.package);
        TF_DEBUG_MSG(
          SBSAR_RENDER, "PackageCache: added parameter list to %s\n", resolvedPackagePath.c_str());
    }
    return entry.parameters;
}

void
clearPackageCache()
{
    GlobalPackageCache& globalPackageCache = _getGlobalPackageCache();
    std::lock_guard guard(globalPackageCache.mutex);
    PackageCache& packageCache = globalPackageCache.packageCache;
    packageCache.clear();
}

GraphInstanceData::GraphInstanceData(std::shared_ptr<SubstanceAir::PackageDesc> package,
                                     const SubstanceAir::GraphDesc& graphDesc,
                                     const std::string& inputParameters)
  : m_package(package)
  , m_instance(graphDesc)
  , m_lastInputParameters(inputParameters)
{
}

SubstanceAir::GraphInstance&
GraphInstanceData::getGraphInstance()
{
    return m_instance;
}

const std::string&
GraphInstanceData::getLastInputParameters() const
{
    return m_lastInputParameters;
}

void
GraphInstanceData::setLastInputParameters(const std::string& inputParameters)
{
    m_lastInputParameters = inputParameters;
}

std::shared_ptr<GraphInstanceData>
getGraphInstanceFromPackageCache(const std::string& resolvedPackagePath,
                                 const ParsePathResult& sbsarParameters)
{
    GlobalPackageCache& globalPackageCache = _getGlobalPackageCache();
    std::lock_guard guard(globalPackageCache.mutex);

    auto& entry = _loadPackage(globalPackageCache.packageCache, resolvedPackagePath);
    auto& instanceCache = entry.instanceCache;
    const std::string& hash = sbsarParameters.graphName;
    auto instance = instanceCache.find(hash);
    if (instance != instanceCache.end())
        return instance->second;

    // create instance and add it to the cache.
    std::shared_ptr<GraphInstanceData> newInstance = createInstance(entry.package, sbsarParameters);
    instanceCache[hash] = newInstance;
    return newInstance;
}

const SubstanceAir::GraphDesc*
findSelectedGraph(const std::string& graphName, const SubstanceAir::Graphs& graphs)
{
    if (graphName == _default_graph_identifier) {
        // We are referring to the default graph
        return !graphs.empty() ? &graphs[0] : nullptr;
    } else {
        // Find the appropriate graph
        for (const SubstanceAir::GraphDesc& g : graphs) {
            std::string cleanedGraphName = getGraphName(g);
            if (cleanedGraphName == graphName) {
                return &g;
            }
        }
    }
    return nullptr;
}

}
