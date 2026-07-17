include(GoogleTest)

add_executable(gltfSanityTests sanityTests.cpp)

usd_plugin_compile_config(gltfSanityTests)

target_link_libraries(gltfSanityTests
PRIVATE
    usd
    usdShade
    GTest::gtest
    GTest::gtest_main
    fileformatUtils
    gtestCommon
    nlohmann_json::nlohmann_json
)

gtest_add_tests(TARGET gltfSanityTests AUTO)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.gltf" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube.bin" "${CMAKE_CURRENT_BINARY_DIR}/Cube.bin" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube_BaseColor.png" "${CMAKE_CURRENT_BINARY_DIR}/Cube_BaseColor.png" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube_MetallicRoughness.png" "${CMAKE_CURRENT_BINARY_DIR}/Cube_MetallicRoughness.png" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/TransmissionThinWalled.gltf" "${CMAKE_CURRENT_BINARY_DIR}/TransmissionThinWalled.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/TransmissionVolumetric.gltf" "${CMAKE_CURRENT_BINARY_DIR}/TransmissionVolumetric.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtCoat.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtCoat.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtDiffuseRoughness.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtDiffuseRoughness.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtDispersion.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtDispersion.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtFuzz.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtFuzz.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtIridescence.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtIridescence.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtCoatSimple.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtCoatSimple.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtVolumeScatterTransmission.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtVolumeScatterTransmission.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ExtVolumeScatterDiffuseTransmission.gltf" "${CMAKE_CURRENT_BINARY_DIR}/ExtVolumeScatterDiffuseTransmission.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/openpbr_base_weight.usda" "${CMAKE_CURRENT_BINARY_DIR}/openpbr_base_weight.usda" COPYONLY)
