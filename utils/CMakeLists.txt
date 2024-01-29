
if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
find_package(OpenImageIO REQUIRED)
find_package(GTest REQUIRED)

add_library(fileformatUtils SHARED)
target_compile_definitions(fileformatUtils PRIVATE USDFFUTILS_EXPORTS)
usd_plugin_compile_config(fileformatUtils)

target_sources(fileformatUtils
PRIVATE
    "README.md"
    "common.h"
    "common.cpp"
    "debugCodes.h"
    "dictencoder.h"
    "dictencoder.cpp"
    "geometry.h"
    "geometry.cpp"
    "transforms.h"
    "transforms.cpp"
    "images.h"
    "images.cpp"
    "layerRead.h"
    "layerRead.cpp"
    "layerWriteShared.h"
    "layerWriteShared.cpp"
    "layerWriteMaterial.h"
    "layerWriteMaterial.cpp"
    "layerWriteMaterialX.h"
    "layerWriteMaterialX.cpp"
    "layerWriteSdfData.h"
    "layerWriteSdfData.cpp"
    "materials.h"
    "materials.cpp"
    "resolver.h"
    "resolver.cpp"
    "sdfMaterialUtils.h"
    "sdfMaterialUtils.cpp"
    "sdfUtils.h"
    "sdfUtils.cpp"
    "test.h"
    "test.cpp"
    "usdData.h"
    "usdData.cpp"
)

target_include_directories(fileformatUtils INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fileformatUtils
PUBLIC
    OpenImageIO::OpenImageIO
    tf
    sdf
    usd
    usdGeom
    usdSkel
    usdShade
    usdUtils
    usdVol
    arch
    GTest::gtest # should separate usdutils-runtime part and usdutils-test part
)

install(TARGETS fileformatUtils)
