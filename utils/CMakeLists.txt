
if (NOT TARGET usd)
    find_package(pxr REQUIRED)
endif()
find_package(OpenImageIO REQUIRED)
find_package(ZLIB REQUIRED)
if(USD_FILEFORMATS_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()
if (USD_FILEFORMATS_ENABLE_PLY OR USD_FILEFORMATS_ENABLE_SPZ)
    find_package(SphericalHarmonics REQUIRED)
endif()

add_library(fileformatUtils SHARED)
target_compile_definitions(fileformatUtils PRIVATE USDFFUTILS_EXPORTS)
if(DEFINED USD_FILEFORMATS_DEFAULT_WRITE_USDPREVIEWSURFACE)
    target_compile_definitions(fileformatUtils PRIVATE
        USD_FILEFORMATS_DEFAULT_WRITE_USDPREVIEWSURFACE=$<BOOL:${USD_FILEFORMATS_DEFAULT_WRITE_USDPREVIEWSURFACE}>)
endif()
if(DEFINED USD_FILEFORMATS_DEFAULT_WRITE_ASM)
    target_compile_definitions(fileformatUtils PRIVATE
        USD_FILEFORMATS_DEFAULT_WRITE_ASM=$<BOOL:${USD_FILEFORMATS_DEFAULT_WRITE_ASM}>)
endif()
if(DEFINED USD_FILEFORMATS_DEFAULT_WRITE_OPENPBR)
    target_compile_definitions(fileformatUtils PRIVATE
        USD_FILEFORMATS_DEFAULT_WRITE_OPENPBR=$<BOOL:${USD_FILEFORMATS_DEFAULT_WRITE_OPENPBR}>)
endif()
usd_plugin_compile_config(fileformatUtils)

set(_UTILS_HEADERS
    "assetresolver.h"
    "common.h"
    "debugCodes.h"
    "featureFlags.h"
    "dictencoder.h"
    "geometry.h"
    "transforms.h"
    "images.h"
    "layerRead.h"
    "layerReadMaterial.h"
    "layerReadMaterialUtils.h"
    "layerWriteShared.h"
    "layerWriteMaterial.h"
    "layerWriteOpenPBR.h"
    "layerWriteSdfData.h"
    "materials.h"
    "neuralAssetsHelper.h"
    "resolver.h"
    "sdfMaterialUtils.h"
    "sdfUtils.h"
    "usdData.h"
)

set(_UTILS_SOURCES
    "assetresolver.cpp"
    "common.cpp"
    "dictencoder.cpp"
    "featureFlags.cpp"
    "geometry.cpp"
    "transforms.cpp"
    "images.cpp"
    "layerRead.cpp"
    "layerReadMaterial.cpp"
    "layerReadMaterialUtils.cpp"
    "layerWriteShared.cpp"
    "layerWriteMaterial.cpp"
    "layerWriteOpenPBR.cpp"
    "layerWriteSdfData.cpp"
    "materials.cpp"
    "neuralAssetsHelper.cpp"
    "resolver.cpp"
    "sdfMaterialUtils.cpp"
    "sdfUtils.cpp"
    "usdData.cpp"
)

if (USD_FILEFORMATS_ENABLE_PLY OR USD_FILEFORMATS_ENABLE_SPZ)
    list(APPEND _UTILS_HEADERS
        "gsplatHelper.h"
    )
    list(APPEND _UTILS_SOURCES
        "gsplatHelper.cpp"
    )
endif()

# Prepend paths
list(TRANSFORM _UTILS_HEADERS PREPEND "include/fileformatutils/")
list(TRANSFORM _UTILS_SOURCES PREPEND "src/")


# Add sources to target
target_sources(fileformatUtils
PRIVATE
    "README.md"
    ${_UTILS_HEADERS}
    ${_UTILS_SOURCES}
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
    OpenImageIO::OpenImageIO_Util
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

if (USD_FILEFORMATS_ENABLE_PLY OR USD_FILEFORMATS_ENABLE_SPZ)
    target_link_libraries(fileformatUtils
    PRIVATE
        SphericalHarmonics::SphericalHarmonics
    )
endif()

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

if(USD_FILEFORMATS_BUILD_TESTS)
    add_subdirectory(tests)
endif()
