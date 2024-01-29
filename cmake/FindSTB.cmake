#[=======================================================================[.rst:
----

Finds the stb library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``stb::stb``
  The stb library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``STB_FOUND``
  True if the system has the stb library.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``STB_INCLUDE_DIR``
  The directory containing ``stb.h``.

#]=======================================================================]
if (TARGET stb::stb)
  return()
endif()

include(FindPackageHandleStandardArgs)

find_path(STB_INCLUDE_DIR
  NAMES stb_image.h
)


find_package_handle_standard_args(STB
  REQUIRED_VARS STB_INCLUDE_DIR
)

if(STB_FOUND)
    add_library(stb::stb INTERFACE IMPORTED)
    set_target_properties(stb::stb PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${STB_INCLUDE_DIR}")
endif()