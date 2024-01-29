include(GoogleTest)

add_executable(objSanityTests sanityTests.cpp)

usd_plugin_compile_config(objSanityTests)

target_link_libraries(objSanityTests
PRIVATE
    usd
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME usdObjSanityTests COMMAND objSanityTests)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/SanityCube.obj" "${CMAKE_CURRENT_BINARY_DIR}/SanityCube.obj" COPYONLY)

set(_python_dirs ${Python3_RUNTIME_LIBRARY_DIRS})

if (NOT _python_dirs)
    set(_python_dirs ${Python_RUNTIME_LIBRARY_DIRS})
endif ()

if (WIN32)
    set_property(TEST usdObjSanityTests APPEND PROPERTY ENVIRONMENT "PATH=${_python_dirs}\\;${_usd_testing_root}\\lib\\;${_usd_testing_root}\\bin\\;$ENV{PATH}")
elseif (UNIX AND NOT APPLE)
    set_property(TEST usdObjSanityTests APPEND PROPERTY ENVIRONMENT "LD_LIBRARY_PATH=${_python_dirs}:${_usd_testing_root}/lib:${_usd_testing_root}/lib64")
endif ()