add_library(usdGltf SHARED)
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
    "tinygltf.cpp"
    "gltfAnisotropy.h"
    "gltfAnisotropy.cpp"
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
    set(PLUG_INFO_LIBRARY_PATH "\$\{PLUG_INFO_LIBRARY_PATH\}")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdGltf${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdGltf PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdGltf PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDGLTF_DESTINATION is set in the parent scope by the add_usd_fileformat macro

if(USDGLTF_ENABLE_INSTALL)
    install(
        TARGETS  usdGltf
        RUNTIME  DESTINATION ${USDGLTF_DESTINATION} COMPONENT Runtime
        LIBRARY  DESTINATION ${USDGLTF_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDGLTF_DESTINATION}/usdGltf/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION ${USDGLTF_DESTINATION}
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
