add_library(sandboxProtocol SHARED
    "messages.cpp"
    "messageIO.cpp"
    "protocolBase.cpp"
    "hostProtocol.cpp"
    "sandboxProtocol.cpp"
    "assetReader.cpp"
    "assetWriter.cpp"
)

usd_plugin_compile_config(sandboxProtocol)
target_compile_definitions(sandboxProtocol PRIVATE USDSANDBOX_EXPORTS)

target_include_directories(sandboxProtocol
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/"
)

target_link_libraries(sandboxProtocol
PUBLIC
    usdIpc
    usdSerialization
PRIVATE
    usd
    ar
    tf
)

set_target_properties(sandboxProtocol PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")

install(TARGETS sandboxProtocol)
