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

#include <sbsarEngine/sbsarPackageCache.h>

namespace adobe::usd::sbsar {
//! \brief Prepare a graph instance for rendering: patch output formats and apply input parameters.
//! Fast operation, no shared state access. Called under state->lock in the render thread.
void USDSBSAR_API
prepareGraph(SubstanceAir::Renderer& renderer,
             GraphInstanceData& instanceData,
             const ParsePathResult& sbsarParameters);

//! \brief Execute the expensive substance rendering (push, run, flush).
//! No shared state access -- safe to call without holding state->lock.
void USDSBSAR_API
executeGraph(SubstanceAir::Renderer& renderer, SubstanceAir::GraphInstance& instance);

//! \brief Collect render results from graph outputs and store them in the asset cache.
//! Accesses AssetCache for previous results (unchanged outputs) and stores new results.
//! Must be called while holding state->lock.
void USDSBSAR_API
collectAndStoreResults(GraphInstanceData& instanceData,
                       const ParsePathResult& sbsarParameters,
                       AssetCache& assetCache);
}
