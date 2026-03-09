#[=======================================================================[.rst:
----

Finds or fetches the TinyGLTF library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_TINYGLTF are 
TRUE, TinyGLTF will be fetched. Otherwise it will be searched via a redirect
to find_package(CONFIG), since TinyGLTF does provide a config module.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``tinygltf::tinygltf``
  The TinyGLTF library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``TinyGLTF_FOUND``


#]=======================================================================]

if(TARGET tinygltf::tinygltf)
    return()
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_TINYGLTF)

    find_package(nlohmann_json REQUIRED)

    message(STATUS "Fetching TinyGLTF")
    include(CPM)
    set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
    set(TINYGLTF_INSTALL ON)
    set(TINYGLTF_HEADER_ONLY OFF)
    set(_saved_BUILD_SHARED_LIBS_TinyGLTF ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)

    CPMAddPackage(
        NAME TinyGLTF
        GIT_REPOSITORY "https://github.com/syoyo/tinygltf.git"
        GIT_TAG        "v2.8.21"
    )
    set(TinyGLTF_FOUND TRUE)
    add_library(tinygltf::tinygltf ALIAS tinygltf)
    set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS_TinyGLTF})
else()
    if (${TinyGLTF_FIND_REQUIRED})
        find_package(TinyGLTF CONFIG REQUIRED)
    else()
        find_package(TinyGLTF CONFIG)
    endif()
endif()

target_link_libraries(tinygltf INTERFACE nlohmann_json::nlohmann_json)