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
#include <pxr/usd/sdf/abstractData.h>

namespace adobe::usd {

struct WriteLayerOptions
{
    bool pruneJoints = false;
    bool writeMaterialX = false;
    std::string assetsPath;
    bool createRenderSettingsPrim = false;
    bool animationTracks = false;
};

struct WriteSdfContext
{
    const WriteLayerOptions* options;
    PXR_NS::SdfAbstractData* sdfData;
    const UsdData* usdData;

    PXR_NS::SdfPathVector nodeMap;
    PXR_NS::SdfPathVector materialMap;
    PXR_NS::SdfPathVector skeletonMap;
    PXR_NS::SdfPathVector meshPrototypeMap;
    PXR_NS::SdfPathVector lightMap;

    std::string srcAssetFilename;
    std::string debugTag;
};

USDFFUTILS_API std::string
getSTPrimvarAttrName(int uvIndex);

USDFFUTILS_API int
parseIntEnding(const std::string& str);

USDFFUTILS_API int
getSTPrimvarTokenIndex(PXR_NS::TfToken token);

USDFFUTILS_API PXR_NS::TfToken
getSTPrimvarAttrToken(int uvIndex);

USDFFUTILS_API PXR_NS::TfToken
getSTTexCoordReaderToken(int uvIndex);

USDFFUTILS_API PXR_NS::VtValue
getTextureZeroVtValue(const PXR_NS::TfToken& channel);

USDFFUTILS_API std::string
createTexturePath(const std::string& srcAssetFilename, const std::string& imageUri);

}
