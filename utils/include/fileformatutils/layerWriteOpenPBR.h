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
#include "sdfMaterialUtils.h"
#include "usdData.h"

namespace adobe::usd {

USDFFUTILS_API void
writeOpenPBR(WriteSdfContext& ctx,
             const PXR_NS::SdfPath& materialPath,
             const OpenPbrMaterial& material,
             MaterialInputs& materialInputs);

// Ideally this function woulde be private, however because the sbsar format is using a pretty
// different implementation than those that use the above entry point, and we still want to share
// the implemenation of this function, we need to make it public.  In the future if the sbsar plugin
// goes through a refactor, we should consider making this private again.
USDFFUTILS_API PXR_NS::SdfPath
createMaterialXTextureReader(PXR_NS::SdfAbstractData* sdfData,
                             const PXR_NS::SdfPath& parentPath,
                             const PXR_NS::TfToken& name,
                             const Input& input,
                             const PXR_NS::SdfPath& uvResultPath,
                             const PXR_NS::SdfPath& textureConnection);

}
