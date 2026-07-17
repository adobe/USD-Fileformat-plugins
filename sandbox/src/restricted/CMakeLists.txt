# Define a new executable target for SandboxedProcess.exe to convert files safely
add_executable(SandboxedProcess
    "sandboxedProcess.cpp"
)

# Include directories
target_include_directories(SandboxedProcess
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)

if(WIN32)
    # Prevent windows.h includes from bringing in conflicts with C++ STL
    target_compile_definitions(SandboxedProcess PRIVATE NOMINMAX)
endif()

# Binary-hardening flags for SandboxedProcess.  This target does not route
# through usd_plugin_compile_config (it is an executable, not a plugin
# shared library), so flags are applied directly here.
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(SandboxedProcess PRIVATE
        /GS  # stack buffer security checks (assert default-on, prevent regression)
    )
    target_link_options(SandboxedProcess PRIVATE
        /DYNAMICBASE   # enable ASLR
        /HIGHENTROPYVA # use full 64-bit address space for ASLR entropy
    )
else()
    # Stack-canary protection and FORTIFY for GCC and Clang (Linux + macOS).
    target_compile_options(SandboxedProcess PRIVATE
        -fstack-protector-strong
        # _FORTIFY_SOURCE=2 requires -O1 or higher; warn-free only under optimisation.
        $<$<CONFIG:Release,RelWithDebInfo>:-D_FORTIFY_SOURCE=2>
    )
    if(UNIX AND NOT APPLE)
        # Full RELRO: lock the GOT read-only after startup.
        # -pie/-fPIE is handled by POSITION_INDEPENDENT_CODE below.
        target_link_options(SandboxedProcess PRIVATE
            -Wl,-z,relro
            -Wl,-z,now
        )
    endif()
endif()

target_link_libraries(SandboxedProcess
PRIVATE
    tf
    sdf
    usd
    arch
    fileformatUtils
    sandboxUtils
    sandboxProtocol
    sandboxHardening
)

# SandboxedProcess must own the libpython link on macOS.
# This standalone executable links the USD core libraries (tf, sdf, usd, ...).
# When USD is built with PXR_PY_UNDEFINED_DYNAMIC_LOOKUP=ON (the macOS default),
# those dylibs leave Python symbols (e.g. _Py_NoneStruct) unresolved for
# flat-namespace dynamic lookup, expecting the host process to have already
# loaded libpython. Unlike the main application, this process does not embed the
# Python interpreter, so nothing loads libpython and dyld aborts at launch with
# "symbol not found in flat namespace '__Py_NoneStruct'". Linking Python::Python
# forces dyld to load the interpreter before any USD dylib touches the Python C
# API. This mirrors the pxr_cpp_bin() change in USD's cmake/macros/Public.cmake
# that lets pxr tools such as usdcat run as standalone executables.
if(APPLE)
    find_package(Python REQUIRED COMPONENTS Development)
    if(TARGET Python::Python)
        target_link_libraries(SandboxedProcess PRIVATE Python::Python)
    endif()
endif()

# Assert PIE (Position Independent Executable) on Linux and macOS.
# Linux: required for correctness -- patchelf >= 0.17.2 corrupts non-PIE ELF executables
# in a way that newer kernels (>= 4.18) reject at load time with SIGSEGV.
# macOS: executables are PIE by default, but we assert it explicitly so the property
# cannot be silently lost by a toolchain or CMake version change.
# CheckPIESupported wires up both the -fPIE compile flag (via POSITION_INDEPENDENT_CODE)
# and the -pie linker flag.
if(UNIX)
    include(CheckPIESupported)
    check_pie_supported()
    set_target_properties(SandboxedProcess PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

set_target_properties(SandboxedProcess PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")

# set(DEST plugin/usd)
set(DEST plugin_sandboxed/usd)

if(USD_SANDBOXPROXY_ENABLE_INSTALL)
    install(
        TARGETS SandboxedProcess
        RUNTIME DESTINATION ${DEST} COMPONENT Runtime
        LIBRARY DESTINATION ${DEST} COMPONENT Runtime
    )
endif()
