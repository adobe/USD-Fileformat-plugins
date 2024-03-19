set(PLUGIN_NAME usdSbsar)
add_library(${PLUGIN_NAME} SHARED ${SRC})


set(PUBLIC_HEADERS
    api.h)
target_sources(${PLUGIN_NAME}
    PRIVATE
    assetPath/assetPathParser.cpp

    assetResolver/sbsarImage.cpp
    assetResolver/sbsarPackageResolver.cpp
    assetResolver/sbsarResolverCache.cpp

    sbsarEngine/sbsarEngine.cpp
    sbsarEngine/sbsarPackageCache.cpp
    sbsarEngine/sbsarRender.cpp
    sbsarEngine/sbsarRenderThread.cpp
    sbsarEngine/sbsarAssetCache.cpp
    sbsarEngine/sbsarInputImageCache.cpp

    usdGeneration/dictEncoder.cpp
    usdGeneration/sbsarAsm.cpp
    usdGeneration/sbsarLuxDomeLight.cpp
    usdGeneration/sbsarMaterial.cpp
    usdGeneration/sbsarMtlx.cpp
    usdGeneration/sbsarSymbolMapper.cpp
    usdGeneration/sbsarUsdPreviewSurface.cpp
    usdGeneration/usdGenerationHelpers.cpp

    sbsarDebug.cpp
    sbsarfileformat.cpp
    tokens.cpp
    ${PUBLIC_HEADERS})


target_include_directories(${PLUGIN_NAME} PUBLIC .)

# target properties
set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_STANDARD 17)

if (USDSBSAR_FORCE_EXTERNAL_USD)
  set(_boost_include_dir "${_usd_testing_root}/include/boost-1_78")
  set(_pxr_include_dir "${_usd_testing_root}/include")
else()
  set(_boost_include_dir "${PXR_INCLUDE_DIRS}/boost-1_70")
  set(_pxr_include_dir "${PXR_INCLUDE_DIRS}")
endif ()

target_include_directories(
        ${PLUGIN_NAME} PRIVATE ${_pxr_include_dir} ${_boost_include_dir})

target_link_libraries(
        ${PLUGIN_NAME}
        PUBLIC tf
        sdf
        usd
        usdGeom
        usdShade
        usdLux
        usdMedia
        usdRender
        vt
        hio
        ${SUBSTANCE_TARGETS}
        fileformatUtils
)

# ##############################################################################

# Available Substance engines, this is sent to C++ as a ; separated string
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSDSBSAR_SUBSTANCE_ENGINES=\"${USDSBSAR_SUBSTANCE_ENGINES}\"")

# ENGINE SUFFIX
option(USDSBSAR_ENGINE_DLL_SUFFIX
       "Suffix to append to dll's to avoid conflicts" "")

# Configure Engine suffix parameter
if(USDSBSAR_ENGINE_DLL_SUFFIX)
  if(NOT WIN32)
    message(
      FATAL_ERROR "[usd-fileformat/sbsar] Engine suffix not implemented on non-windows platforms")
  endif(NOT WIN32)
  message("[usd-fileformat/sbsar] Has USDSBSAR_ENGINE_DLL_SUFFIX ${USDSBSAR_ENGINE_DLL_SUFFIX}")
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -DUSDSBSAR_ENGINE_DLL_SUFFIX=\"${USDSBSAR_ENGINE_DLL_SUFFIX}\""
  )
else(USDSBSAR_ENGINE_DLL_SUFFIX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSDSBSAR_ENGINE_DLL_SUFFIX=\"\"")
endif(USDSBSAR_ENGINE_DLL_SUFFIX)
# ##############################################################################

# set compile flags
list(APPEND flags "${_PXR_CXX_FLAGS}")

if(WIN32)
  # Silence USD warnings
  list(APPEND flags "/wd4244" "/wd4305" "/wd4267")
  list(APPEND flags "-DNOMINMAX" "-DBOOST_ALL_DYN_LINK" "-DBOOST_ALL_NO_LIB")
  target_compile_definitions(${PLUGIN_NAME} PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX "DEBUG_TAG=SBSAR")
endif(WIN32)

if(USDSBSAR_ENABLE_FIX_STORM_16BIT)
  target_compile_definitions(${PLUGIN_NAME} PRIVATE FIX_STORM_16BIT)
endif()

# enable ASM
if(USDSBSAR_ENABLE_ASM)
  message("[usd-fileformat/sbsar] Building with ASM")
  target_compile_definitions(${PLUGIN_NAME} PUBLIC USDSBSAR_ENABLE_ASM)
else(USDSBSAR_ENABLE_ASM)
  message("[usd-fileformat/sbsar] Building without ASM")
endif(USDSBSAR_ENABLE_ASM)

# enable UsdPreviewSurface
if(USDSBSAR_ENABLE_USDPREVIEWSURFACE)
  message("[usd-fileformat/sbsar] Building with UsdPreviewSurface")
  target_compile_definitions(${PLUGIN_NAME} PUBLIC USDSBSAR_ENABLE_USDPREVIEWSURFACE)
else(USDSBSAR_ENABLE_USDPREVIEWSURFACE)
  message("[usd-fileformat/sbsar] Building without UsdPreviewSurface")
endif(USDSBSAR_ENABLE_USDPREVIEWSURFACE)

# enable texture transform
if(USDSBSAR_ENABLE_TEXTURE_TRANSFORM)
  message("[usd-fileformat/sbsar] Building with Texture Transform Support")
  target_compile_definitions(${PLUGIN_NAME} PUBLIC USDSBSAR_ENABLE_TEXTURE_TRANSFORM)
else(USDSBSAR_ENABLE_TEXTURE_TRANSFORM)
  message("[usd-fileformat/sbsar] Building without Texture Transform Support")
endif(USDSBSAR_ENABLE_TEXTURE_TRANSFORM)

# tests linked libs
if(USDSBSAR_TEST_UNDEFINED_LIBS
   AND UNIX
   AND NOT APPLE)
  list(APPEND flags "-Wl,--no-allow-shlib-undefined" "-Wl,--no-undefined"
       "-pthread")
endif()

set(_boost_include_dir)
set(_pxr_include_dir)

# set selected flags
target_compile_options(${PLUGIN_NAME} PRIVATE ${flags})

# Guard for /Zc:inline
if(WIN32)
    if (MSVC_VERSION GREATER_EQUAL 1920)
        target_compile_options(${PLUGIN_NAME} PRIVATE "/Zc:inline-")
    endif()
endif() # WIN32

# ##############################################################################


# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
  set(PLUG_INFO_LIBRARY_PATH "\$\{PLUG_INFO_LIBRARY_PATH\}")
else()
  set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}${PLUGIN_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
configure_file(plugInfo.json.in plugInfo.json)

set_target_properties(${PLUGIN_NAME} PROPERTIES PUBLIC_HEADER ${PUBLIC_HEADERS})
target_compile_definitions(${PLUGIN_NAME} PRIVATE USDSBSAR_EXPORTS)

set_target_properties(${PLUGIN_NAME} PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)
set(_resource_list ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json generatedSchema.usda schema.usda)
set_target_properties(${PLUGIN_NAME} PROPERTIES RESOURCE_FILES "${_resource_list}")

if(USDSBSAR_ENABLE_INSTALL)
  # Install the plugInfo.json file for the specific plugin
  install(
    TARGETS ${PLUGIN_NAME}
    RUNTIME DESTINATION plugin/usd COMPONENT Runtime
    LIBRARY DESTINATION plugin/usd COMPONENT Runtime
    RESOURCE DESTINATION plugin/usd/${PLUGIN_NAME}/resources COMPONENT Runtime
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Devel)

  # Install the master plugInfo.json file for the install directory Note that this
  # one exists to make sure the plugin is easy to deploy in the plugin directory
  # of a pre-existing usd build which assumes the plugin has a
  # <pluginName>/resources/plugInfo.json
  install(
          FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.root.json
          DESTINATION plugin/usd
          RENAME plugInfo.json
          COMPONENT Runtime)
endif()

# ##############################################################################
