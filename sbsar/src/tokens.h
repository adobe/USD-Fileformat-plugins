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
#ifndef USDSBSAR_TOKENS_H
#define USDSBSAR_TOKENS_H

/// \file usdSbsar/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class UsdSbsarTokensType
///
/// \link UsdSbsarTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// UsdSbsarTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use UsdSbsarTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(UsdSbsarTokens->pngFormat);
/// \endcode
struct UsdSbsarTokensType {
    USDSBSAR_API UsdSbsarTokensType();
    /// \brief "png"
    /// 
    ///  Png extension name 
    const TfToken pngFormat;
    /// \brief "10"
    /// 
    /// Image resolution of 1024x1024 pixels
    const TfToken RES_1024x1024;
    /// \brief "7"
    /// 
    /// Image resolution of 128x128 pixels
    const TfToken RES_128x128;
    /// \brief "4"
    /// 
    /// Image resolution of 16x16 pixels
    const TfToken RES_16x16;
    /// \brief "0"
    /// 
    /// Image resolution of 1x1 pixels
    const TfToken RES_1x1;
    /// \brief "11"
    /// 
    /// Image resolution of 2048x2048 pixels
    const TfToken RES_2048x2048;
    /// \brief "8"
    /// 
    /// Image resolution of 256x256 pixels
    const TfToken RES_256x256;
    /// \brief "1"
    /// 
    /// Image resolution of 2x2 pixels
    const TfToken RES_2x2;
    /// \brief "5"
    /// 
    /// Image resolution of 32x32 pixels
    const TfToken RES_32x32;
    /// \brief "12"
    /// 
    /// Image resolution of 4096x4096 pixels 
    const TfToken RES_4096x4096;
    /// \brief "2"
    /// 
    /// Image resolution of 4x4 pixels
    const TfToken RES_4x4;
    /// \brief "9"
    /// 
    /// Image resolution of 512x512 pixels
    const TfToken RES_512x512;
    /// \brief "6"
    /// 
    /// Image resolution of 64x64 pixels
    const TfToken RES_64x64;
    /// \brief "3"
    /// 
    /// Image resolution of 8x8 pixels
    const TfToken RES_8x8;
    /// \brief "sbsar"
    /// 
    ///  Sbsar extension name 
    const TfToken sbsarFormat;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var UsdSbsarTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa UsdSbsarTokensType
extern USDSBSAR_API TfStaticData<UsdSbsarTokensType> UsdSbsarTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
