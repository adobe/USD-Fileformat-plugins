option(USD_FILEFORMATS_ENABLE_ASSET_TESTS "Build the more in depth unit tests using downloaded assets." OFF)
option(USDFBX_ENABLE_INSTALL "Enable installation of plugin artifacts" ON)


if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
find_package(ZLIB REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(FBXSDK REQUIRED)
find_package(GTest REQUIRED)


add_subdirectory(src)
if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# We only want usdFbx in our package
set(CPACK_INSTALL_CMAKE_PROJECTS "src;usdFbx;ALL;/")

include(CPack)

