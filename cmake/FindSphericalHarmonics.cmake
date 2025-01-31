#[=======================================================================[.rst:
----

Finds or fetches the spherical-harmonics library.
If USD_FILEFORMATS_FORCE_FETCHCONTENT or USD_FILEFORMATS_FETCH_SPHERICAL_HARMONICS are 
TRUE, spherical-harmonics will be fetched. Otherwise it will be searched via find commands.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if fetched:

``SphericalHarmonics::SphericalHarmonics``
  The SphericalHarmonics library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``SphericalHarmonics_FOUND``

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``SH_INCLUDE_DIR``
  The directory containing ``sh/spherical_harmonics.h``.

#]=======================================================================]

if(TARGET SphericalHarmonics::SphericalHarmonics)
    return()
endif()

if (NOT TARGET Eigen3::Eigen)
    find_package(Eigen3 REQUIRED)
endif()

if(USD_FILEFORMATS_FORCE_FETCHCONTENT OR USD_FILEFORMATS_FETCH_SPHERICAL_HARMONICS)
    message(STATUS "Fetching SphericalHarmonics")
    include(FetchContent)
    FetchContent_Declare(
        spherical_harmonics_git
        GIT_REPOSITORY "https://github.com/google/spherical-harmonics.git"
        GIT_TAG        "ccb6c7fec875a1cd5ce5eb1315a9fa7603e0919a"
    )
    FetchContent_MakeAvailable(spherical_harmonics_git)

    if(spherical_harmonics_git_POPULATED)
        set(SphericalHarmonics_FOUND TRUE)
        set(SH_SRC_FILES
            ${spherical_harmonics_git_SOURCE_DIR}/sh/spherical_harmonics.cc
            ${spherical_harmonics_git_SOURCE_DIR}/sh/spherical_harmonics.h
            ${spherical_harmonics_git_SOURCE_DIR}/sh/image.h
        )
        add_library(SphericalHarmonics STATIC)
        target_sources(SphericalHarmonics PRIVATE ${SH_SRC_FILES})
        set(SH_INCLUDE_DIR "${spherical_harmonics_git_SOURCE_DIR}")
        target_include_directories(SphericalHarmonics PUBLIC ${SH_INCLUDE_DIR})
        target_link_libraries(SphericalHarmonics PUBLIC Eigen3::Eigen)
        set_property(TARGET SphericalHarmonics PROPERTY POSITION_INDEPENDENT_CODE ON)
        set_property(TARGET SphericalHarmonics PROPERTY CXX_STANDARD 17)
        target_compile_definitions(SphericalHarmonics PRIVATE "_USE_MATH_DEFINES")
        add_library(SphericalHarmonics::SphericalHarmonics ALIAS SphericalHarmonics)
    endif()
else()
    include(FindPackageHandleStandardArgs)

    find_path(SH_INCLUDE_DIR
        NAMES sh/spherical_harmonics.h
    )

    find_package_handle_standard_args(SphericalHarmonics
        REQUIRED_VARS SH_INCLUDE_DIR
    )

    if(SphericalHarmonics_FOUND)
        set(SH_SRC_FILES
            ${SH_INCLUDE_DIR}/sh/spherical_harmonics.cc
            ${SH_INCLUDE_DIR}/sh/spherical_harmonics.h
            ${SH_INCLUDE_DIR}/sh/image.h
        )
        add_library(SphericalHarmonics STATIC)
        target_sources(SphericalHarmonics PRIVATE ${SH_SRC_FILES})
        set(SH_INCLUDE_DIR "${SphericalHarmonics_SOURCE_DIR}")
        target_include_directories(SphericalHarmonics PUBLIC ${SH_INCLUDE_DIR})
        target_link_libraries(SphericalHarmonics PUBLIC Eigen3::Eigen)
        set_property(TARGET SphericalHarmonics PROPERTY POSITION_INDEPENDENT_CODE ON)
        set_property(TARGET SphericalHarmonics PROPERTY CXX_STANDARD 17)
        target_compile_definitions(SphericalHarmonics PRIVATE "_USE_MATH_DEFINES")

       add_library(SphericalHarmonics::SphericalHarmonics ALIAS SphericalHarmonics)
    elseif(${SphericalHarmonics_FIND_REQUIRED})
        message(FATAL_ERROR "Could not find SphericalHarmonics")
    endif()
endif()


