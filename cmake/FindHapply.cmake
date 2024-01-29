#[=======================================================================[.rst:
----

Finds or fetches the Happly library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_HAPPLY are 
TRUE, Happly will be fetched. Otherwise it will be searched via find commands.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``happly::happly``
  The Happly library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``Happly_FOUND``

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``HAPPLY_INCLUDE_DIR``
  The directory containing ``happly.h``.

#]=======================================================================]

if(TARGET happly::happly)
    return()
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_HAPPLY)
    message(STATUS "Fetching Happly")
    include(FetchContent)
    FetchContent_Declare(
        Happly
        GIT_REPOSITORY "https://github.com/nmwsharp/happly.git"
        GIT_TAG        "cfa2611550bc7da65855a78af0574b65deb81766"
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(Happly)
    if (happly_POPULATED)
        set(Happly_FOUND TRUE)
        add_library(happly::happly INTERFACE IMPORTED)
        target_include_directories(happly::happly INTERFACE ${happly_SOURCE_DIR})
    elseif(${Happly_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch Happly")
    endif()
else()
    include(FindPackageHandleStandardArgs)

    find_path(HAPPLY_INCLUDE_DIR
        NAMES happly.h
    )

    find_package_handle_standard_args(Happly
        REQUIRED_VARS HAPPLY_INCLUDE_DIR
    )

    if(Happly_FOUND)
        add_library(happly::happly INTERFACE IMPORTED)
        set_target_properties(happly::happly PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${HAPPLY_INCLUDE_DIR}")
    elseif(${Happly_FIND_REQUIRED})
        message(FATAL_ERROR "Could not find Happly")
    endif()
endif()


