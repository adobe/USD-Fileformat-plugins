include(GoogleTest)

add_executable(gltfSanityTests sanityTests.cpp)

usd_plugin_compile_config(gltfSanityTests)

target_link_libraries(gltfSanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

gtest_add_tests(TARGET gltfSanityTests AUTO)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.gltf" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube.bin" "${CMAKE_CURRENT_BINARY_DIR}/Cube.bin" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube_BaseColor.png" "${CMAKE_CURRENT_BINARY_DIR}/Cube_BaseColor.png" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube_MetallicRoughness.png" "${CMAKE_CURRENT_BINARY_DIR}/Cube_MetallicRoughness.png" COPYONLY)
