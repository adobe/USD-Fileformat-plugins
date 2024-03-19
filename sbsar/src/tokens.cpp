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
//
#include "./tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdSbsarTokensType::UsdSbsarTokensType() :
    pngFormat("png", TfToken::Immortal),
    RES_1024x1024("10", TfToken::Immortal),
    RES_128x128("7", TfToken::Immortal),
    RES_16x16("4", TfToken::Immortal),
    RES_1x1("0", TfToken::Immortal),
    RES_2048x2048("11", TfToken::Immortal),
    RES_256x256("8", TfToken::Immortal),
    RES_2x2("1", TfToken::Immortal),
    RES_32x32("5", TfToken::Immortal),
    RES_4096x4096("12", TfToken::Immortal),
    RES_4x4("2", TfToken::Immortal),
    RES_512x512("9", TfToken::Immortal),
    RES_64x64("6", TfToken::Immortal),
    RES_8x8("3", TfToken::Immortal),
    sbsarFormat("sbsar", TfToken::Immortal),
    allTokens({
        pngFormat,
        RES_1024x1024,
        RES_128x128,
        RES_16x16,
        RES_1x1,
        RES_2048x2048,
        RES_256x256,
        RES_2x2,
        RES_32x32,
        RES_4096x4096,
        RES_4x4,
        RES_512x512,
        RES_64x64,
        RES_8x8,
        sbsarFormat
    })
{
}

TfStaticData<UsdSbsarTokensType> UsdSbsarTokens;

PXR_NAMESPACE_CLOSE_SCOPE
