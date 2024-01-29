#[=======================================================================[.rst:
----

Finds or fetches the FastFloat library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_FASTFLOAT are 
TRUE, FastFloat will be fetched. Otherwise it will be searched via a redirect
to find_package(CONFIG), since FastFloat does provide a config module.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``FastFloat::fast_float``
  The FastFloat library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``FastFloat_FOUND``


#]=======================================================================]

if(TARGET FastFloat::fast_float)
    return()
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_FASTFLOAT)
    message(STATUS "Fetching FastFloat")
    include(FetchContent)
    FetchContent_Declare(
        FastFloat
        GIT_REPOSITORY "https://github.com/lemire/fast_float.git"
        GIT_TAG        "v1.1.2" # 8159e8bcf63c1b92f5a51fb550f966e56624b209
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(FastFloat)
    if (fastfloat_POPULATED)
        set(FastFloat_FOUND TRUE)
        add_library(FastFloat::fast_float ALIAS fast_float)
    elseif(${FastFloat_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch FastFloat")
    endif()
else()
    if(${FastFloat_FIND_REQUIRED})
        find_package(FastFloat CONFIG REQUIRED)
    else()
        find_package(FastFloat CONFIG)
    endif()
endif()