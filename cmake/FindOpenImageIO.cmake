#[=======================================================================[.rst:
----

Finds the OpenImageIO library.
This find module will simply redirect to a find_package(CONFIG) hinted to look
into the pxr_DIR.


Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``OpenImageIO::OpenImageIO``
  The OpenImageIO library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``OpenImageIO_FOUND``
  True if the system has the OpenImageIO library.



#]=======================================================================]
if (TARGET OpenImageIO::OpenImageIO)
    return()
endif()

if(${OpenImageIO_FIND_REQUIRED})
    find_package(OpenImageIO PATHS ${pxr_DIR} ${pxr_DIR}/lib64 REQUIRED)
else()
    find_package(OpenImageIO PATHS ${pxr_DIR} ${pxr_DIR}/lib64)
endif()
