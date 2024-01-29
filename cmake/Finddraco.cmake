#[=======================================================================[.rst:
----

Finds or fetches the Draco library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_DRACO are
TRUE, Draco will be fetched. Otherwise it will be searched via find commands,
since draco 1.3.6 (the one bundled in USD) does not have a good config module.
The search is hinted to look into pxr_DIR, but can be overriden by setting
draco_ROOT.


Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found or fetched:

``draco::draco``
  The draco library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``draco_FOUND``
  True if the system has the draco library.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``draco_INCLUDE_DIR``
  The directory containing ``draco_version.h``.
``draco_LIBRARY``
  The path to the draco.lib library.

#]=======================================================================]

if (TARGET draco::draco)
    return()
endif()


if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_DRACO)
    message(STATUS "Fetching draco")
    include(FetchContent)
    FetchContent_Declare(
        draco
        GIT_REPOSITORY "https://github.com/google/draco.git"
        GIT_TAG        "9f856abaafb4b39f1f013763ff061522e0261c6f" # 1.56
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(draco)
    if(draco_POPULATED)
        set(draco_FOUND TRUE)
    elseif(${draco_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch draco")
    endif()
else()
    include(SelectLibraryConfigurations)
    include(FindPackageHandleStandardArgs)

    find_path(draco_INCLUDE_DIR
        HINTS ${pxr_DIR}/include
        NAMES draco/core/draco_version.h
    )

    find_library(draco_LIBRARY_DEBUG
        HINTS ${pxr_DIR}/lib
        NAMES draco
    )

    find_library(draco_LIBRARY_RELEASE
        HINTS ${pxr_DIR}/lib
        NAMES draco
    )

    select_library_configurations(draco)
    find_package_handle_standard_args(draco REQUIRED_VARS draco_INCLUDE_DIR draco_LIBRARIES)

    if(draco_FOUND)
        add_library(draco::draco STATIC IMPORTED)
        set_target_properties(draco::draco PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${draco_INCLUDE_DIR}")
        if (draco_LIBRARY_DEBUG)
            set_property(TARGET draco::draco        PROPERTY IMPORTED_LOCATION_DEBUG ${draco_LIBRARY_DEBUG})
            set_property(TARGET draco::draco APPEND PROPERTY IMPORTED_CONFIGURATIONS "Debug")
        endif()
        if (draco_LIBRARY_RELEASE)
            set_property(TARGET draco::draco        PROPERTY IMPORTED_LOCATION_RELEASE ${draco_LIBRARY_RELEASE})
            set_property(TARGET draco::draco APPEND PROPERTY IMPORTED_CONFIGURATIONS   "Release")
        endif()
    elseif(${draco_FIND_REQUIRED})
        message(FATAL_ERROR "Could not find draco")
    endif()
endif()