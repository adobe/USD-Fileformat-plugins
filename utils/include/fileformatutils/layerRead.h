/*
Copyright 2023 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#pragma once
#include "api.h"
#include "usdData.h"
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/common.h>

namespace adobe::usd {

struct USDFFUTILS_API ReadLayerOptions
{
    bool triangulate = false;
    bool flatten = false;
    bool ignoreInvisible = false;

    // The default max for the number of mesh joints indices and weights is 4.  Specific file
    // format exporters can modify this prior to export. Setting the value to -1 means the max is
    // ignored.
    int maxMeshInfluenceCount = 4;
};

/// \ingroup utils_layer
/// \brief Takes a SBSAR texture parameterization.
USDFFUTILS_API std::string
getSbsarUsageFromParameters(const std::string& parametersStr);

/// \ingroup utils_layer
/// \brief This function extracts a usable file path from an assetPath.
USDFFUTILS_API std::string
extractFilePathFromAssetPath(const std::string& assetPath);

/// \ingroup utils_layer
/// \brief Reads data from a USD layer and dumps it into a UsdData structure.
USDFFUTILS_API bool
readLayer(const ReadLayerOptions& options,
          const PXR_NS::SdfLayer& layer,
          UsdData& data,
          const std::string& debugTag);
}