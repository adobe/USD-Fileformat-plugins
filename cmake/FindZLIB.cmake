#[=======================================================================[.rst:
----

Finds or fetches the ZLIB library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_ZLIB are 
TRUE, ZLIB will be fetched. Otherwise it will be searched via find commands.
The search is hinted to look into pxr_DIR, but can be overriden by setting
ZLIB_ROOT.


Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``ZLIB::ZLIB``
  The zlib library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``ZLIB_FOUND``
  True if the system has the zlib library.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``ZLIB_INCLUDE_DIR``
  The directory containing ``zlib.h``.
``ZLIB_LIBRARY``
  The path to the zlib library.


#]=======================================================================]
if(TARGET ZLIB::ZLIB)
    return()
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_ZLIB)
    message(STATUS "Fetching ZLIB")
    include(FetchContent)
    FetchContent_Declare(
        ZLIB
        GIT_REPOSITORY "https://github.com/madler/zlib.git"
        GIT_TAG        "cacf7f1d4e3d44d871b605da3b647f07d718623f" # /tag/v1.2.11
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(ZLIB)
    if(zlib_POPULATED)
        set(ZLIB_FOUND TRUE)
        add_library(ZLIB::ZLIB ALIAS zlib)
    elseif(${ZLIB_FIND_REQUIRED})
        message(FATAL_ERROR "Could not fetch ZLIB")
    endif()
else()
    include(SelectLibraryConfigurations)
    include(FindPackageHandleStandardArgs)

    if (UNIX AND NOT APPLE)
      find_path(ZLIB_INCLUDE_DIR
          HINTS ${pxr_DIR}/include
          NAMES zlib.h
      )
      find_library(ZLIB_LIBRARY_DEBUG
          HINTS ${pxr_DIR}/lib
          NAMES z zlib zlibd
      )
      find_library(ZLIB_LIBRARY_RELEASE
          HINTS ${pxr_DIR}/lib
          NAMES z zlib
      )
    else()
      find_path(ZLIB_INCLUDE_DIR
          HINTS ${pxr_DIR}/include
          NO_CMAKE_SYSTEM_PATH # otherwise it always finds the system one
          NAMES zlib.h
      )
      find_library(ZLIB_LIBRARY_DEBUG
          HINTS ${pxr_DIR}/lib
          NO_CMAKE_SYSTEM_PATH # otherwise it always finds the system one
          NAMES z zlib zlibd
      )
      find_library(ZLIB_LIBRARY_RELEASE
          HINTS ${pxr_DIR}/lib
          NO_CMAKE_SYSTEM_PATH # otherwise it always finds the system one
          NAMES z zlib
      )
    endif()

    select_library_configurations(ZLIB)
    find_package_handle_standard_args(ZLIB REQUIRED_VARS ZLIB_INCLUDE_DIR ZLIB_LIBRARIES)

    if(ZLIB_FOUND)
        add_library(ZLIB::ZLIB SHARED IMPORTED)
        set_target_properties(ZLIB::ZLIB PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ZLIB_INCLUDE_DIR})
        if (ZLIB_LIBRARY_DEBUG)
            set_property(TARGET ZLIB::ZLIB        PROPERTY IMPORTED_LOCATION_DEBUG ${ZLIB_LIBRARY_DEBUG})
            set_property(TARGET ZLIB::ZLIB        PROPERTY IMPORTED_IMPLIB_DEBUG   ${ZLIB_LIBRARY_DEBUG})
            set_property(TARGET ZLIB::ZLIB APPEND PROPERTY IMPORTED_CONFIGURATIONS "Debug")
        endif()
        if (ZLIB_LIBRARY_RELEASE)
            set_property(TARGET ZLIB::ZLIB        PROPERTY IMPORTED_LOCATION_RELEASE ${ZLIB_LIBRARY_RELEASE})
            set_property(TARGET ZLIB::ZLIB        PROPERTY IMPORTED_IMPLIB_RELEASE   ${ZLIB_LIBRARY_RELEASE})
            set_property(TARGET ZLIB::ZLIB APPEND PROPERTY IMPORTED_CONFIGURATIONS   "Release")
        endif()
    elseif(${ZLIB_FIND_REQUIRED})
        message(FATAL_ERROR "Could not find ZLIB")
    endif()
endif()