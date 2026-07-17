/*
Copyright 2025 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#include <sandbox/platformConfig.h>
#include <sandbox/utilities/utilities.h>

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace {
// Mirror the platform separator used inside BuildNewPluginPath so test expectations match
// exactly on both Windows and POSIX builds.
#if SANDBOX_IS_WINDOWS
const std::string SEP = ";";
#else
const std::string SEP = ":";
#endif
}

using adobe::usd::sandbox::BuildNewPluginPath;
using adobe::usd::sandbox::ConsumeBoolArg;
// params: currentPxrPluginPath, proxyPluginPath, unsafePluginRoot

// When the current PXR_PLUGINPATH_NAME is empty, return unsafePluginRoot directly.
TEST(BuildNewPluginPath, EmptyCurrentPath)
{
    ASSERT_EQ(BuildNewPluginPath("", "/proxy/dir", "/unsafe/root"), "/unsafe/root");
}

// When proxyPluginPath is empty we don't know how the proxy plugin was referenced; to be safe the
// whole variable will be replaced entirely to prevent the sandbox from finding the proxy plugin.
TEST(BuildNewPluginPath, EmptyProxyPath)
{
    const std::string current = "/other/a" + SEP + "/other/b";
    ASSERT_EQ(BuildNewPluginPath(current, "", "/unsafe/root"), "/unsafe/root");
}

// Current path contains only the proxy entry — result is just unsafePluginRoot.
TEST(BuildNewPluginPath, ProxyFoundSingleEntry)
{
    ASSERT_EQ(BuildNewPluginPath("/proxy/dir", "/proxy/dir", "/unsafe/root"), "/unsafe/root");
}

// Proxy is the first of multiple entries — replaced in-place, order preserved.
TEST(BuildNewPluginPath, ProxyFoundAtBeginning)
{
    const std::string current = "/proxy/dir" + SEP + "/other/a" + SEP + "/other/b";
    const std::string expected = "/unsafe/root" + SEP + "/other/a" + SEP + "/other/b";
    ASSERT_EQ(BuildNewPluginPath(current, "/proxy/dir", "/unsafe/root"), expected);
}

// Proxy is in the middle of three entries — replaced in-place, flanking entries unchanged.
TEST(BuildNewPluginPath, ProxyFoundInMiddle)
{
    const std::string current = "/other/a" + SEP + "/proxy/dir" + SEP + "/other/b";
    const std::string expected = "/other/a" + SEP + "/unsafe/root" + SEP + "/other/b";
    ASSERT_EQ(BuildNewPluginPath(current, "/proxy/dir", "/unsafe/root"), expected);
}

// Proxy is the last of multiple entries — replaced in-place.
TEST(BuildNewPluginPath, ProxyFoundAtEnd)
{
    const std::string current = "/other/a" + SEP + "/other/b" + SEP + "/proxy/dir";
    const std::string expected = "/other/a" + SEP + "/other/b" + SEP + "/unsafe/root";
    ASSERT_EQ(BuildNewPluginPath(current, "/proxy/dir", "/unsafe/root"), expected);
}

// Proxy appears twice in the path — both occurrences are replaced.
TEST(BuildNewPluginPath, ProxyDuplicateEntries)
{
    const std::string current = "/proxy/dir" + SEP + "/other/a" + SEP + "/proxy/dir";
    const std::string expected = "/unsafe/root" + SEP + "/other/a" + SEP + "/unsafe/root";
    ASSERT_EQ(BuildNewPluginPath(current, "/proxy/dir", "/unsafe/root"), expected);
}

// Current path has a single entry that does not match the proxy — unsafePluginRoot is prepended.
TEST(BuildNewPluginPath, ProxyNotFoundSingleEntry)
{
    const std::string expected = "/unsafe/root" + SEP + "/other/a";
    ASSERT_EQ(BuildNewPluginPath("/other/a", "/proxy/dir", "/unsafe/root"), expected);
}

// Multiple existing entries, none matching the proxy — unsafePluginRoot prepended, rest preserved.
TEST(BuildNewPluginPath, ProxyNotFoundMultipleEntries)
{
    const std::string current = "/other/a" + SEP + "/other/b" + SEP + "/other/c";
    const std::string expected =
      "/unsafe/root" + SEP + "/other/a" + SEP + "/other/b" + SEP + "/other/c";
    ASSERT_EQ(BuildNewPluginPath(current, "/proxy/dir", "/unsafe/root"), expected);
}

// Verify that the platform-specific separator character is used to join the result. This test
// constructs input using SEP and checks the output uses SEP as well, locking in the behavior on
// both Windows (";") and POSIX (":") builds.
TEST(BuildNewPluginPath, PlatformSeparatorUsed)
{
    const std::string current = "/entry/a" + SEP + "/proxy/dir" + SEP + "/entry/b";
    const std::string result = BuildNewPluginPath(current, "/proxy/dir", "/unsafe/root");

    // The result must contain SEP and must NOT contain the other platform's separator.
    ASSERT_NE(result.find(SEP), std::string::npos);
#if SANDBOX_IS_WINDOWS
    ASSERT_EQ(result.find(':'), std::string::npos);
#else
    ASSERT_EQ(result.find(';'), std::string::npos);
#endif
}

// Only the exact value "true" is truthy; the key is always erased so a consumed host-only argument
// is not forwarded to the sandboxed worker, while unrelated entries are left intact.
TEST(ConsumeBoolArg, ParsesAndErases)
{
    std::map<std::string, std::string> args = {
        { "yes", "true" }, { "no", "false" }, { "caps", "TRUE" }, { "keep", "x" }
    };
    EXPECT_TRUE(ConsumeBoolArg(args, "yes"));
    EXPECT_FALSE(ConsumeBoolArg(args, "no"));
    EXPECT_FALSE(ConsumeBoolArg(args, "caps"));   // only lowercase "true" is truthy
    EXPECT_FALSE(ConsumeBoolArg(args, "absent")); // missing key yields false
    EXPECT_EQ(args.count("yes"), 0u);             // consumed keys are erased
    EXPECT_EQ(args.count("no"), 0u);
    EXPECT_EQ(args.count("caps"), 0u);
    EXPECT_EQ(args.count("keep"), 1u); // unrelated keys remain
}
