option(NO_UNDEFINED          "Active no-undefined compile options" ON)
option(USD_FILEFORMATS_ENABLE_ASSET_TESTS   "Build the more in depth unit tests using downloaded assets." OFF)
cmake_dependent_option(USDSPZ_ENABLE_INSTALL "Enable installation of plugin artifacts" ON "USD_FILEFORMATS_ENABLE_INSTALL" OFF)

if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
if(USD_FILEFORMATS_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()
find_package(spz REQUIRED)
find_package(SphericalHarmonics REQUIRED)


add_subdirectory(src)

# Pass this list from the src/CMakeLists.txt to the parent scope
set(SPZ_EXT_LIST ${SPZ_EXT_LIST} PARENT_SCOPE)

if (USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif ()

# We only want PLY in our package
set(CPACK_INSTALL_CMAKE_PROJECTS "src;usdSpz;ALL;/")

include(CPack)

fileformats_register_plugin("usdSpz")
