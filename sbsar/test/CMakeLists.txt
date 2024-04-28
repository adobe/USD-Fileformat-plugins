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

gtest_add_tests(TARGET sbsarSanityTests AUTO)
