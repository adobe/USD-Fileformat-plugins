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
    include(CPM)
    set(BUILD_SHARED_LIBS OFF) # otherwise fails
    set(LIBXML2_WITH_ICONV OFF)
    set(LIBXML2_WITH_LZMA OFF)
    set(LIBXML2_WITH_PYTHON OFF)
    set(LIBXML2_WITH_ZLIB ON)
    set(LIBXML2_WITH_TESTS OFF)
    CPMAddPackage(
        NAME LibXml2
        GIT_REPOSITORY "https://github.com/GNOME/libxml2.git"
        GIT_TAG        "v2.13.0"
    )
    set(LibXml2_FOUND TRUE)
else()
    message(STATUS "Find LibXml2 ${LibXml2_ROOT}")
    if(${LibXml2_FIND_REQUIRED})
        find_package(LibXml2 CONFIG REQUIRED)
    else()
        find_package(LibXml2 CONFIG)
    endif()
endif()
