add_library(usdSpz SHARED)

usd_plugin_compile_config(usdSpz)
target_compile_definitions(usdSpz PRIVATE USDSPZ_EXPORTS)

target_sources(usdSpz
PRIVATE
    "api.h"
    "debugCodes.h"
    "debugCodes.cpp"
    "fileFormat.h"
    "fileFormat.cpp"
    "spzImport.h"
    "spzImport.cpp"
    "spzExport.h"
    "spzExport.cpp"
)

target_include_directories(usdSpz
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}"
)

target_link_libraries(usdSpz
PRIVATE
    tf
    sdf
    usd
    usdGeom
    usdSkel
    usdShade
    spz::spz
    SphericalHarmonics::SphericalHarmonics
    fileformatUtils
)

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    set(PLUG_INFO_LIBRARY_PATH "\$\{PLUG_INFO_LIBRARY_PATH\}")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdSpz${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdSpz PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdSpz PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDSPZ_DESTINATION is set in the parent scope by the add_usd_fileformat macro

if(USDSPZ_ENABLE_INSTALL)
    install(
        TARGETS usdSpz
        RUNTIME DESTINATION ${USDSPZ_DESTINATION} COMPONENT Runtime
        LIBRARY DESTINATION ${USDSPZ_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDSPZ_DESTINATION}/usdSpz/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION ${USDSPZ_DESTINATION}
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
