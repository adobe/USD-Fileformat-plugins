add_library(sandboxHardening SHARED)

usd_plugin_compile_config(sandboxHardening)
target_compile_definitions(sandboxHardening PRIVATE USDSANDBOX_EXPORTS)

if(WIN32)
    target_sources(sandboxHardening PRIVATE "hardeningWin.cpp")
elseif(APPLE)
    target_sources(sandboxHardening PRIVATE "hardeningMac.cpp" "sandboxProfile.cpp")
elseif(UNIX)
    target_sources(sandboxHardening PRIVATE "hardeningLinux.cpp")

    include(ExternalProject)
    ExternalProject_Add(
        libseccomp
        PREFIX ${CMAKE_BINARY_DIR}/libseccomp
        GIT_REPOSITORY "https://github.com/seccomp/libseccomp.git"
        GIT_TAG "v2.6.0"
        CONFIGURE_COMMAND sh -c "cd ${CMAKE_BINARY_DIR}/libseccomp/src/libseccomp && ./autogen.sh && ./configure && echo CONFIGURE SUCCESS"
        BUILD_COMMAND sh -c "cd ${CMAKE_BINARY_DIR}/libseccomp/src/libseccomp && make -j$(nproc) VERBOSE=1 && echo BUILD SUCCESS"
        INSTALL_COMMAND sh -c "cd ${CMAKE_BINARY_DIR}/libseccomp/src/libseccomp && echo INSTALL SUCCESS"
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        LOG_OUTPUT_ON_FAILURE 1
    )
    install(DIRECTORY ${CMAKE_BINARY_DIR}/libseccomp/src/libseccomp/src/.libs DESTINATION libseccomp)
endif()

target_include_directories(sandboxHardening
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include/"
)

target_link_libraries(sandboxHardening
PUBLIC
    usdIpc
PRIVATE
    usd
)

if(APPLE)
    # sandboxProfile.cpp uses getExecutableDirectory() from sandboxUtils (utilities.h).
    target_link_libraries(sandboxHardening PRIVATE sandboxUtils)
endif()

if(UNIX AND NOT APPLE)
    add_dependencies(sandboxHardening libseccomp)
    target_include_directories(sandboxHardening
        PRIVATE ${CMAKE_BINARY_DIR}/libseccomp/src/libseccomp/include)
    target_link_directories(sandboxHardening
        PRIVATE ${CMAKE_BINARY_DIR}/libseccomp/src/libseccomp/src/.libs)
    target_link_libraries(sandboxHardening PRIVATE seccomp rt)
endif()

set_target_properties(sandboxHardening PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")

install(TARGETS sandboxHardening)
