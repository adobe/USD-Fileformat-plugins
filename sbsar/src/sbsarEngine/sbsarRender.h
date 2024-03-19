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
//! \brief Start a rendering of the given graph instance with the given sbsar parameters.
//! Store all result in AssetCache.
//! \param renderer Substance renderer, must be unique.
//! \param instanceData Graph instance to renderer.
//! \param sbsarParameters Input parameters that will be set to the graph instance.
//! \param assetCache Cache where all the render's result are stored.
void
renderGraph(SubstanceAir::Renderer& renderer,
            GraphInstanceData& instanceData,
            const ParsePathResult& sbsarParameters,
            AssetCache& assetCache);
}
