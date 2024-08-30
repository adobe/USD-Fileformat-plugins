option(USD_FILEFORMATS_ENABLE_ASSET_TESTS "Build the more in depth unit tests using downloaded assets." OFF)
option(USDGLTF_ENABLE_INSTALL "Enable installation of plugin artifacts" ON)
option(USD_FILEFORMATS_ENABLE_DRACO "Enable reading of draco meshes in glTF" ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)



if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
find_package(Threads REQUIRED)
if(USD_FILEFORMATS_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()
find_package(TinyGLTF REQUIRED)
if(USD_FILEFORMATS_ENABLE_DRACO)
    find_package(draco REQUIRED)
endif()


add_subdirectory(src)
if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()


#We only want usdGltf in our package
set(CPACK_INSTALL_CMAKE_PROJECTS "src;usdGltf;ALL;/")

include(CPack)

