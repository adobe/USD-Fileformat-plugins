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

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_SPZ)
    message(STATUS "Fetching spz")
    include(FetchContent)
    FetchContent_Declare(
        spz
        GIT_REPOSITORY "https://github.com/raymondyfei/spz.git"
        GIT_TAG        "fd4e2a57bd6b7462657d41eebda330eca0f35159"
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(spz)
    if (spz_POPULATED)
        set(spz_FOUND TRUE)
        file(GLOB SPZ_SRC_FILES
            ${spz_SOURCE_DIR}/src/cc/*.cc
            ${spz_SOURCE_DIR}/src/cc/*.h
        )
        add_library(spz STATIC)
        target_sources(spz PRIVATE ${SPZ_SRC_FILES})
        set(SPZ_INCLUDE_DIR "${spz_SOURCE_DIR}/src/cc")
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
        message(FATAL_ERROR "Could not fetch spz")
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


