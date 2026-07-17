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
#include <fileformatutils/naming.h>

#include <pxr/base/tf/unicodeUtils.h>

#include <cstdint>

using namespace PXR_NS;

namespace adobe::usd {

namespace {

// Encode a single TfUtf8CodePoint to UTF-8 bytes appended to `out`.
// USD does not ship a public encoder; this implements the standard UTF-8
// encoding rules. Retire when Pixar's tf_utf8_identifiers proposal lands an
// upstream equivalent. (File-local `_lowerCamel` per the utils/src convention,
// e.g. _writeMetadata / _readNormalScale.)
//
// Invariant: TfUtf8CodePointView only yields scalar values <= U+10FFFF
// (malformed input decodes to U+FFFD), so the four-byte branch is the widest
// case and never reads or writes out of range.
void
_appendUtf8CodePoint(std::string& out, const TfUtf8CodePoint codePoint)
{
    const uint32_t c = codePoint.AsUInt32();
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

} // anonymous namespace

std::string
MakeValidUsdIdentifier(const std::string& source)
{
    if (source.empty()) {
        return "_";
    }

    std::string result;
    result.reserve(source.size());

    TfUtf8CodePointView view(source);
    auto it = view.begin();
    const auto end = view.end();
    const auto underscore = TfUtf8CodePointFromAscii('_');

    // Defensive: a non-empty std::string always yields >=1 codepoint (invalid
    // bytes decode to U+FFFD), so this is belt-and-suspenders, but it lets the
    // reader stop reasoning about the view's non-empty guarantee before *it.
    if (it == end) {
        return "_";
    }

    // Position 0: must be XID_Start or '_'. The explicit underscore check is
    // required: '_' is not XID_Start, and omitting it would double a leading
    // '_' (since '_' is XID_Continue, the else-branch would prepend then append).
    if (TfIsUtf8CodePointXidStart(*it) || *it == underscore) {
        _appendUtf8CodePoint(result, *it);
    } else {
        result += '_';
        // A leading XID_Continue-but-not-XID_Start codepoint (e.g. a digit) is
        // preserved after the prepended '_'.
        if (TfIsUtf8CodePointXidContinue(*it)) {
            _appendUtf8CodePoint(result, *it);
        }
    }
    ++it;

    // Position N>0: each codepoint must be XID_Continue (which includes '_' via
    // the Pc category; non-XID codepoints, including spaces and punctuation,
    // collapse to '_').
    for (; it != end; ++it) {
        if (TfIsUtf8CodePointXidContinue(*it)) {
            _appendUtf8CodePoint(result, *it);
        } else {
            result += '_';
        }
    }

    return result;
}

} // namespace adobe::usd
