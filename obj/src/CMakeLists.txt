add_library(usdObj SHARED)

usd_plugin_compile_config(usdObj)
target_compile_definitions(usdObj PRIVATE USDOBJ_EXPORTS)

target_sources(usdObj
PRIVATE
    "api.h"
    "debugCodes.h"
    "debugCodes.cpp"
    "fileFormat.h"
    "fileFormat.cpp"
    "obj.h"
    "obj.cpp"
    "objExport.h"
    "objExport.cpp"
    "objImport.h"
    "objImport.cpp"
    "objResolver.h"
    "objResolver.cpp"
)

target_include_directories(usdObj
PRIVATE
    "${PROJECT_BINARY_DIR}"
)

target_link_libraries(usdObj
PUBLIC
    tf
    sdf
    usd
    usdGeom
    usdSkel
    usdShade
    fileformatUtils
    fmt::fmt
    FastFloat::fast_float
)

target_include_directories(usdObj
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}/src"
)

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    set(PLUG_INFO_LIBRARY_PATH "\$\{PLUG_INFO_LIBRARY_PATH\}")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdObj${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdObj PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdObj PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDOBJ_DESTINATION is set in the parent scope by the add_usd_fileformat macro

if(USDOBJ_ENABLE_INSTALL)
    install(
        TARGETS usdObj
        RUNTIME DESTINATION ${USDOBJ_DESTINATION} COMPONENT Runtime
        LIBRARY DESTINATION ${USDOBJ_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDOBJ_DESTINATION}/usdObj/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION ${USDOBJ_DESTINATION}
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
