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

#include <string>

namespace adobe::usd {

/// Produce a USD-valid prim identifier from an arbitrary UTF-8 source string.
///
/// Preserves Unicode characters that fall in XID_Start / XID_Continue (UAX #31);
/// replaces non-XID codepoints (and malformed UTF-8, which decodes to U+FFFD) with '_'.
/// Empty input returns "_". A leading codepoint that is XID_Continue but not
/// XID_Start (e.g. a digit) is preserved after a prepended '_'.
///
/// Examples:
///   "Object_n3d#" -> "Object_n3d_"
///   "京都 Building" -> "京都_Building"
///   "Müller café"  -> "Müller_café"
///   "2024_render"  -> "_2024_render"
///   "🎨 art"        -> "__art"
///   ""             -> "_"
///
/// Bridge utility: replace with the upstream sanitizer when Pixar's
/// tf_utf8_identifiers proposal lands in USD core.
///
/// Uniqueness suffixing (the existing _001/_002 collision logic) is a separate
/// concern; sanitize first, then uniquify.
USDFFUTILS_API std::string
MakeValidUsdIdentifier(const std::string& source);

} // namespace adobe::usd
