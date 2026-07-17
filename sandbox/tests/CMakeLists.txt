find_package(GTest REQUIRED)
include(GoogleTest)
FIND_PACKAGE(Threads REQUIRED)

set(TARGET_NAME sandboxUtilsTests)

add_executable(sandboxUtilsTests testPluginPath.cpp main.cpp)

target_include_directories(sandboxUtilsTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${PXR_INCLUDE_DIRS}"
)

target_link_libraries(sandboxUtilsTests PRIVATE
    GTest::gtest
    sandboxUtils
    sandboxProtocol
    tf
    sdf
    usd
    arch
)
add_test(NAME sandboxUtilsTests COMMAND sandboxUtilsTests)

# testHardening.cpp is skipped from the public mirror sync (internal hardening
# validation, not part of the public test harness), so guard this target: a
# CMakeLists.txt shared with the public repo must still configure cleanly
# there even though the source file it references doesn't exist.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testHardening.cpp")
    add_executable(hardeningTests testHardening.cpp)

    target_include_directories(hardeningTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../include"
            "${PXR_INCLUDE_DIRS}"
    )

    # Windows: testHardening.cpp provides its own main() (needed to intercept
    # --apply-restrictions-and-report before GoogleTest runs), so link gtest
    # (not gtest_main). On all other platforms, gtest_main supplies main().
    if(WIN32)
        target_link_libraries(hardeningTests PRIVATE
            GTest::gtest
            sandboxHardening
        )
    else()
        target_link_libraries(hardeningTests PRIVATE
            GTest::gtest_main
            sandboxHardening
        )
    endif()

    add_test(NAME hardeningTests COMMAND hardeningTests)
endif()

add_executable(protocolTests testProtocol.cpp)

target_include_directories(protocolTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${PXR_INCLUDE_DIRS}"
)

target_link_libraries(protocolTests PRIVATE
    GTest::gtest_main
    sandboxProtocol
    usdSerialization
)

add_test(NAME protocolTests COMMAND protocolTests)

add_executable(base64UrlTests testBase64url.cpp)

target_include_directories(base64UrlTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${PXR_INCLUDE_DIRS}"
)

target_link_libraries(base64UrlTests PRIVATE
    GTest::gtest_main
    sandboxUtils
)

add_test(NAME base64UrlTests COMMAND base64UrlTests)

add_executable(utilitiesTests testUtilities.cpp)

target_include_directories(utilitiesTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${PXR_INCLUDE_DIRS}"
)

target_link_libraries(utilitiesTests PRIVATE
    GTest::gtest_main
    sandboxUtils
    sdf
    usd
    tf
)

add_test(NAME utilitiesTests COMMAND utilitiesTests)

# The BadAsset:// scheme is discovered through the USD plugin registry (PXR_PLUGINPATH_NAME),
# so this executable does not link usdInMemResolver directly -- linking the build-tree resolver
# dylib while the registry loads the installed copy would double-register the resolver types.
# The quarantine helpers under test live in sandboxUtils (a plain utility lib, not a Plug-loaded
# resolver plugin), so linking it here is safe.
add_executable(badAssetResolverTests testBadAssetResolver.cpp)

target_include_directories(badAssetResolverTests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${PXR_INCLUDE_DIRS}"
)

target_link_libraries(badAssetResolverTests PRIVATE
    GTest::gtest_main
    sandboxUtils
    ar
    usd
    sdf
    tf
)

add_test(NAME badAssetResolverTests COMMAND badAssetResolverTests)
