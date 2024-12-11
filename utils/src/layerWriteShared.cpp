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
#include <fileformatutils/layerWriteShared.h>

#include <fileformatutils/common.h>
#include <algorithm>
#include <vector>

using namespace PXR_NS;

namespace adobe::usd {

std::string
getSTPrimvarAttrName(int uvIndex)
{
    static std::string stPrimvarName = "stPrimvarName";
    if (uvIndex < 0) {
        TF_WARN("Invalid uvIndex for stPrimvarName %d", uvIndex);
        return stPrimvarName;
    }
    if (uvIndex == 0)
        return stPrimvarName;
    return stPrimvarName + std::to_string(uvIndex);
}

int
parseIntEnding(const std::string& str)
{
    if (str.empty())
        return -1;
    try {
        std::size_t pos{};
        const int i{ std::stoi(str, &pos) };
        if (pos == str.size() && i >= 0) {
            return i;
        }
    } catch (const std::out_of_range&) {
        return -1;
    }
    return -1;
}

// If the token string starts with "st", check if the characters following it can be converted to a
// non-zero int. This is essentially looking for tokens: st, st0, st1, st2, st3, ...
// (note that st and st0 are considered equivalent)
// The number value is returned or -1 if there isn't a pattern match.
int
getSTPrimvarTokenIndex(TfToken token)
{
    std::string const& str = token.GetString();
    if (str.compare(0, 2, "st") == 0) {
        if (str.size() == 2)
            return 0;
        return parseIntEnding(str.substr(2));
    }
    return -1;
}

// return a token with "st" for uvIndex==0, "st1" for uvIndex==1, "st2" for uvIndex==2, ...
TfToken
getSTPrimvarAttrToken(int uvIndex)
{
    if (uvIndex < 0) {
        TF_WARN("Invalid uvIndex [%d] for st primvar: ", uvIndex);
        return TfToken();
    }

    if (uvIndex <= 0)
        return AdobeTokens->st;
    return TfToken(AdobeTokens->st.GetString() + std::to_string(uvIndex));
}

// return a token with "texCoordReader" for uvIndex==0, "texCoordReader1" for uvIndex==1,
// "texCoordReader2" for uvIndex==2, ...
TfToken
getSTTexCoordReaderToken(int uvIndex)
{
    if (uvIndex < 0) {
        TF_WARN("Invalid uvIndex [%d] for texCoordReader", uvIndex);
        return TfToken();
    }
    if (uvIndex == 0)
        return AdobeTokens->texCoordReader;
    return TfToken(AdobeTokens->texCoordReader.GetString() + std::to_string(uvIndex));
}

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
