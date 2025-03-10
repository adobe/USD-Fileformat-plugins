include(GoogleTest)

add_executable(spzSanityTests sanityTests.cpp)

usd_plugin_compile_config(spzSanityTests)

target_link_libraries(spzSanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

gtest_add_tests(TARGET spzSanityTests AUTO)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanitySplats.spz" "${CMAKE_CURRENT_BINARY_DIR}/SanitySplats.spz" COPYONLY)