option(NO_UNDEFINED "Active no-undefined compile options" ON)
option(USD_FILEFORMATS_ENABLE_ASSET_TESTS "Build the more in depth unit tests using downloaded assets." OFF)
option(USDSTL_ENABLE_INSTALL "Enable installation of plugin artifacts" ON)


if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
if(USD_FILEFORMATS_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()


add_subdirectory(src)
if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

#We only want usdStl in our package
set(CPACK_INSTALL_CMAKE_PROJECTS "src;usdStl;ALL;/")

include(CPack)
