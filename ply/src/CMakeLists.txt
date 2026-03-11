add_library(usdPly SHARED)

usd_plugin_compile_config(usdPly)
target_compile_definitions(usdPly PRIVATE USDPLY_EXPORTS)

target_sources(usdPly
PRIVATE
    "api.h"
    "debugCodes.h"
    "debugCodes.cpp"
    "fileFormat.h"
    "fileFormat.cpp"
    "plyImport.h"
    "plyImport.cpp"
    "plyExport.h"
    "plyExport.cpp"
    "dictencoder.h"
    "dictencoder.cpp"
)

target_include_directories(usdPly
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}"
)

target_link_libraries(usdPly
PRIVATE
    tf
    sdf
    usd
    usdGeom
    usdSkel
    usdShade
    happly::happly
    fileformatUtils
)

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    set(PLUG_INFO_LIBRARY_PATH "\$\{PLUG_INFO_LIBRARY_PATH\}")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdPly${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdPly PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdPly PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDPLY_DESTINATION is set in the parent scope by the add_usd_fileformat macro
if(USDPLY_ENABLE_INSTALL)
    install(
        TARGETS usdPly
        RUNTIME DESTINATION ${USDPLY_DESTINATION} COMPONENT Runtime
        LIBRARY DESTINATION ${USDPLY_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDPLY_DESTINATION}/usdPly/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION ${USDPLY_DESTINATION}
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
