find_package(GTest REQUIRED)
include(GoogleTest)

set(TARGET_NAME ipcTests)

add_executable(${TARGET_NAME}
    testPipe.cpp
    testSharedMemory.cpp
    testProcess.cpp
)

target_link_libraries(${TARGET_NAME} PRIVATE
    GTest::gtest
    GTest::gtest_main
    usdIpc
)

set_target_properties(${TARGET_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
