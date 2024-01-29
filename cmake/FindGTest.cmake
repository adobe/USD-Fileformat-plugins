#[=======================================================================[.rst:
----

Finds or fetches the GTest library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_GTEST are 
TRUE, GTest will be fetched. Otherwise it will be searched via a redirect
to find_package(CONFIG), since GTest does provide a config module.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``GTest::gtest``
  The GTest library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``GTest_FOUND``


#]=======================================================================]

if(TARGET GTest::gtest)
    return()
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_GTEST)
    message(STATUS "Fetching GTest")
    include(FetchContent)
    FetchContent_Declare(
        googletest # using GTest here triggers errors
        GIT_REPOSITORY "https://github.com/google/googletest.git"
        GIT_TAG        "release-1.11.0"
        OVERRIDE_FIND_PACKAGE
    )
    set(BUILD_SHARED_LIBS OFF)
    set(gtest_force_shared_crt ON)
    set(BUILD_GMOCK OFF)
    set(BUILD_GTEST ON)
    FetchContent_MakeAvailable(googletest)
    set(BUILD_SHARED_LIBS ON)
    if(googletest_POPULATED)
        set(GTest_FOUND TRUE)
    elseif(${GTest_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch GTest")
    endif()
else()
    if(${GTest_FIND_REQUIRED})
        find_package(GTest CONFIG REQUIRED)
    else()
        find_package(GTest CONFIG)
    endif()
endif()