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
#include "sbsarPackageResolver.h"
#include "sbsarDebug.h"
#include <assetPath/assetPathParser.h>
#include <assetResolver/sbsarResolverCache.h>
#include <fstream>
#include <mutex>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/definePackageResolver.h>
#include <sbsarEngine/sbsarRenderThread.h>
#include <pxr/usd/ar/inMemoryAsset.h>
#include <sbsarEngine/sbsarPackageCache.h>
#include <usdGeneration/usdGenerationHelpers.h>

using namespace adobe::usd::sbsar;
PXR_NAMESPACE_OPEN_SCOPE

AR_DEFINE_PACKAGE_RESOLVER(SBSARPackageResolver, ArPackageResolver);

namespace {
void
replaceAll(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}
}

SBSARPackageResolver::SBSARPackageResolver()
{
    TF_DEBUG(SBSAR_PACKAGE_RESOLVER).Msg("Package Resolver Created\n");
}

SBSARPackageResolver::~SBSARPackageResolver()
{
    TF_DEBUG(SBSAR_PACKAGE_RESOLVER).Msg("Package Resolver Destroyed\n");
}

std::string
SBSARPackageResolver::Resolve(const std::string& packagePath, const std::string& packagedPath)
{
    TF_DEBUG(SBSAR_PACKAGE_RESOLVER)
      .Msg("Resolving %s %s\n", packagePath.c_str(), packagedPath.c_str());
    const std::string ext = TfGetExtension(packagedPath);
    if (ext == "mdl") {
        // MDL files are not embedded
        // just return the path as absolute
        return packagedPath;
    }
    if (ext == "usd") {
        // Package resolver should refer mtlx
        // to the package if presence (not checking at this point)
        return packagedPath;
    }
    if (ext == "png") {
        // Package resolver should refer thumbnail.png
        // to the package if presence (not checking at this point)
        return packagedPath;
    }

    ParsePathResult parseOutput;
    ParsePathResult::ParseError result = parsePath(packagedPath, parseOutput);
    if (result == ParsePathResult::PE_SUCCESS) {
        // Add a .sbsarimage to make sure it's considered an image file
        return packagedPath + ".sbsarimage";
    } else {
        // TODO: Manage error
        return "";
    }
}

std::shared_ptr<ArAsset>
SBSARPackageResolver::OpenAsset(const std::string& packagePath, const std::string& packagedPath)
{
    std::string cache_path = packagePath + "[" + packagedPath + "]";

    SBSARResolverCache& cache = SBSARResolverCache::GetInstance();
    std::shared_ptr<ArAsset> cachedAsset = cache.findCachedAsset(cache_path);
    if (cachedAsset) {
        TF_DEBUG(SBSAR_PACKAGE_RESOLVER)
          .Msg("Using cached assets with key %s\n", cache_path.c_str());
        return cachedAsset;
    }

    std::shared_ptr<ArAsset> newAsset;
    if (TfGetExtension(packagedPath) == "sbsarimage") {
        newAsset = OpenSbsarAsset(packagePath, packagedPath);
    } else if (TfGetExtension(packagedPath) == "mtlx") {
        newAsset = OpenAssetMtlx(packagePath, packagedPath);
    } else if (TfGetExtension(packagedPath) == "png") {
        newAsset = OpenThumbnailAsset(packagePath, packagedPath);
    } else {
        // Unknown packaged file
        TF_WARN("Unsupported asset resolved %s %s", packagePath.c_str(), packagedPath.c_str());
    }
    if (newAsset) {
        cache.addCachedAsset(cache_path, newAsset);
    }
    return newAsset;
}

std::shared_ptr<ArAsset>
SBSARPackageResolver::OpenAssetMtlx(const std::string& packagePath, const std::string& packagedPath)
{
    TF_DEBUG(SBSAR_PACKAGE_RESOLVER)
      .Msg(
        "Mtlx References not implemented yet %s %s\n", packagePath.c_str(), packagedPath.c_str());
    return {};
}

std::shared_ptr<ArAsset>
SBSARPackageResolver::OpenSbsarAsset(const std::string& packagePath,
                                     const std::string& packagedPath)
{
    std::string fixedPackagedPath{ packagedPath };

    // Paths coming back from RTX have a strange "/[" when just "[" is
    // expected...
    replaceAll(fixedPackagedPath, "/[", "[");
    replaceAll(fixedPackagedPath, "/]", "]");

    TF_DEBUG(SBSAR_PACKAGE_RESOLVER)
      .Msg("Opening sbsar asset %s %s\n", packagePath.c_str(), fixedPackagedPath.c_str());

    std::string packagedPath_no_ext = fixedPackagedPath.substr(0, fixedPackagedPath.size() - 11);
    std::string cache_path = packagePath + packagedPath_no_ext;

    return renderSbsarAsset(packagePath, packagedPath_no_ext);
}

std::shared_ptr<ArAsset>
SBSARPackageResolver::OpenThumbnailAsset(const std::string& packagePath,
                                         const std::string& packagedPath)
{
    const SubstanceAir::GraphDesc* selectedGraph;
    std::shared_ptr<SubstanceAir::PackageDesc> packageDesc = getSbsarFromPackageCache(packagePath);
    if(packageDesc == nullptr) {
        TF_RUNTIME_ERROR("PackageCache: No package found");
        return nullptr;
    }
    const SubstanceAir::PackageDesc::Graphs& graphs = packageDesc->getGraphs();
    if (graphs.empty()) {
        TF_RUNTIME_ERROR("PackageCache: No graphs found");
        return nullptr;
    }

    if (packagedPath == "thumbnail.png") {
        // if the packagedPath is "thumbnail.png", return a thumbnail from a graph that matches 
        // the name of the sbsar file. 
        const std::string& graphName = TfStringGetBeforeSuffix(TfGetBaseName(packagePath));
        selectedGraph = findSelectedGraph(graphName, graphs);
        bool found = false; 
        if (selectedGraph) {
            if (!selectedGraph->mThumbnail.empty()) {
                found = true;
            }
        }

        // if no such graph exists, return the first graph's thumbnail.
        if (!found) {
            selectedGraph = &graphs[0];
            if (selectedGraph == nullptr) {
                TF_RUNTIME_ERROR("PackageCache: No suitable graph found");
                return nullptr;
            }
        }
    } else {
        const std::string& graphName = TfStringGetBeforeSuffix(TfGetBaseName(packagedPath));
        selectedGraph = findSelectedGraph(graphName, graphs);
        if (selectedGraph == nullptr) {
            TF_RUNTIME_ERROR("PackageCache: No suitable graph found");
            return nullptr;
        }
    } 

    assert(selectedGraph);
    const auto& thumbnailData = selectedGraph->mThumbnail;
    if(!thumbnailData.empty()) {
        auto data_size = thumbnailData.size();
        auto buffer = std::shared_ptr<char>(new char[data_size], std::default_delete<char[]>());
        char* data = buffer.get();
        std::copy(thumbnailData.begin(), thumbnailData.end(), data);
        return PXR_NS::ArInMemoryAsset::FromBuffer(std::move(buffer), data_size);
    } else {
        TF_RUNTIME_ERROR("No thumbnail found");
    }
    
    return nullptr;
}

void
SBSARPackageResolver::BeginCacheScope(VtValue* cacheScopeData)
{
    // TF_DEBUG(SBSAR_PACKAGE_RESOLVER).Msg("Beginning Cache Scope\n");
    SBSARResolverCache::GetInstance().BeginCacheScope(cacheScopeData);
    SBSARResolverCache::GetInstance().dumpStats();
}

void
SBSARPackageResolver::EndCacheScope(VtValue* cacheScopeData)
{
    // TF_DEBUG(SBSAR_PACKAGE_RESOLVER).Msg("Ending Cache Scope\n");
    SBSARResolverCache::GetInstance().dumpStats();
    SBSARResolverCache::GetInstance().EndCacheScope(cacheScopeData);
}
PXR_NAMESPACE_CLOSE_SCOPE
