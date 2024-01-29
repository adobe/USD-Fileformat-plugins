include(GoogleTest)

add_executable(plySanityTests sanityTests.cpp)

usd_plugin_compile_config(plySanityTests)

target_link_libraries(plySanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

if(USD_FILEFORMATS_BUILD_TESTS)
    add_test(NAME usdPlySanityTests COMMAND plySanityTests)
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.ply" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.ply" COPYONLY)

set(_python_dirs ${Python3_RUNTIME_LIBRARY_DIRS})

if (NOT _python_dirs)
    set(_python_dirs ${Python_RUNTIME_LIBRARY_DIRS})
endif ()

if (WIN32)
    set_property(TEST usdPlySanityTests APPEND PROPERTY ENVIRONMENT "PATH=${_python_dirs}\\;${_usd_testing_root}\\lib\\\;${_usd_testing_root}\\bin\\\;$ENV{PATH}")
elseif (UNIX AND NOT APPLE)
    set_property(TEST usdPlySanityTests APPEND PROPERTY ENVIRONMENT "LD_LIBRARY_PATH=${_python_dirs}:${_usd_testing_root}/lib:${_usd_testing_root}/lib64")
endif ()