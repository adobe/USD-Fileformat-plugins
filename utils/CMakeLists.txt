
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

target_sources(fileformatUtils
PRIVATE
    "README.md"
    "assetresolver.h"
    "assetresolver.cpp"
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
    "neuralAssetsHelper.h"
    "neuralAssetsHelper.cpp"
    "resolver.h"
    "resolver.cpp"
    "sdfMaterialUtils.h"
    "sdfMaterialUtils.cpp"
    "sdfUtils.h"
    "sdfUtils.cpp"
    "usdData.h"
    "usdData.cpp"
)

target_include_directories(fileformatUtils
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE "${PROJECT_BINARY_DIR}"
)

target_link_libraries(fileformatUtils
PUBLIC
    OpenImageIO::OpenImageIO
    ${PXR_LIBRARIES}
    ZLIB::ZLIB
)

if(USD_FILEFORMATS_BUILD_TESTS)
    target_sources(fileformatUtils
        PRIVATE
            "test.h"
            "test.cpp"
    )
    target_link_libraries(fileformatUtils
        PUBLIC
            GTest::gtest # should separate usdutils-runtime part and usdutils-test part
    )
endif()

install(TARGETS fileformatUtils)
