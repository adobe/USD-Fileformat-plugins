add_library(usdSandboxProxy SHARED)

usd_plugin_compile_config(usdSandboxProxy)
target_compile_definitions(usdSandboxProxy PRIVATE USDSANDBOXPROXY_EXPORTS)

target_sources(usdSandboxProxy
PRIVATE
    "fileFormat.cpp"
    "sandboxProxyResolver.cpp"
)

target_include_directories(usdSandboxProxy
PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)

target_link_libraries(usdSandboxProxy
PRIVATE
    usd
    usdGeom
    usdSkel
    usdShade
    fileformatUtils
    sandboxUtils
    sandboxProtocol
    sandboxHardening
)

# In the plugInfo.json, we need a list of sandboxed extensions. The fileformat extension list is
# case insensitive, and the resolver extension list is case sensitive, so we must generate both.

# Helper function to filter a list, making it all lower case and removing duplicates
# case-insensitive.
function(filter_case_insensitive LIST FILTERED_LIST)
    set(RESULT "")
    foreach(EXTENSION IN LISTS ${LIST})
        # Append the lower case version of the extension to the result if it's not already in the
        # list
        string(TOLOWER "${EXTENSION}" EXTENSION_LOWER)
        list(FIND RESULT "${EXTENSION_LOWER}" INDEX)
        if (INDEX EQUAL -1)
            list(APPEND RESULT "${EXTENSION_LOWER}")
        endif()
    endforeach()

    set(${FILTERED_LIST} "${RESULT}" PARENT_SCOPE)
endfunction()

# Resolver extension list is case sensitive
list(JOIN USD_FILEFORMATS_SANDBOXED_EXTENSIONS "\",\"" CASE_SENSITIVE_EXTENSIONS_STRING)
set(CASE_SENSITIVE_EXTENSIONS_STRING "\"${CASE_SENSITIVE_EXTENSIONS_STRING}\"")

# Fileformat extension list is case insensitive, so we must filter it to remove duplicates
filter_case_insensitive(USD_FILEFORMATS_SANDBOXED_EXTENSIONS SANDBOXED_EXTENSIONS_CASE_INSENSITIVE)
list(JOIN SANDBOXED_EXTENSIONS_CASE_INSENSITIVE "\",\"" CASE_INSENSITIVE_EXTENSIONS_STRING)
set(CASE_INSENSITIVE_EXTENSIONS_STRING "\"${CASE_INSENSITIVE_EXTENSIONS_STRING}\"")

message(STATUS "Sandboxed extensions: ${CASE_INSENSITIVE_EXTENSIONS_STRING}")

set(PLUG_INFO_SANDBOXED_EXTENSIONS_CASE_SENSITIVE ${CASE_SENSITIVE_EXTENSIONS_STRING})
set(PLUG_INFO_SANDBOXED_EXTENSIONS_CASE_INSENSITIVE ${CASE_INSENSITIVE_EXTENSIONS_STRING})

# Installation of plugin files mimics the file structure that USD has for plugins,
# so it is easy to deploy it in a pre-existing USD build, if one chooses to do so.

# Allow an option for deferring the path replacement to install time
if(USD_PLUGIN_DEFER_LIBRARY_PATH_REPLACEMENT)
    set(PLUG_INFO_LIBRARY_PATH "@PLUG_INFO_LIBRARY_PATH@")

    # For constructing the sandbox profile, in the situation where the plugins are being used or
    # installed with another application. That application will define the library path variable.
    # Note that it must include libraries the application uses, including Python, as well as
    # access to any plugInfo.json files that the sandboxed process will need. This is a variable
    # that is used on MacOS only, so on other platforms it should be replaced with an empty string
    # or some other indicator that the variable is not used
    set(PLUG_INFO_SANDBOX_LIBRARY_PATH "@PLUG_INFO_SANDBOX_LIBRARY_PATH@")

    set(PLUG_INFO_SANDBOX_EXECUTABLE_RELATIVE_PATH "@PLUG_INFO_SANDBOX_EXECUTABLE_RELATIVE_PATH@")
    set(PLUG_INFO_PROXY_PLUGIN_PATH "@PLUG_INFO_PROXY_PLUGIN_PATH@")
else()

    set(PLUG_INFO_LIBRARY_PATH "../${CMAKE_SHARED_LIBRARY_PREFIX}usdSandboxProxy${CMAKE_SHARED_LIBRARY_SUFFIX}")
    # For constructing the sandbox profile, when the plugins are being built standalone. We must
    # supply the location of libraries used (including Python) unless the given variable has
    # already been defined externally.
    if (APPLE)
        get_filename_component(Python_BASE_DIR ${Python3_RUNTIME_LIBRARY_DIRS} DIRECTORY)

        # The variable may be defined externally if libraries are referenced some other way
        # besides LD_LIBRARY_PATH
        if (NOT DEFINED PLUG_INFO_SANDBOX_LIBRARY_PATH)
            set(PLUG_INFO_SANDBOX_LIBRARY_PATH "$ENV{LD_LIBRARY_PATH}:${Python_BASE_DIR}")
        endif()
    else()
        set(PLUG_INFO_SANDBOX_LIBRARY_PATH "NOT USED ON THIS OS")
    endif()

    set(PLUGIN_INFO_UNSAFE_PLUGIN_RELATIVE_PATH "../../plugin_sandboxed/usd")
    set(PLUG_INFO_SANDBOX_EXECUTABLE_RELATIVE_PATH "../../plugin_sandboxed/usd/SandboxedProcess")
    # Relative paths in plugInfo.json are resolved at runtime relative to the dylib's directory,
    # not relative to the plugInfo.json file itself. So "." resolves to the dylib's parent
    # directory, which in a standalone build is the same directory USD scans for plugins (i.e.
    # what appears in PXR_PLUGINPATH_NAME). For bundled app builds where the dylib and plugin
    # discovery directory differ, PLUG_INFO_PROXY_PLUGIN_PATH must be set to the correct absolute
    # path for that deployment.
    if (NOT DEFINED PLUG_INFO_PROXY_PLUGIN_PATH)
        set(PLUG_INFO_PROXY_PLUGIN_PATH ".")
    endif()
endif()
configure_file(plugInfo.json.in plugInfo.json)
set_target_properties(usdSandboxProxy PROPERTIES RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json)

set_target_properties(usdSandboxProxy PROPERTIES RESOURCE_FILES "${CMAKE_CURRENT_BINARY_DIR}/plugInfo.json:plugInfo.json")

if(WIN32)
    target_include_directories(usdSandboxProxy
    PRIVATE
        "${pxr_ROOT}/include/boost-1_86"
    )
endif()

# The sandbox proxy plugin is installed to a separate location from the normal plugins
if(USD_SANDBOXPROXY_ENABLE_INSTALL)
    install(
        TARGETS  usdSandboxProxy
        RUNTIME  DESTINATION plugin_proxy/usd COMPONENT Runtime
        LIBRARY  DESTINATION plugin_proxy/usd COMPONENT Runtime
        RESOURCE DESTINATION plugin_proxy/usd/usdSandboxProxy/resources COMPONENT Runtime
    )

    install(
        FILES plugInfo.root.json
        DESTINATION plugin_proxy/usd
        RENAME plugInfo.json
        COMPONENT Runtime
    )
endif()
