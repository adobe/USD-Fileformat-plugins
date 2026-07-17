add_library(usdGltf SHARED)

set(GLTF_EXT_LIST "gltf;GLTF;glTF;glb;GLB;glB" PARENT_SCOPE)

usd_plugin_compile_config(usdGltf)
target_compile_definitions(usdGltf PRIVATE USDGLTF_EXPORTS)

target_sources(usdGltf
PRIVATE
    "api.h"
    "debugCodes.h"
    "debugCodes.cpp"
    "fileFormat.h"
    "fileFormat.cpp"
    "gltf.h"
    "gltf.cpp"
    "gltfAnisotropy.h"
    "gltfAnisotropy.cpp"
    "gltfAnisotropyASM.h"
    "gltfAnisotropyASM.cpp"
    "gltfAnisotropyOpenPBR.h"
    "gltfAnisotropyOpenPBR.cpp"
    "gltfExport.h"
    "gltfExport.cpp"
    "gltfImport.h"
    "gltfImport.cpp"
    "gltfResolver.h"
    "gltfResolver.cpp"
    "gltfSpecGloss.h"
    "gltfSpecGloss.cpp"
    "importGltfContext.h"
)

# tinygltf.cpp defines TINYGLTF_IMPLEMENTATION and compiles the full tinygltf
# implementation as a single TU (the header-only library pattern). Only do this
# when tinygltf::tinygltf is an INTERFACE (header-only) target. When it is a
# compiled STATIC library, the lib already contains the implementation; compiling
# tinygltf.cpp alongside it produces duplicate symbols — a hard error on Apple's
# linker starting with Xcode 26.
get_target_property(_tinygltf_aliased tinygltf::tinygltf ALIASED_TARGET)
if(NOT _tinygltf_aliased)
    set(_tinygltf_aliased tinygltf::tinygltf)
endif()
get_target_property(_tinygltf_type ${_tinygltf_aliased} TYPE)
if(_tinygltf_type STREQUAL "INTERFACE_LIBRARY")
    target_sources(usdGltf PRIVATE "tinygltf.cpp")
endif()
unset(_tinygltf_aliased)
unset(_tinygltf_type)

target_include_directories(usdGltf
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}"
)

target_link_libraries(usdGltf
PRIVATE
    usd
    usdGeom
    usdSkel
    usdShade
    tinygltf::tinygltf
    Threads::Threads
    fileformatUtils
)

if(USD_FILEFORMATS_ENABLE_DRACO)
    target_compile_definitions(usdGltf PRIVATE TINYGLTF_ENABLE_DRACO)
    target_link_libraries(usdGltf PRIVATE draco::draco)
    message(STATUS "DRACO INCLUDE ${CMAKE_BINARY_DIR}")
    target_include_directories(usdGltf PRIVATE
        ${draco_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )
endif()

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    # We still need to go through `configure_file` even with `USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT` because we burn additional CMake variable beyond PLUG_INFO_LIBRARY_PATH
    # So we set `PLUG_INFO_LIBRARY_PATH` as a no op value and let other CMake variables being burnt in
    set(PLUG_INFO_LIBRARY_PATH "@PLUG_INFO_LIBRARY_PATH@")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdGltf${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

configure_file(plugInfo.json.in plugInfo.json)
set_property(TARGET usdGltf APPEND PROPERTY RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")
set_property(TARGET usdGltf APPEND PROPERTY RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDGLTF_DESTINATION is set in the parent scope by the add_usd_fileformat macro

if(USDGLTF_ENABLE_INSTALL)
    set_property(TARGET usdGltf
        APPEND PROPERTY
            INSTALL_RPATH "${plugin_install_rpath_root}/."
    )
    install(
        TARGETS  usdGltf
        EXPORT usd-fileformats-targets
        RUNTIME  DESTINATION ${USDGLTF_DESTINATION} COMPONENT Runtime
        LIBRARY  DESTINATION ${USDGLTF_DESTINATION} COMPONENT Runtime
        ARCHIVE  DESTINATION ${USDGLTF_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDGLTF_DESTINATION}/usdGltf/resources COMPONENT Runtime
    )

    if(USD_FILEFORMATS_ENABLE_INSTALL_PLUGINFO_ROOT)
        install(
            FILES plugInfo.root.json
            DESTINATION ${USDGLTF_DESTINATION}
            RENAME plugInfo.json
            COMPONENT Runtime
        )
    endif()
endif()
