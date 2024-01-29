include(GoogleTest)

add_executable(gltfSanityTests sanityTests.cpp)

usd_plugin_compile_config(gltfSanityTests)

target_link_libraries(gltfSanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME usdGlTFSanityTests COMMAND gltfSanityTests)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.gltf" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.gltf" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube.bin" "${CMAKE_CURRENT_BINARY_DIR}/Cube.bin" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube_BaseColor.png" "${CMAKE_CURRENT_BINARY_DIR}/Cube_BaseColor.png" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Cube_MetallicRoughness.png" "${CMAKE_CURRENT_BINARY_DIR}/Cube_MetallicRoughness.png" COPYONLY)

set(_python_dirs ${Python3_RUNTIME_LIBRARY_DIRS})

if (NOT _python_dirs)
    set(_python_dirs ${Python_RUNTIME_LIBRARY_DIRS})
endif ()

if (WIN32)
    set_property(TEST usdGlTFSanityTests APPEND PROPERTY ENVIRONMENT "PATH=${_python_dirs}\\;${_usd_testing_root}\\lib\\;${_usd_testing_root}\\bin\\;$ENV{PATH}")
elseif (UNIX AND NOT APPLE)
    set_property(TEST usdGlTFSanityTests APPEND PROPERTY ENVIRONMENT "LD_LIBRARY_PATH=${_python_dirs}:${_usd_testing_root}/lib:${_usd_testing_root}/lib64")
endif ()