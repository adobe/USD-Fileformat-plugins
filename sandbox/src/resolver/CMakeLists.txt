add_library(usdInMemResolver SHARED)
target_compile_definitions(usdInMemResolver PRIVATE USDINMEMRESOLVER_EXPORTS)
usd_plugin_compile_config(usdInMemResolver)

target_sources(usdInMemResolver
PRIVATE
    "inMemoryResolver.cpp"
    "inMemoryWritableAsset.cpp"
    "badAssetResolver.cpp"
)

target_include_directories(usdInMemResolver
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)

target_link_libraries(usdInMemResolver
PRIVATE
    usd
    fileformatUtils
    sandboxUtils
)

if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    set(PLUG_INFO_LIBRARY_PATH "@PLUG_INFO_LIBRARY_PATH@")
    set(PLUG_INFO_SANDBOX_LIBRARY_PATH "@PLUG_INFO_SANDBOX_LIBRARY_PATH@")

else()

    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdInMemResolver${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if (APPLE)
        get_filename_component(Python_BASE_DIR ${Python3_RUNTIME_LIBRARY_DIRS} DIRECTORY)
        if (NOT DEFINED PLUG_INFO_SANDBOX_LIBRARY_PATH)
            set(PLUG_INFO_SANDBOX_LIBRARY_PATH "$ENV{LD_LIBRARY_PATH}:${Python_BASE_DIR}/Python")
        endif()
    else()
        set(PLUG_INFO_SANDBOX_LIBRARY_PATH "NOT USED ON THIS OS")
    endif()
endif()

configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdInMemResolver PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdInMemResolver PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

message(STATUS "CMAKE_INSTALL_RPATH: ${CMAKE_INSTALL_RPATH}")
set_target_properties(usdInMemResolver PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time

if(USD_SANDBOXPROXY_ENABLE_INSTALL)
    # Because the in-memory resolver is used by both sandboxed and non-sandboxed plugins, we need to
    # install it to both plugin_proxy (a separate location from the normal plugins) and
    # plugin_sandboxed
    set(DESTINATIONS "plugin_proxy/usd" "plugin_sandboxed/usd")

    foreach(DEST ${DESTINATIONS})
        install(
            TARGETS usdInMemResolver
            RUNTIME DESTINATION ${DEST} COMPONENT Runtime
            LIBRARY DESTINATION ${DEST} COMPONENT Runtime
            RESOURCE DESTINATION ${DEST}/usdInMemResolver/resources COMPONENT Runtime
        )
    endforeach()
endif()
