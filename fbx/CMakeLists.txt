option(USD_FILEFORMATS_ENABLE_ASSET_TESTS "Build the more in depth unit tests using downloaded assets." OFF)
cmake_dependent_option(USDFBX_ENABLE_INSTALL "Enable installation of plugin artifacts" ON "USD_FILEFORMATS_ENABLE_INSTALL" OFF)



if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
find_package(ZLIB REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(FBXSDK REQUIRED)
if(USD_FILEFORMATS_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()


add_subdirectory(src)

# Pass this list from the src/CMakeLists.txt to the parent scope
set(FBX_EXT_LIST ${FBX_EXT_LIST} PARENT_SCOPE)

if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# We only want usdFbx in our package
set(CPACK_INSTALL_CMAKE_PROJECTS "src;usdFbx;ALL;/")

include(CPack)

fileformats_register_plugin("usdFbx")
