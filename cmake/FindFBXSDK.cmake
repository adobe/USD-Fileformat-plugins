#[=======================================================================[.rst:
----

Finds the fbx library.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``fbx::fbx``
  The fbx library

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``fbx_FOUND``
  True if the system has the fbx library.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``FBX_INCLUDE_DIR``
  The directory containing ``fbxsdk.h``.
``FBX_LIBRARY``
  The path to the Foo library.

#]=======================================================================]
if(TARGET fbxsdk::fbxsdk)
    return()
endif()

include(SelectLibraryConfigurations)
include(FindPackageHandleStandardArgs)
include(CheckTypeSize)

CHECK_TYPE_SIZE(void* SIZEOF_PTR)
if(SIZEOF_PTR EQUAL 8)
  set(FBX_PLATFORM_SUFFIX "x64")
else()
  set(FBX_PLATFORM_SUFFIX "x86")
endif()

if (NOT (DEFINED FBX_COMPILER_SUFFIX))
    if(WIN32)
        set(VS_VERSIONS "vs2022" "vs2019" "vs2017" "vs2015")
        foreach(VS_VERSION IN LISTS VS_VERSIONS)
            if(EXISTS "${FBXSDK_ROOT}/lib/${VS_VERSION}")
                  set(FBX_COMPILER_SUFFIX ${VS_VERSION})
                  break()
            endif()
        endforeach()

        # Fallback based on MSVC version if directory check fails
        if(NOT FBX_COMPILER_SUFFIX)
            if(MSVC_VERSION GREATER_EQUAL 1930)
                set(FBX_COMPILER_SUFFIX "vs2022")
            elseif(MSVC_VERSION GREATER_EQUAL 1920)
                set(FBX_COMPILER_SUFFIX "vs2019")
            elseif(MSVC_VERSION GREATER_EQUAL 1910)
                set(FBX_COMPILER_SUFFIX "vs2017")
            elseif(MSVC_VERSION GREATER_EQUAL 1900)
                set(FBX_COMPILER_SUFFIX "vs2015")
            elseif(MSVC12)
                set(FBX_COMPILER_SUFFIX "vs2013")
            elseif(MSVC11)
                set(FBX_COMPILER_SUFFIX "vs2012")
            elseif(MSVC10)
                set(FBX_COMPILER_SUFFIX "vs2010")
            else()
                message(WARNING "Unknown or unsupported Visual Studio version. Defaulting to an assumed compatible version.")
                set(FBX_COMPILER_SUFFIX "vs2019") # Default fallback for unknown versions
            endif()
        endif()
    elseif(APPLE)
        set(FBX_COMPILER_SUFFIX "clang")
    else()
        set(FBX_COMPILER_SUFFIX "gcc")
    endif()
endif ()

find_path(FBXSDK_INCLUDE_DIRS fbxsdk.h)
find_library(FBXSDK_LIBRARY_DEBUG
    NAMES libfbxsdk-md libfbxsdk.a fbxsdk
    PATH_SUFFIXES
        ${FBX_COMPILER_SUFFIX}/${FBX_PLATFORM_SUFFIX}/debug
        ${FBX_COMPILER_SUFFIX}/libstdcpp/debug
        ${FBX_COMPILER_SUFFIX}/debug
)
find_library(FBXSDK_LIBRARY_RELEASE
    NAMES libfbxsdk-md libfbxsdk.a fbxsdk
    PATH_SUFFIXES
        ${FBX_COMPILER_SUFFIX}/${FBX_PLATFORM_SUFFIX}/release
        ${FBX_COMPILER_SUFFIX}/libstdcpp/release
        ${FBX_COMPILER_SUFFIX}/release
)

if(WIN32)
    set(FBXSDK_DEPENDENCIES ZLIB::ZLIB LibXml2::LibXml2)
elseif(APPLE)
    find_library(cf CoreFoundation)
    set(FBXSDK_DEPENDENCIES ZLIB::ZLIB LibXml2::LibXml2 dl pthread iconv ${cf})
else()
    set(FBXSDK_DEPENDENCIES ZLIB::ZLIB LibXml2::LibXml2 dl pthread)
endif()


select_library_configurations(FBXSDK)
find_package_handle_standard_args(FBXSDK REQUIRED_VARS FBXSDK_INCLUDE_DIRS FBXSDK_LIBRARIES)
if(FBXSDK_FOUND)
    add_library(fbxsdk::fbxsdk STATIC IMPORTED)
    set_target_properties(fbxsdk::fbxsdk PROPERTIES
        IMPORTED_LOCATION_DEBUG ${FBXSDK_LIBRARY_DEBUG}
        IMPORTED_LOCATION_RELEASE ${FBXSDK_LIBRARY_RELEASE}
        IMPORTED_CONFIGURATIONS "Debug;Release"
        INTERFACE_LINK_LIBRARIES "${FBXSDK_DEPENDENCIES}"
        INTERFACE_INCLUDE_DIRECTORIES ${FBXSDK_INCLUDE_DIRS}
    )
    set_target_properties(fbxsdk::fbxsdk PROPERTIES
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
    )
    install(FILES ${FBXSDK_LIBRARY_${UPPER_BUILD_TYPE}} DESTINATION plugin/usd COMPONENT Runtime)
endif()
