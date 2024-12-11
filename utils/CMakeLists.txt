
if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
find_package(OpenImageIO REQUIRED)
find_package(ZLIB REQUIRED)
if(USD_FILEFORMATS_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()

add_library(fileformatUtils SHARED)
target_compile_definitions(fileformatUtils PRIVATE USDFFUTILS_EXPORTS)
usd_plugin_compile_config(fileformatUtils)

set(HEADERS
    "assetresolver.h"
    "common.h"
    "debugCodes.h"
    "dictencoder.h"
    "geometry.h"
    "transforms.h"
    "images.h"
    "layerRead.h"
    "layerWriteShared.h"
    "layerWriteMaterial.h"
    "layerWriteMaterialX.h"
    "layerWriteSdfData.h"
    "materials.h"
    "neuralAssetsHelper.h"
    "resolver.h"
    "sdfMaterialUtils.h"
    "sdfUtils.h"
    "usdData.h"
)

set(SOURCES
    "assetresolver.cpp"
    "common.cpp"
    "dictencoder.cpp"
    "geometry.cpp"
    "transforms.cpp"
    "images.cpp"
    "layerRead.cpp"
    "layerWriteShared.cpp"
    "layerWriteMaterial.cpp"
    "layerWriteMaterialX.cpp"
    "layerWriteSdfData.cpp"
    "materials.cpp"
    "neuralAssetsHelper.cpp"
    "resolver.cpp"
    "sdfMaterialUtils.cpp"
    "sdfUtils.cpp"
    "usdData.cpp"
)

# Prepend paths
list(TRANSFORM HEADERS PREPEND "include/fileformatutils/")
list(TRANSFORM SOURCES PREPEND "src/")

# Add sources to target
target_sources(fileformatUtils
PRIVATE
    "README.md"
    ${HEADERS}
    ${SOURCES}
)

target_include_directories(fileformatUtils
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${PROJECT_BINARY_DIR}"
)

target_link_libraries(fileformatUtils
PUBLIC
    OpenImageIO::OpenImageIO
    tf
    sdf
    usd
    usdGeom
    usdLux
    usdRender
    usdSkel
    usdShade
    usdUtils
    usdVol
    hio
    arch
    ZLIB::ZLIB
)

if(USD_FILEFORMATS_BUILD_TESTS)
    target_sources(fileformatUtils
        PRIVATE
            "include/fileformatutils/test.h"
            "src/test.cpp"
    )
    target_link_libraries(fileformatUtils
        PUBLIC
            GTest::gtest # should separate usdutils-runtime part and usdutils-test part
    )
endif()

install(TARGETS fileformatUtils)
