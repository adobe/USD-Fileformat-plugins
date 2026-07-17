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
///   2. Native OpenPBR processing -- controls whether importers and exporters
///      use OpenPbrMaterial directly or go through the ASM conversion layer.
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
/// DEPRECATED: scheduled for removal. Set
/// USD_FILEFORMATS_NATIVE_OPENPBR_PROCESSING=1 to migrate; that flag forces
/// this setting off and USD_FILEFORMATS_WRITE_OPENPBR on automatically. Set
/// this setting to false to suppress the runtime deprecation warning without
/// migrating.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_WRITE_USDPREVIEWSURFACE;

/// When true, AdobeStandardMaterial (ASM) material networks are written.
/// DEPRECATED: scheduled for removal. Set
/// USD_FILEFORMATS_NATIVE_OPENPBR_PROCESSING=1 to migrate; that flag forces
/// this setting off and USD_FILEFORMATS_WRITE_OPENPBR on automatically. Set
/// this setting to false to suppress the runtime deprecation warning without
/// migrating.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_WRITE_ASM;

/// When true, OpenPBR / MaterialX material networks are written.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_WRITE_OPENPBR;

// ---------------------------------------------------------------------------
// Native OpenPBR processing
//
// These gate native OpenPBR code paths. Each flag should be specific enough
// that its name reflects what it affects.
// ---------------------------------------------------------------------------

/// Gates native OpenPBR processing code paths. When enabled, importers populate
/// OpenPbrMaterial directly and exporters read from it, bypassing the ASM
/// Material conversion layer. This is separate from the material model selection
/// above -- USD_FILEFORMATS_WRITE_OPENPBR controls *whether* OpenPBR is written,
/// while this flag controls *how* it is processed.
extern PXR_NS::TfEnvSetting<bool> USD_FILEFORMATS_NATIVE_OPENPBR_PROCESSING;

PXR_NAMESPACE_CLOSE_SCOPE

namespace adobe::usd {

/// Returns true when USD_FILEFORMATS_NATIVE_OPENPBR_PROCESSING is enabled.
/// Use this instead of isFeatureEnabled(USD_FILEFORMATS_NATIVE_OPENPBR_PROCESSING)
/// to avoid Windows DLL symbol export issues.
USDFFUTILS_API bool
isNativeOpenPbrProcessingEnabled();

/// Returns true when USD_FILEFORMATS_WRITE_ASM is enabled. Use this instead of
/// isFeatureEnabled(USD_FILEFORMATS_WRITE_ASM) to avoid Windows DLL symbol export
/// issues.
USDFFUTILS_API bool
isWriteAsmEnabled();

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
///
/// Each USD_FILEFORMATS_WRITE_* env setting is read directly and the four flags
/// are independent. A once-per-process TF_WARN is emitted for each deprecated
/// setting that resolves to true.
USDFFUTILS_API void
applyMaterialModelDefaults(bool& writeUsdPreviewSurface, bool& writeASM, bool& writeOpenPBR);

/// Emit TF_WARN on the first call per process for each of the deprecated
/// material-write env settings that resolves to true, directing users at the
/// per-flag opt-out (set the deprecated flag to 0) as the migration path.
/// Subsequent calls are no-ops. A setting that resolves to false never emits a
/// warning.
USDFFUTILS_API void
warnOnceOnDeprecatedMaterialSettings(bool writeASM, bool writeUsdPreviewSurface);

/// Test-only: reset the per-process "already warned" flags used by
/// warnOnceOnDeprecatedMaterialSettings so unit tests can exercise the
/// one-shot behavior repeatedly.
USDFFUTILS_API void
resetDeprecationWarningOnceFlagsForTesting();

}
