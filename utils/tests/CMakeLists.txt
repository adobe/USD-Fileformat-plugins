include(GoogleTest)

add_executable(utilsTests tests.cpp)

usd_plugin_compile_config(utilsTests)

target_link_libraries(utilsTests
                      PRIVATE
                        sdf
                        usd
                        usdGeom
                        usdSkel
                        usdShade
                        GTest::gtest
                        GTest::gtest_main
                        fileformatUtils
)

add_test(NAME utilsTests
         COMMAND utilsTests
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
