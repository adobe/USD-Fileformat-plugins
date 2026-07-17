set(TARGET_NAME usdSerialization)
project(${TARGET_NAME})

if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()

add_library(${TARGET_NAME} SHARED
    "src/buffer.cpp"
    "include/serialization/api.h"
    "include/serialization/buffer.h"
)

target_compile_definitions(${TARGET_NAME} PRIVATE SERIALIZATION_EXPORTS)

target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        arch
)

set_target_properties(${TARGET_NAME} PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_RPATH}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS ${TARGET_NAME})

if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
