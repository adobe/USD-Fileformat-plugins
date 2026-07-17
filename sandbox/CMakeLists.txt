option(USD_SANDBOXPROXY_ENABLE_INSTALL "Enable installation of plugin artifacts" ON)
set(TARGET_NAME usdSandbox)
project(${TARGET_NAME})

if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()

find_package(Boost REQUIRED)
find_package(Python REQUIRED)

if (APPLE OR UNIX)
    set(CMAKE_MACOSX_RPATH TRUE)
    # Process LD_LIBRARY_PATH if it's defined and not empty
    if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
        # Read the LD_LIBRARY_PATH environment variable
        string(REPLACE ":" ";" LD_LIBRARY_PATH_LIST "$ENV{LD_LIBRARY_PATH}")
        set(CMAKE_INSTALL_RPATH "${LD_LIBRARY_PATH_LIST}")
    endif()
endif()

add_subdirectory(src/fileformat)
add_subdirectory(src/hardening)
add_subdirectory(src/protocol)
add_subdirectory(src/restricted)
add_subdirectory(src/resolver)
add_subdirectory(src/utilities)

if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
