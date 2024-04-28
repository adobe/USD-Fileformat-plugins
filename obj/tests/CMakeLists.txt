include(GoogleTest)

add_executable(objSanityTests sanityTests.cpp)

usd_plugin_compile_config(objSanityTests)

target_link_libraries(objSanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

gtest_add_tests(TARGET objSanityTests AUTO)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.obj" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.obj" COPYONLY)
