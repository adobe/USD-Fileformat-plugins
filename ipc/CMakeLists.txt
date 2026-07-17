set(TARGET_NAME usdIpc)
project(${TARGET_NAME})

if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()

set(IPC_COMMON_SOURCES
    "src/pipe.cpp"
    "src/factory.cpp"
    "src/sharedMemory.cpp"
)

set(IPC_COMMON_HEADERS
    "include/ipc/api.h"
    "include/ipc/platformConfig.h"
    "include/ipc/pipe.h"
    "include/ipc/sharedMemory.h"
    "include/ipc/process.h"
)

if(WIN32)
    set(PLATFORM_SOURCES
        "src/win/sharedMemoryWin.cpp"
        "src/win/processWin.cpp"
        "src/win/utilitiesWin.cpp"
    )
    set(PLATFORM_HEADERS
        "src/win/sharedMemoryWin.h"
        "src/win/processWin.h"
        "src/win/utilitiesWin.h"
    )
elseif(APPLE OR UNIX)
    set(PLATFORM_SOURCES
        "src/posix/sharedMemoryPosix.cpp"
        "src/posix/processPosix.cpp"
    )
    set(PLATFORM_HEADERS
        "src/posix/sharedMemoryPosix.h"
        "src/posix/processPosix.h"
    )
endif()

add_library(${TARGET_NAME} SHARED
    ${IPC_COMMON_SOURCES}
    ${IPC_COMMON_HEADERS}
    ${PLATFORM_SOURCES}
    ${PLATFORM_HEADERS}
)

target_compile_definitions(${TARGET_NAME} PRIVATE IPC_EXPORTS)

# Don't use windows.h min/max, which shadow the C++ versions
if(WIN32)
    target_compile_definitions(${TARGET_NAME} PUBLIC NOMINMAX)
endif()

target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(APPLE OR UNIX)
    set(CMAKE_MACOSX_RPATH TRUE)
endif()

set_target_properties(${TARGET_NAME} PROPERTIES
    INSTALL_RPATH "${CMAKE_INSTALL_RPATH}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        arch
        tf
)

if(UNIX AND NOT APPLE)
    # shm_open / shm_unlink live in librt on Linux (cf. old fix #1156).
    target_link_libraries(${TARGET_NAME} PUBLIC rt)
endif()

install(TARGETS ${TARGET_NAME})

if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
