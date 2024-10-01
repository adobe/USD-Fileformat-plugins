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
#include "layerWriteShared.h"

#include <pxr/pxr.h>
#include <pxr/usd/usd/common.h>

namespace adobe::usd {

// The ability to set Sdf data on a layer is not a public API, but the SdfFileFormat class has a
// private API that we can forward to the writeLayer function to give it that ability
using SetLayerDataFn = void (*)(PXR_NS::SdfLayer* layer, PXR_NS::SdfAbstractDataRefPtr& data);

/// \ingroup utils_layer
/// \brief Writes the data contained in a UsdData structure to a Sdf layer via the API of the
/// low-level SdfAbstractData.
USDFFUTILS_API bool
writeLayer(const WriteLayerOptions& options,
           UsdData& data,
           PXR_NS::SdfLayer* layer,
           PXR_NS::SdfAbstractDataRefPtr& sdfData,
           const std::string& sourceFileType,
           const std::string& debugTag,
           SetLayerDataFn setLayerDataFn = nullptr);
}
