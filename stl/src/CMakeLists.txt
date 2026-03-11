add_library(usdStl SHARED)

usd_plugin_compile_config(usdStl)
target_compile_definitions(usdStl PRIVATE USDSTL_EXPORTS)

target_sources(usdStl
PRIVATE
    "api.h"
    "debugCodes.h"
    "debugCodes.cpp"
    "fileFormat.h"
    "fileFormat.cpp"
    "stlModel.h"
    "stlModel.cpp"
    "stlImport.h"
    "stlImport.cpp"
    "stlExport.h"
    "stlExport.cpp"
)

target_include_directories(usdStl
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}"
)

target_link_libraries(usdStl
PRIVATE
    tf
    vt
    sdf
    usd
    usdGeom
    usdSkel
    usdShade
    usdLux
    usdUtils
    fileformatUtils
)

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    set(PLUG_INFO_LIBRARY_PATH "\$\{PLUG_INFO_LIBRARY_PATH\}")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdStl${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdStl PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdStl PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDSTL_DESTINATION is set in the parent scope by the add_usd_fileformat macro

if(USDSTL_ENABLE_INSTALL)
    install(
        TARGETS usdStl
        RUNTIME DESTINATION ${USDSTL_DESTINATION} COMPONENT Runtime
        LIBRARY DESTINATION ${USDSTL_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDSTL_DESTINATION}/usdStl/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION ${USDSTL_DESTINATION}
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
