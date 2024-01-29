#[=======================================================================[.rst:
----

Finds or fetches the LibXml2 library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_LIBXML2 are 
TRUE, LibXml2 will be fetched. Otherwise it will be searched via a redirect
to find_package(CONFIG), since LibXml2 does provide a config module.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``LibXml2::LibXml2``
  The LibXml2 library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``LibXml2_FOUND``


#]=======================================================================]

if(TARGET LibXml2::LibXml2)
    return()
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_LIBXML2)
    message(STATUS "Fetching libxml2")
    include(FetchContent)
    FetchContent_Declare(
        LibXml2
        GIT_REPOSITORY "https://github.com/GNOME/libxml2.git"
        GIT_TAG        "ae383bdb74523ddaf831d7db0690173c25e483b3" # Release v2.10.0
        OVERRIDE_FIND_PACKAGE
    )
    set(BUILD_SHARED_LIBS OFF) # otherwise fails
    set(LIBXML2_WITH_ICONV OFF)
    set(LIBXML2_WITH_LZMA OFF)
    set(LIBXML2_WITH_PYTHON OFF)
    set(LIBXML2_WITH_ZLIB ON)
    set(LIBXML2_WITH_TESTS OFF)
    FetchContent_MakeAvailable(LibXml2)
    if(libxml2_POPULATED)
        set(LibXml2_FOUND TRUE)
    elseif(${LibXml2_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch LibXml2")
    endif()
else()
    message(STATUS "Find LibXml2 ${LibXml2_ROOT}")
    if(${LibXml2_FIND_REQUIRED})
        find_package(LibXml2 CONFIG REQUIRED)
    else()
        find_package(LibXml2 CONFIG)
    endif()
endif()