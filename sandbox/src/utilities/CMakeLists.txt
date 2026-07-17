add_library(sandboxUtils SHARED)

usd_plugin_compile_config(sandboxUtils)
target_compile_definitions(sandboxUtils PRIVATE USDSANDBOX_EXPORTS)

target_sources(sandboxUtils
PRIVATE
    "utilities.cpp"
    "base64url.cpp"
    "quarantine.cpp"
    "sandboxAssetCache.cpp"
    "../debugCodes.cpp"
)

target_include_directories(sandboxUtils
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/"
)

target_link_libraries(sandboxUtils
PRIVATE
    usd
    fileformatUtils
)

set_target_properties(sandboxUtils PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")


# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time

install(TARGETS sandboxUtils)