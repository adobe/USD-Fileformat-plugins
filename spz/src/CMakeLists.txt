add_library(usdSpz SHARED)

set(SPZ_EXT_LIST "spz" PARENT_SCOPE)

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
    # We still need to go through `configure_file` even with `USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT` because we burn additional CMake variable beyond PLUG_INFO_LIBRARY_PATH
    # So we set `PLUG_INFO_LIBRARY_PATH` as a no op value and let other CMake variables being burnt in
    set(PLUG_INFO_LIBRARY_PATH "@PLUG_INFO_LIBRARY_PATH@")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdSpz${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

configure_file(plugInfo.json.in plugInfo.json)
set_property(TARGET usdSpz APPEND PROPERTY RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json")
set_property(TARGET usdSpz APPEND PROPERTY RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

# USDSPZ_DESTINATION is set in the parent scope by the add_usd_fileformat macro

if(USDSPZ_ENABLE_INSTALL)
    set_property(TARGET usdSpz
        APPEND PROPERTY
            INSTALL_RPATH "${plugin_install_rpath_root}/."
    )
    install(
        TARGETS usdSpz
        EXPORT usd-fileformats-targets
        RUNTIME DESTINATION ${USDSPZ_DESTINATION} COMPONENT Runtime
        LIBRARY DESTINATION ${USDSPZ_DESTINATION} COMPONENT Runtime
        ARCHIVE DESTINATION ${USDSPZ_DESTINATION} COMPONENT Runtime
        RESOURCE DESTINATION ${USDSPZ_DESTINATION}/usdSpz/resources COMPONENT Runtime
    )

    if(USD_FILEFORMATS_ENABLE_INSTALL_PLUGINFO_ROOT)
        install(
            FILES plugInfo.root.json
            DESTINATION ${USDSPZ_DESTINATION}
            RENAME plugInfo.json
            COMPONENT Runtime
        )
    endif()
endif()
