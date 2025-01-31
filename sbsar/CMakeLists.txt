# Dependencies options
option(substance_DIR
       "Directory for external substance engine location" "")

option(USDSBSAR_ENABLE_INSTALL "Enable installation of plugin artifacts" ON)
# Substance Engine options
option(USDSBSAR_ENABLE_VULKAN "Enable Vulkan as backend for Substance Engine" OFF)
# Plugin features options
option(USDSBSAR_ENABLE_TEXTURE_TRANSFORM "Enables properties for tiling/scaling/rotating textures" ON)
option(USDSBSAR_ENABLE_FIX_STORM_16BIT "Enables fix storm 16bit textures issues" ON)
# Tests options (avaible only on windows)
option(USDSBSAR_TEST_UNDEFINED_LIBS "Raise error if at the end of compilation libs are not correctly linked" ON)

# Check if we're on an Apple silicon platform
set(USDSBSAR_BUILD_APPLE_SILICON OFF)
# This is the official flag from the cmake documentation
if(CMAKE_APPLE_SILICON_PROCESSOR)
  if(CMAKE_APPLE_SILICON_PROCESSOR STREQUAL "arm64")
    set(USDSBSAR_BUILD_APPLE_SILICON ON)
  endif()
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
  # If the official flag isn't set (which seems to be the case by default) we just check the host processor type
  set(USDSBSAR_BUILD_APPLE_SILICON ON)
endif()

if(WIN32)
  # List d3d11 before ogl3 to give preference to the D3D11 engine
  set(USDSBSAR_DEFAULT_SUBSTANCE_ENGINES d3d11_blend ogl3_blend sse2_blend)
elseif(USDSBSAR_BUILD_APPLE_SILICON)
  set(USDSBSAR_DEFAULT_SUBSTANCE_ENGINES mtl_blend ogl3_blend neon_blend)
else()
  # Linux and Intel Mac
  set(USDSBSAR_DEFAULT_SUBSTANCE_ENGINES mtl_blend ogl3_blend sse2_blend)
endif()

if(USDSBSAR_ENABLE_VULKAN)
  # Appending the Vulkan engine for now. Usually we pick the first available engine
  list(APPEND USDSBSAR_DEFAULT_SUBSTANCE_ENGINES vk_blend)
endif()

set(USDSBSAR_SUBSTANCE_ENGINES "${USDSBSAR_DEFAULT_SUBSTANCE_ENGINES}" CACHE
      STRING "List of Substance Engine implementations")

if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()

# Install path options
set(USDSBSAR_RUNTIME_INSTALL_PATH
    ""
    CACHE STRING
          "Install directory for engines and other dependent shared library")
set(USDSBSAR_RPATH
    ""
    CACHE STRING
          "linux/mac only research path for dependencies")
# ##############################################################################

# build type as upper case, usefull to set IMPORT_LOCATION_DEBUG/RELEASE
string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPER_BUILD_TYPE)

# prepare for include project's cmake finds and macros and pxrConfig's directory
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/sbsar/cmake)

# if not set, set default runtime directory and rpath
if(NOT USDSBSAR_RUNTIME_INSTALL_PATH)
  if(WIN32)
    set(USDSBSAR_RUNTIME_INSTALL_PATH "bin")
  else()
    set(USDSBSAR_RUNTIME_INSTALL_PATH "lib")
  endif(WIN32)
endif(NOT USDSBSAR_RUNTIME_INSTALL_PATH)
if(NOT USDSBSAR_RPATH)
  set(USDSBSAR_RPATH "${USDSBSAR_RUNTIME_INSTALL_PATH}")
endif()

# find dependent packages

find_package(ZLIB REQUIRED)
include(substance_engine)
add_subdirectory(src)

if(USD_FILEFORMATS_BUILD_TESTS)
  add_subdirectory(test)
endif(USD_FILEFORMATS_BUILD_TESTS)
