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

target_precompile_headers(usdPly
PRIVATE
    "<pxr/pxr.h>"
    "<pxr/usd/usd/common.h>"
    "<pxr/base/vt/array.h>"
    "<pxr/base/gf/rotation.h>"
    "<pxr/base/gf/quath.h>"
    "<pxr/base/gf/matrix4f.h>"
    "<pxr/base/gf/matrix4d.h>"
    "<pxr/base/tf/token.h>"
    "<pxr/usd/kind/registry.h>"
    "<pxr/usd/pcp/cache.h>"
    "<pxr/usd/ar/asset.h>"
    "<pxr/usd/ar/defaultResolver.h>"
    "<pxr/usd/ar/resolverContextBinder.h>"
    "<pxr/usd/usd/zipFile.h>"
    "<pxr/usd/usd/typed.h>"
    "<pxr/usd/usd/primRange.h>"
    "<pxr/usd/usd/payloads.h>"
    "<pxr/usd/usd/relationship.h>"
    "<pxr/usd/usd/schemaRegistry.h>"
    "<pxr/usd/usd/primCompositionQuery.h>"
    "<pxr/usd/usd/stage.h>"
    "<pxr/usd/usd/modelAPI.h>"
    "<pxr/usd/usd/references.h>"
    "<pxr/usd/sdf/reference.h>"
    "<pxr/usd/sdf/payload.h>"
    "<pxr/usd/sdf/layer.h>"
    "<pxr/usd/sdf/types.h>"
    "<pxr/usd/sdf/assetPath.h>"
    "<pxr/usd/usd/common.h>"
    "<pxr/usd/usdGeom/tokens.h>"
    "<pxr/usd/usdGeom/primvarsAPI.h>"
    "<pxr/usd/usdGeom/xform.h>"
    "<pxr/usd/usdGeom/xformable.h>"
    "<pxr/usd/usdGeom/xformCommonAPI.h>"
    "<pxr/usd/usdGeom/metrics.h>"
    "<pxr/usd/usdShade/tokens.h>"
    "<pxr/usd/usdShade/connectableAPI.h>"
    "<pxr/usd/usdShade/output.h>"
    "<pxr/usd/usdShade/materialBindingAPI.h>"
    "<pxr/usd/usdSkel/skeleton.h>"
    "<pxr/usd/usdSkel/cache.h>"
    "<pxr/usd/usdSkel/root.h>"
    "<pxr/usd/usdSkel/bindingAPI.h>"
    "<pxr/usd/usdSkel/utils.h>"
    "<pxr/usd/usdSkel/skeletonQuery.h>"
    "<pxr/usd/usdSkel/animation.h>"
    "<iostream>"
    "<vector>"
    "<map>"
    "<string>"
    "<algorithm>"
    "<fstream>"
    "<sstream>"
    "<cassert>"
    "<cstdlib>"
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

if(USDPLY_ENABLE_INSTALL)
    install(
        TARGETS usdPly
        RUNTIME DESTINATION plugin/usd COMPONENT Runtime
        LIBRARY DESTINATION plugin/usd COMPONENT Runtime
        RESOURCE DESTINATION plugin/usd/usdPly/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION plugin/usd
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
