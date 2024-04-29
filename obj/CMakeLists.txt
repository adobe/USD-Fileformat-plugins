option(NO_UNDEFINED "Active no-undefined compile options" ON)
option(USD_FILEFORMATS_ENABLE_ASSET_TESTS "Build the more in depth unit tests using downloaded assets." OFF)
option(USDOBJ_ENABLE_INSTALL "Enable installation of plugin artifacts" ON)




if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
if(USD_FILEFORMATS_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()
find_package(fmt REQUIRED)
find_package(FastFloat REQUIRED)


add_subdirectory(src)
if(USD_FILEFORMATS_BUILD_TESTS )
    add_subdirectory(tests)
endif()

#We only want OBJ in our package
set(CPACK_INSTALL_CMAKE_PROJECTS "src;usdObj;ALL;/")

include(CPack)
