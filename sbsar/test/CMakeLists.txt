include(GoogleTest)

add_executable(sbsarSanityTests sanityTests.cpp)

usd_plugin_compile_config(sbsarSanityTests)

target_link_libraries(sbsarSanityTests
PRIVATE
    ar
    usd
    usdSbsar
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME usdSbsarSanityTests COMMAND sbsarSanityTests)

set(_python_dirs ${Python3_RUNTIME_LIBRARY_DIRS})

if (NOT _python_dirs)
    set(_python_dirs ${Python_RUNTIME_LIBRARY_DIRS})
endif ()

if (WIN32)
    set_property(TEST usdSbsarSanityTests APPEND PROPERTY ENVIRONMENT "PATH=${_python_dirs}\\;${_usd_testing_root}\\lib\\;${_usd_testing_root}\\bin\\;$ENV{PATH}")
elseif (UNIX AND NOT APPLE)
    set_property(TEST usdSbsarSanityTests APPEND PROPERTY ENVIRONMENT "LD_LIBRARY_PATH=${_python_dirs}\\;${_usd_testing_root}/lib:${_usd_testing_root}/lib64")
endif ()
