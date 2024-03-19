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
#pragma once
#include <sbsarEngine/sbsarAssetCache.h>
#include <substance/framework/graph.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>
// Forward decl
namespace SubstanceAir {
class PackageDesc;
typedef vector<GraphDesc> Graphs;
}

namespace adobe::usd::sbsar {

//! \brief Load and cache SBSAR packages coming from the USD asset system
//! This function is safe to call from any thread and will load the SBSAR package if it isn't in the
//! cache yet. The cache size is controled by CacheSize. When the cache is full, the oldest used
//! package is removed. Since the returned shared_ptr will presist even when the cache is cleared.
//! \param resolvedPackagePath The complete path to the package the should be opened
//! \param outContentHash     If valid, will be used to return the hash of the package
std::shared_ptr<SubstanceAir::PackageDesc>
getSbsarFromPackageCache(const std::string& resolvedPackagePath, size_t* outContentHash = nullptr);

using ParameterListPtr = std::shared_ptr<std::vector<const SubstanceAir::InputDescBase*>>;

//! \brief Get list of parameter names of the graphs inside of the package
//! This function is safe to call from any thread and will load the SBSAR package if it isn't in the
//! cache yet. Based on the package it will extract and cache the list of parameter names.
//! \param resolvedPackagePath The complete path to the package the should be opened
ParameterListPtr
getParameterListFromPackageCache(const std::string& resolvedPackagePath);

//! \brief Erase all the cache.
void
clearPackageCache();

//! \brief
//! Class to store a GraphInstance and the last input parameters used.
class GraphInstanceData
{
  public:
    GraphInstanceData(std::shared_ptr<SubstanceAir::PackageDesc> package,
                      const SubstanceAir::GraphDesc& graphDesc,
                      const std::string& inputParameters);

    SubstanceAir::GraphInstance& getGraphInstance();
    const std::string& getLastInputParameters() const;
    void setLastInputParameters(const std::string& inputParameters);

  private:
    // Keep a reference to the package to avoid it being deleted while the graph instance is used.
    std::shared_ptr<SubstanceAir::PackageDesc> m_package;
    SubstanceAir::GraphInstance m_instance;
    std::string m_lastInputParameters;
};

//! \brief Get an instance of a graph in a package.
//! This function is safe to call from any thread, will load the SBSAR package if it isn't in the
//! cache yet and create the instance if it isn't in the cache yet.
//! Graph instance are deleted with the package, since the returned shared_ptr will presist even
//! when the cache is cleared.
//! \param resolvedPackagePath The complete path to the package the should be opened.
//! \param sbsarParameters Graph name and other sbsar's input parameters.
std::shared_ptr<GraphInstanceData>
getGraphInstanceFromPackageCache(const std::string& resolvedPackagePath,
                                 const ParsePathResult& sbsarParameters);

//! \brief Find a graph in the package with a given name
const SubstanceAir::GraphDesc*
findSelectedGraph(const std::string& graphName, const SubstanceAir::Graphs& graphs);
}
