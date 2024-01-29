#[=======================================================================[.rst:
----

Finds or fetches the fmt library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_FMT are 
TRUE, fmt will be fetched. Otherwise it will be searched via a redirect
to find_package(CONFIG), since fmt does provide a config module.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``fmt::fmt``
  The fmt library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``fmt_FOUND``


#]=======================================================================]

if(TARGET fmt::fmt)
    return()
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_FMT)
    message(STATUS "Fetching fmt")
    include(FetchContent)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY "https://github.com/fmtlib/fmt.git"
        GIT_TAG        "10.1.1" # f5e54359df4c26b6230fc61d38aa294581393084
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(fmt)
    if(fmt_POPULATED)
        set(fmt_FOUND TRUE)
    elseif(${fmt_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch fmt")
    endif()
else()
    if(${fmt_FIND_REQUIRED})
        find_package(fmt CONFIG REQUIRED)
    else()
        find_package(fmt CONFIG)
    endif()
endif()