find_package(GTest REQUIRED)
include(GoogleTest)

set(TARGET_NAME serializationLibTests)

add_executable(${TARGET_NAME}
    testBuffer.cpp
)

target_link_libraries(${TARGET_NAME} PRIVATE
    GTest::gtest
    GTest::gtest_main
    usdSerialization
)

set_target_properties(${TARGET_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
