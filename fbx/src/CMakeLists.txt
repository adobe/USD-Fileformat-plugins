add_library(usdFbx SHARED)

usd_plugin_compile_config(usdFbx)
target_compile_definitions(usdFbx PRIVATE USDFBX_EXPORTS)

target_sources(usdFbx
PRIVATE
    "api.h"
    "debugCodes.h"
    "debugCodes.cpp"
    "fileFormat.h"
    "fileFormat.cpp"
    "fbx.h"
    "fbx.cpp"
    "fbxExport.h"
    "fbxExport.cpp"
    "fbxImport.h"
    "fbxImport.cpp"
    "fbxResolver.h"
    "fbxResolver.cpp"
)

target_include_directories(usdFbx
PRIVATE
    "${PROJECT_BINARY_DIR}"
)

target_link_libraries(usdFbx
PRIVATE
    tf
    sdf
    usd 
    usdGeom 
    usdSkel
    usdShade
    usdUtils
    arch
    fileformatUtils
    fbxsdk::fbxsdk
)

target_precompile_headers(usdFbx
PRIVATE
    "<pxr/pxr.h>"
    "<pxr/base/tf/diagnostic.h>"
    "<pxr/base/gf/matrix3d.h>"
    "<pxr/base/gf/rotation.h>"
    "<pxr/base/gf/quath.h>"
    "<pxr/usd/usd/primRange.h>"
    "<pxr/usd/usd/common.h>"
    "<pxr/usd/usdGeom/mesh.h>"
    "<pxr/usd/usdGeom/primvarsAPI.h>"
    "<pxr/usd/usdGeom/xform.h>"
    "<pxr/usd/usdGeom/camera.h>"
    "<pxr/usd/usdGeom/xformable.h>"
    "<pxr/usd/usdGeom/xformCommonAPI.h>"
    "<pxr/usd/usdGeom/nurbsCurves.h>"
    "<pxr/usd/usdGeom/hermiteCurves.h>"
    "<pxr/usd/usdGeom/basisCurves.h>"
    "<pxr/usd/usdSkel/skeleton.h>"
    "<fbxsdk.h>"
    "<iostream>"
    "<vector>"
    "<map>"
    "<mutex>"
)

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    set(PLUG_INFO_LIBRARY_PATH "\$\{PLUG_INFO_LIBRARY_PATH\}")
else()
    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdFbx${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdFbx PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdFbx PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

if(USDFBX_ENABLE_INSTALL)
    install(
        TARGETS  usdFbx
        RUNTIME  DESTINATION plugin/usd COMPONENT Runtime
        LIBRARY  DESTINATION plugin/usd COMPONENT Runtime
        RESOURCE DESTINATION plugin/usd/usdFbx/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION plugin/usd
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
