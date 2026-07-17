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

#include <gtest/gtest.h>

#include <string>

using adobe::usd::sandbox::base64url::decode;
using adobe::usd::sandbox::base64url::encode;

TEST(Base64Url, RoundTripsArbitraryBytes)
{
    for (const std::string& s : {
           std::string("/etc/passwd"),
           std::string("../../../secret"),
           std::string("http://169.254.169.254/latest/meta-data"),
           std::string("\\\\host\\share\\x"),
           std::string("\0\x01\xff embedded", 12),
           // High bytes (>127) are where a signed-char encoder bug shows up -- load-bearing.
           std::string("/Users/Jose\xCC\x81/textures/cafe\xCC\x81.png"), // accented Latin (NFD)
           std::string(
             "/\xE8\xB5\x84\xE4\xBA\xA7/\xE7\xBA\xB9\xE7\x90\x86.png"), // CJK 资产/纹理.png
           std::string("my textures/rock (1).png"),                     // spaces + parens
         }) {
        const std::string enc = encode(s);
        EXPECT_EQ(enc.find_first_of("+/="), std::string::npos); // url-safe, unpadded
        ASSERT_TRUE(decode(enc).has_value());
        EXPECT_EQ(*decode(enc), s);
    }
}

TEST(Base64Url, RejectsInvalidInput)
{
    EXPECT_FALSE(decode("!!!!").has_value()); // non-alphabet
    EXPECT_FALSE(decode("A").has_value());    // impossible length (1 mod 4)
}

TEST(Base64Url, KnownVector)
{
    EXPECT_EQ(encode("/etc/passwd"), "L2V0Yy9wYXNzd2Q");
}

TEST(Base64Url, EmptyAndShortRoundTrip)
{
    // Empty input encodes to empty and decodes back to empty (not nullopt).
    EXPECT_EQ(encode(""), "");
    ASSERT_TRUE(decode("").has_value());
    EXPECT_EQ(*decode(""), "");

    // 1-, 2-, and 3-byte inputs round-trip (the partial-group tails of the encoder).
    for (const std::string& s : { std::string("a"), std::string("ab"), std::string("abc") }) {
        const std::string enc = encode(s);
        EXPECT_EQ(enc.find_first_of("+/="), std::string::npos);
        ASSERT_TRUE(decode(enc).has_value()) << "failed to decode: " << enc;
        EXPECT_EQ(*decode(enc), s);
    }
}
