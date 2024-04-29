include(GoogleTest)

add_executable(fbxSanityTests sanityTests.cpp)

usd_plugin_compile_config(fbxSanityTests)


target_link_libraries(fbxSanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

gtest_add_tests(TARGET fbxSanityTests AUTO)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.fbx" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.fbx" COPYONLY)
