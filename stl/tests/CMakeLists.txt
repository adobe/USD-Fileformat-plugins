include(GoogleTest)

add_executable(stlSanityTests sanityTests.cpp)

usd_plugin_compile_config(stlSanityTests)

target_link_libraries(stlSanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

gtest_add_tests(TARGET stlSanityTests AUTO)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.stl" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.stl" COPYONLY)
