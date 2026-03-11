/*
Copyright 2026 Adobe. All rights reserved.
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

#include <pxr/base/tf/envSetting.h>

/// Runtime feature flags for USD file format plugins.
///
/// These use OpenUSD's TfEnvSetting to allow toggling behavior at runtime via
/// environment variables, without requiring a separate branch or recompilation.
///
/// There are two categories of flags:
///
///   1. Material model configuration -- controls which material representations
///      are written by default. These are not experimental; they are stable
///      configuration knobs.
///
///   2. Experimental feature flags -- gates for in-development code paths that
///      are not yet ready for production use.
///
/// === Adding a new feature flag ===
///
/// 1. Declare the flag in this header (inside PXR_NAMESPACE):
///
///     extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_MY_FLAG;
///
/// 2. Define the flag in featureFlags.cpp:
///
///     TF_DEFINE_ENV_SETTING(USD_FILEFORMATS_MY_FLAG, false,
///                           "Description of what this flag controls");
///
/// 3. Use the flag in plugin code:
///
///     #include <fileformatutils/featureFlags.h>
///
///     if (adobe::usd::isFeatureEnabled(USD_FILEFORMATS_MY_FLAG)) {
///         // alternative code path
///     }
///
/// === Activating at runtime ===
///
///     export USD_FILEFORMATS_MY_FLAG=1
///
/// Or via PIXAR_TF_ENV_SETTING_FILE (see TfEnvSetting docs).

PXR_NAMESPACE_OPEN_SCOPE

// ---------------------------------------------------------------------------
// Material model configuration
//
// These flags control which material representations are written by default.
// They are fully independent -- any combination is valid (0=off, 1=on).
// File format arguments (e.g. "writeOpenPBR=true") still override these
// defaults on a per-file basis.
// ---------------------------------------------------------------------------

/// When true, UsdPreviewSurface material networks are written.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_WRITE_USDPREVIEWSURFACE;

/// When true, AdobeStandardMaterial (ASM) material networks are written.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_WRITE_ASM;

/// When true, OpenPBR / MaterialX material networks are written.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_WRITE_OPENPBR;

// ---------------------------------------------------------------------------
// Experimental feature flags
//
// These gate in-development code paths. Each flag should be specific enough
// that its name reflects what it affects.
// ---------------------------------------------------------------------------

/// Gates experimental OpenPBR processing code paths (e.g. new writer/reader
/// logic that is not yet production-ready). This is separate from the material
/// model selection above -- USD_FILEFORMATS_WRITE_OPENPBR controls *whether*
/// OpenPBR is written, while this flag controls *how* it is processed.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_EXPERIMENTAL_OPENPBR_PROCESSING;

PXR_NAMESPACE_CLOSE_SCOPE

namespace adobe::usd {

/// Query whether a specific feature flag is enabled.
/// Wraps TfGetEnvSetting for a consistent, readable call site.
template<class T>
inline T
isFeatureEnabled(PXR_NS::TfEnvSetting<T>& setting)
{
    return PXR_NS::TfGetEnvSetting(setting);
}

/// Apply material model defaults from environment variables to the three
/// write-material booleans. Call this in the default constructor of any struct
/// that carries writeUsdPreviewSurface / writeASM / writeOpenPBR fields.
USDFFUTILS_API void
applyMaterialModelDefaults(bool& writeUsdPreviewSurface, bool& writeASM, bool& writeOpenPBR);

}
