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
    message(STATUS "Fetching TinyGLTF")
    include(FetchContent)
    FetchContent_Declare(
        TinyGLTF
        GIT_REPOSITORY "https://github.com/syoyo/tinygltf.git"
        GIT_TAG        "v2.8.21" # 4bfc1fc1807e2e2cf3d3111f67d6ebd957514c80
        OVERRIDE_FIND_PACKAGE
    )
    set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
    set(TINYGLTF_INSTALL OFF)
    set(TINYGLTF_HEADER_ONLY ON)
    FetchContent_MakeAvailable(TinyGLTF)
    if(tinygltf_POPULATED)
        set(TinyGLTF_FOUND TRUE)
        add_library(tinygltf::tinygltf ALIAS tinygltf)
    elseif(${TinyGLTF_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch TinyGLTF")
    endif()
else()
    if (${TinyGLTF_FIND_REQUIRED})
        find_package(TinyGLTF CONFIG REQUIRED)
    else()
        find_package(TinyGLTF CONFIG)
    endif()
endif()