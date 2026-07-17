add_library(usdStl SHARED)

set(STL_EXT_LIST "stl" PARENT_SCOPE)

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
    # We still need to go through `configure_file` even with `USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT` because we burn additional CMake variable beyond PLUG_INFO_LIBRARY_PATH
    # So we set `PLUG_INFO_LIBRARY_PATH` as a no op value and let other CMake variables being burnt in
    set(PLUG_INFO_LIBRARY_PATH "@PLUG_INFO_LIBRARY_PATH@")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdStl${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

configure_file(plugInfo.json.in plugInfo.json)
set_property(TARGET usdStl APPEND PROPERTY RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")
set_property(TARGET usdStl APPEND PROPERTY RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDSTL_DESTINATION is set in the parent scope by the add_usd_fileformat macro

if(USDSTL_ENABLE_INSTALL)
    set_property(TARGET usdStl
        APPEND PROPERTY
            INSTALL_RPATH "${plugin_install_rpath_root}/."
    )
    install(
        TARGETS usdStl
        EXPORT usd-fileformats-targets
        RUNTIME DESTINATION ${USDSTL_DESTINATION} COMPONENT Runtime
        LIBRARY DESTINATION ${USDSTL_DESTINATION} COMPONENT Runtime
        ARCHIVE DESTINATION ${USDSTL_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDSTL_DESTINATION}/usdStl/resources COMPONENT Runtime
    )

    if(USD_FILEFORMATS_ENABLE_INSTALL_PLUGINFO_ROOT)
        install(
            FILES plugInfo.root.json
            DESTINATION ${USDSTL_DESTINATION}
            RENAME plugInfo.json
            COMPONENT Runtime
        )
    endif()
endif()
