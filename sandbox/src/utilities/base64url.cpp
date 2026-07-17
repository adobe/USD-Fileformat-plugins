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

#include <sandbox/utilities/base64url.h>

namespace adobe::usd::sandbox::base64url {

namespace {

// RFC 4648 §5 URL- and filename-safe alphabet: indices 0-63 map to the encoded character.
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Map an encoded character back to its 6-bit value, or -1 if it is not in the alphabet. The
// standard-base64 characters '+' and '/' are deliberately not in the alphabet and so are rejected.
int
sextet(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

}

std::string
encode(std::string_view bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);

    // Bytes are read as unsigned so values above 127 shift correctly regardless of char signedness.
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t n = bytes.size();

    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        const unsigned b0 = data[i], b1 = data[i + 1], b2 = data[i + 2];
        out += kAlphabet[b0 >> 2];
        out += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
        out += kAlphabet[((b1 & 0x0f) << 2) | (b2 >> 6)];
        out += kAlphabet[b2 & 0x3f];
    }

    // Trailing partial group: emit only the characters that carry data (padding is stripped).
    if (const size_t rem = n - i; rem == 1) {
        const unsigned b0 = data[i];
        out += kAlphabet[b0 >> 2];
        out += kAlphabet[(b0 & 0x03) << 4];
    } else if (rem == 2) {
        const unsigned b0 = data[i], b1 = data[i + 1];
        out += kAlphabet[b0 >> 2];
        out += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
        out += kAlphabet[(b1 & 0x0f) << 2];
    }

    return out;
}

std::optional<std::string>
decode(std::string_view text)
{
    // Accept padded or unpadded input: strip any trailing '=' first. A '=' anywhere else is not
    // in the alphabet and is rejected below.
    size_t len = text.size();
    while (len > 0 && text[len - 1] == '=')
        --len;
    const std::string_view s = text.substr(0, len);

    // A length of 1 (mod 4) cannot be produced by base64: there are no leftover bits for a byte.
    if (s.size() % 4 == 1)
        return std::nullopt;

    std::string out;
    out.reserve(s.size() / 4 * 3 + 2);

    size_t i = 0;
    for (; i + 4 <= s.size(); i += 4) {
        const int c0 = sextet(s[i]), c1 = sextet(s[i + 1]), c2 = sextet(s[i + 2]),
                  c3 = sextet(s[i + 3]);
        if ((c0 | c1 | c2 | c3) < 0) // any -1 sets the sign bit of the OR
            return std::nullopt;
        out += static_cast<char>((c0 << 2) | (c1 >> 4));
        out += static_cast<char>(((c1 & 0x0f) << 4) | (c2 >> 2));
        out += static_cast<char>(((c2 & 0x03) << 6) | c3);
    }

    if (const size_t rem = s.size() - i; rem == 2) {
        const int c0 = sextet(s[i]), c1 = sextet(s[i + 1]);
        if ((c0 | c1) < 0)
            return std::nullopt;
        out += static_cast<char>((c0 << 2) | (c1 >> 4));
    } else if (rem == 3) {
        const int c0 = sextet(s[i]), c1 = sextet(s[i + 1]), c2 = sextet(s[i + 2]);
        if ((c0 | c1 | c2) < 0)
            return std::nullopt;
        out += static_cast<char>((c0 << 2) | (c1 >> 4));
        out += static_cast<char>(((c1 & 0x0f) << 4) | (c2 >> 2));
    }

    return out;
}

}
