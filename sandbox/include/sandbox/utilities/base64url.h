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

#include <optional>
#include <string>
#include <string_view>

namespace adobe::usd::sandbox::base64url {

/**
 * Encode arbitrary bytes as base64url (RFC 4648 §5): the URL- and filename-safe alphabet
 * (@c A-Z @c a-z @c 0-9 @c - @c _), with padding (@c =) stripped. The result contains none of
 * @c + @c / @c =, so it is safe to embed in a URI scheme, filename, log line, or terminal.
 *
 * @param bytes The raw (possibly attacker-controlled, possibly non-UTF-8) bytes to encode.
 *
 * @return The base64url text. Bytes are treated as @c unsigned, so values above 127 encode
 *         correctly regardless of the platform's @c char signedness.
 */
USDSANDBOX_API
std::string
encode(std::string_view bytes);

/**
 * Decode base64url text back to the original bytes. Total and non-throwing: any malformed input
 * yields @c std::nullopt rather than an exception or partial result.
 *
 * Accepts both padded and unpadded input. Rejects any character outside the base64url alphabet
 * (notably the standard-base64 @c + and @c /) and any length that is impossible for base64
 * (@c ≡1 (mod 4) after removing padding).
 *
 * @param text The base64url text to decode.
 *
 * @return The decoded bytes, or @c std::nullopt if @p text is not valid base64url.
 */
USDSANDBOX_API
std::optional<std::string>
decode(std::string_view text);

}
