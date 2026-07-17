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

#include <sandbox/utilities/quarantine.h>

#include <sandbox/utilities/base64url.h>
#include <sandbox/utilities/utilities.h>

#include <pxr/usd/sdf/layer.h>

using namespace PXR_NS;

namespace adobe::usd::sandbox {

bool
IsQuarantined(std::string_view assetPath)
{
    return assetPath.substr(0, kBadAssetScheme.size()) == kBadAssetScheme;
}

std::string
QuarantineReference(std::string_view originalRef)
{
    // Idempotent: never nest a quarantine URI inside another on re-import or round-trip.
    if (IsQuarantined(originalRef)) {
        return std::string(originalRef);
    }
    return std::string(kBadAssetScheme) + base64url::encode(originalRef);
}

std::optional<std::string>
RevealQuarantinedReference(std::string_view badAssetUri)
{
    if (!IsQuarantined(badAssetUri)) {
        return std::nullopt;
    }
    std::optional<std::string> decoded =
      base64url::decode(badAssetUri.substr(kBadAssetScheme.size()));
    if (!decoded) {
        return std::nullopt;
    }
    // A NUL byte is never legitimate in a reference; rejecting it here closes a NUL-truncation
    // allow-list bypass in host re-resolve code (which may treat the decoded bytes as a C string).
    if (decoded->find('\0') != std::string::npos) {
        return std::nullopt;
    }
    return decoded;
}

std::vector<std::string>
CollectQuarantinedReferences(const SdfLayerHandle& layer)
{
    std::vector<std::string> quarantined;
    if (!layer) {
        return quarantined;
    }

    // FindAssetPaths visits both default and time-sampled (animated) asset values, so a
    // quarantined reference hidden in an animated attribute is still collected.
    const SdfLayerRefPtr refLayer(&(*layer));
    for (const auto& [authoredPath, resolvedPath] : FindAssetPaths(refLayer)) {
        if (IsQuarantined(authoredPath)) {
            quarantined.push_back(authoredPath);
        }
    }
    return quarantined;
}

}
