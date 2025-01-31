include(GoogleTest)

add_executable(plySanityTests sanityTests.cpp)

usd_plugin_compile_config(plySanityTests)

target_link_libraries(plySanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

gtest_add_tests(TARGET plySanityTests AUTO)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.ply" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.ply" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/貝殻ビューア Colored Cube.ply" "${CMAKE_CURRENT_BINARY_DIR}/貝殻ビューア Colored Cube.ply" COPYONLY)