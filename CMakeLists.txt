cmake_minimum_required(VERSION 3.16.0)
project(usdFileFormats)

file(READ "version" VERSION)
string(REGEX MATCH "([0-9]*).([0-9]*).([0-9]*)" _ ${VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_MATCH_1})
set(CPACK_PACKAGE_VERSION_MINOR ${CMAKE_MATCH_2})
set(CPACK_PACKAGE_VERSION_PATCH ${CMAKE_MATCH_3})
set(VERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH})
message(STATUS "PROJECT VERSION IS: ${VERSION}")
configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h")

if (HAS_PARENT)
    set(usd_fileformats_standalone_default FALSE)
else ()
    set(usd_fileformats_standalone_default TRUE)
endif()

option(USD_FILEFORMATS_STANDALONE "Enable project standalone build" ${usd_fileformats_standalone_default})

if(USD_FILEFORMATS_STANDALONE)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
endif()
include(cmake/compiler_config.cmake)

option(USD_FILEFORMATS_BUILD_TESTS  "Build the unit tests" ON)
option(USD_FILEFORMATS_ENABLE_FBX   "Enables fbx plugin"   ON)
option(USD_FILEFORMATS_ENABLE_GLTF  "Enables gltf plugin"  ON)
option(USD_FILEFORMATS_ENABLE_OBJ   "Enables obj plugin"   ON)
option(USD_FILEFORMATS_ENABLE_SPZ   "Enables spz plugin"   ON)
option(USD_FILEFORMATS_ENABLE_PLY   "Enables ply plugin"   ON)
option(USD_FILEFORMATS_ENABLE_STL   "Enables stl plugin"   ON)
option(USD_FILEFORMATS_ENABLE_SBSAR   "Enables sbsar plugin"   OFF)
option(USD_FILEFORMATS_ENABLE_DRACO "Enables draco for the gltf plugin"   OFF)
option(USD_FILEFORMATS_FETCH_GTEST "Forces FetchContent for GTest" ON)
option(USD_FILEFORMATS_FETCH_TINYGLTF "Forces FetchContent for TinyGLTF" ON)
option(USD_FILEFORMATS_FETCH_DRACO "Forces FetchContent for Draco" OFF)
option(USD_FILEFORMATS_FETCH_ZLIB "Forces FetchContent for Zlib" OFF)
option(USD_FILEFORMATS_FETCH_LIBXML2 "Forces FetchContent for LibXml2" ON)
option(USD_FILEFORMATS_FETCH_HAPPLY "Forces FetchContent for Happly" ON)
option(USD_FILEFORMATS_FETCH_SPZ "Forces FetchContent for spz" ON)
option(USD_FILEFORMATS_FETCH_SPHERICAL_HARMONICS "Forces FetchContent for SphericalHarmonics" ON)
option(USD_FILEFORMATS_FETCH_FMT "Forces FetchContent for Fmt" ON)
option(USD_FILEFORMATS_FETCH_FASTFLOAT "Forces FetchContent for FastFLoat" ON)
option(USD_FILEFORMATS_ENABLE_CXX11_ABI "Use the CXX 11 ABI on Linux" OFF)
option(USD_FILEFORMATS_ENABLE_ASM  "Enables ASM material representation"   OFF)
option(USD_FILEFORMATS_FETCH_SPHERICAL_HARMONICS "Forces FetchContent for SphericalHarmonics" ON)

# This is a future looking option which turns on/off writing MaterialX Shaders
# when importing a file per the OpenPBR spec:
# https://github.com/AcademySoftwareFoundation/OpenPBR/blob/main/reference/open_pbr_surface.mtlx
# NOTE: These MaterialX shaders are not compatible with Reality Composer.
option(USD_FILEFORMATS_ENABLE_MTLX "Enables MaterialX material representation" OFF)

# unary_function and binary_function are no longer provided in C++17 and newer Standard modes.
# They can be re-enabled with _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
add_compile_definitions(_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)

if (USD_FILEFORMATS_BUILD_TESTS AND USD_FILEFORMATS_STANDALONE)
    # Only enable testing here if tests are turned on, and the project
    # is building standalone
    enable_testing()
endif ()

if (USD_FILEFORMATS_ENABLE_ASM)
    message("Building with ASM")
    add_definitions(-DUSD_FILEFORMATS_ENABLE_ASM)
else(USD_FILEFORMATS_ENABLE_ASM)
    message("Building without ASM")
endif(USD_FILEFORMATS_ENABLE_ASM)


if (UNIX AND NOT APPLE)
    # Only use this option to drive flags if set standalone. If this is
    # included in another project, don't set the ABI at all and let
    # the parent project drive it
    if (USD_FILEFORMATS_STANDALONE)
        if (USD_FILEFORMATS_ENABLE_CXX11_ABI)
            add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=1)
        else ()
            add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
        endif ()

        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    endif ()
endif ()

set(_usd_testing_root)
if (USD_FILEFORMATS_STANDALONE)
    # Acqurie the real root to the USD project
    get_filename_component(_usd_testing_root ${pxr_ROOT} ABSOLUTE)

    if (WIN32)
        file(TO_CMAKE_PATH "${_usd_testing_root}" _usd_testing_root)
    endif ()
endif ()

add_subdirectory(utils)

if (USD_FILEFORMATS_ENABLE_FBX)
    add_subdirectory(fbx)
endif()
if (USD_FILEFORMATS_ENABLE_GLTF)
    add_subdirectory(gltf)
endif()
if (USD_FILEFORMATS_ENABLE_OBJ)
    add_subdirectory(obj)
endif()
if (USD_FILEFORMATS_ENABLE_PLY)
    add_subdirectory(ply)
endif()
if (USD_FILEFORMATS_ENABLE_SBSAR)
    add_subdirectory(sbsar)
endif()
if (USD_FILEFORMATS_ENABLE_SPZ)
    add_subdirectory(spz)
endif()
if (USD_FILEFORMATS_ENABLE_STL)
    add_subdirectory(stl)
endif()

if (UNIX AND NOT APPLE)
    if (USD_FILEFORMATS_STANDALONE AND USD_FILEFORMATS_BUILD_TESTS)
        install(TARGETS fileformatUtils DESTINATION plugin/usd)
    endif ()
endif ()

