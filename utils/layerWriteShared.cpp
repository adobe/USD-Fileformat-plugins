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
#include "layerWriteShared.h"

#include "common.h"

using namespace PXR_NS;

namespace adobe::usd {

const std::string stPrimvarNameAttrName = "stPrimvarName";

VtValue
getTextureZeroVtValue(const TfToken& channel)
{
    if (channel == AdobeTokens->r || channel == AdobeTokens->g || channel == AdobeTokens->b ||
        channel == AdobeTokens->a) {
        return VtValue(0.0f);
    } else if (channel == AdobeTokens->rgb) {
        return VtValue(GfVec3f(0.0f));
    } else if (channel == AdobeTokens->rgba) {
        return VtValue(GfVec4f(0.0f));
    } else {
        TF_WARN("getTextureZeroVtValue for unsupported channel %s", channel.GetText());
        return VtValue();
    }
}

std::string
createTexturePath(const std::string& srcAssetFilename, const std::string& imageUri)
{
    return srcAssetFilename.empty() ? imageUri : srcAssetFilename + "[" + imageUri + "]";
}

}
