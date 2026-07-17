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

#include <sandbox/api.h>

// Forward-declare SdfLayer handle types -- avoids pulling sdf/layer.h (and its boost/python
// transitive dependency) into consumers that only need the lightweight quarantine helpers. The
// typedef is identical to the one in sdf/layer.h, so including both in one TU is harmless.
#include <pxr/usd/sdf/declareHandles.h>
PXR_NAMESPACE_OPEN_SCOPE
SDF_DECLARE_HANDLES(SdfLayer);
PXR_NAMESPACE_CLOSE_SCOPE

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace adobe::usd::sandbox {

/// The inert URI scheme (including the `://` separator) that quarantined references are encoded
/// into. Resolved by BadAssetResolver (in the sandbox resolver plugin) to nothing. Defined here,
/// in the lowest layer, so both the quarantine helpers and the resolver share one definition.
inline constexpr std::string_view kBadAssetScheme = "BadAsset://";

/// Encode an attacker-controlled reference into an inert `BadAsset://<base64url>` quarantine URI.
/// Idempotent: if @p originalRef is already quarantined it is returned unchanged (no nesting on
/// re-import or round-trip). The encoded form is safe to surface in logs/usdcat/terminals.
USDSANDBOX_API
std::string
QuarantineReference(std::string_view originalRef);

/// True if @p assetPath is a `BadAsset://` quarantine URI.
USDSANDBOX_API
bool
IsQuarantined(std::string_view assetPath);

/// Decode a quarantine URI back to the original reference. Decode-only -- opens nothing.
///
/// Returns @c std::nullopt if @p badAssetUri is not a well-formed `BadAsset://` URI, if the
/// payload is not valid base64url, OR if the decoded bytes contain a NUL (never legitimate; this
/// closes a NUL-truncation allow-list bypass in host re-resolve code). The returned string is
/// attacker-controlled and may still hold other non-printing bytes -- callers MUST sanitize before
/// display and canonicalize + allow-list-check (on the exact bytes they open) before opening.
USDSANDBOX_API
std::optional<std::string>
RevealQuarantinedReference(std::string_view badAssetUri);

/// Collect every quarantined (`BadAsset://`) reference authored on @p layer, across both default
/// and time-sampled asset values. Returns the encoded URIs.
///
/// Reports only references a prior scrub already quarantined; it does not itself detect or
/// quarantine unsafe paths. On an un-scrubbed layer it returns only whatever `BadAsset://` values
/// happen to be present.
USDSANDBOX_API
std::vector<std::string>
CollectQuarantinedReferences(const PXR_NS::SdfLayerHandle& layer);

}
