#[=======================================================================[.rst:
----

Finds or fetches the spz library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_SPZ are 
TRUE, spz will be fetched. Otherwise it will be searched via find commands.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``spz::spz``
  The spz library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``spz_FOUND``

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``SPZ_INCLUDE_DIR``
  The directory containing ``splat-types.h``.

#]=======================================================================]

if(TARGET spz::spz)
    return()
endif()

if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB REQUIRED)
endif()

set(SPZ_BUILD_TOOLS OFF)
set(SPZ_BUILD_EXTENSIONS ON)

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_SPZ)
    message(STATUS "Fetching spz")
    include(CPM)
    # zstd's installed CMake config breaks incremental re-configure: find_package(zstd)
    # re-discovers it in the install prefix and its import guard collides with the zstd
    # target already created this pass. Force spz's FetchContent path instead. The fetch
    # runs in a function so CMAKE_DISABLE_FIND_PACKAGE_zstd stays local to it (the spz
    # target it creates is global and persists); later find_package(zstd) is unaffected.
    function(_spz_fetch_with_bundled_zstd)
        set(CMAKE_DISABLE_FIND_PACKAGE_zstd TRUE)
        CPMAddPackage(
            NAME spz
            GIT_REPOSITORY "https://github.com/nianticlabs/spz.git"
            GIT_TAG        "21715c3b7a609ea6fb7c69b8ae42181a12b59f22" # v3.0.0+adobe.32
            OPTIONS        "BUILD_SHARED_LIBS OFF"
        )
    endfunction()
    _spz_fetch_with_bundled_zstd()
    set(spz_FOUND TRUE)
    if (NOT MSVC)
        target_compile_options(spz PRIVATE "-Wno-shorten-64-to-32")
    endif()
else()
    include(FindPackageHandleStandardArgs)

    find_path(SPZ_INCLUDE_DIR
        NAMES splat-types.h
    )

    find_package_handle_standard_args(spz
        REQUIRED_VARS SPZ_INCLUDE_DIR
    )

    if(spz_FOUND)
        file(GLOB SPZ_SRC_FILES
            ${SPZ_INCLUDE_DIR}/*.cc
            ${SPZ_INCLUDE_DIR}/*.h
        )
        add_library(spz STATIC)
        target_sources(spz PRIVATE ${SPZ_SRC_FILES})
        target_include_directories(spz PUBLIC ${SPZ_INCLUDE_DIR})
        target_link_libraries(spz PRIVATE ZLIB::ZLIB)
        set_property(TARGET spz PROPERTY POSITION_INDEPENDENT_CODE ON)
        set_property(TARGET spz PROPERTY CXX_STANDARD 17)
        target_compile_definitions(spz PRIVATE "_USE_MATH_DEFINES")

        if (NOT MSVC)
            target_compile_options(spz PRIVATE "-Wno-shorten-64-to-32")
        endif()

        add_library(spz::spz ALIAS spz)
    elseif(${spz_FIND_REQUIRED})
        message(FATAL_ERROR "Could not find spz")
    endif()
endif()


