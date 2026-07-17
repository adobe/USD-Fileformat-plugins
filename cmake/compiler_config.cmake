function(usd_plugin_compile_config TARGET)

    set(CMAKE_CXX_EXTENSIONS OFF)
    if (CMAKE_CXX_STANDARD EQUAL 20)
        target_compile_features(${TARGET} PUBLIC cxx_std_20)
    else()
        target_compile_features(${TARGET} PUBLIC cxx_std_17)
    endif()

    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET} PRIVATE
            /utf-8 # treat source and execution character sets as UTF-8
            /W3 # we want to be as strict as possible
            $<$<BOOL:${BUILD_STRICT_MODE}>:/WX>
            # enable two-phase name lookup and other strict checks (binding a non-const reference to a temporary, etc..)
            $<$<BOOL:$<VERSION_GREATER:${USD_BOOST_VERSION},106600>>:/permissive->
            /Zi # enable pdb generation.
            /Zc:rvalueCast # standards compliant.
            # https://developercommunity.visualstudio.com/content/problem/914943/zcinline-removes-extern-symbols-inside-anonymous-n.html
            $<IF:$<VERSION_GREATER_EQUAL:${MSVC_VERSION},1920>,/Zc:inline-,/Zc:inline> # don't strip "arch_ctor_<name>" symbols (VS2019+)
            /MP # enable multiprocessor builds.
            /EHsc # enable exception handling.
            /w35038 # enable initialization order as a level 3 warning
            /wd4180 # disable warnings
            /wd4244
            /wd4267
            /wd4273
            /wd4305
            /wd4506
            /wd4996
            /wd4180
            /wd4251 # exporting STL classes
            /Zc:__cplusplus # Make sure substance engine zero initilization works
            /GS  # stack buffer security checks (assert default-on, prevent silent regression)
        )
        target_compile_definitions(${TARGET} PRIVATE
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _SCL_SECURE_NO_WARNINGS
            _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
            BOOST_ALL_DYN_LINK
            BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE
            HAVE_SNPRINTF # Needed to prevent Python from adding a define for snprintf (VS2015+)
            TBB_USE_ASSERT
            TBB_USE_THREADING_TOOLS
            TBB_SUPPRESS_DEPRECATED_MESSAGES
            TBB_USE_PERFORMANCE_WARNINGS
        )

    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${TARGET} PRIVATE
            -Wall
            $<$<BOOL:${BUILD_STRICT_MODE}>:-Werror>
            -Wno-deprecated
            -Wno-deprecated-declarations
            -Wno-unused-local-typedefs
            -m64
            -Wrange-loop-analysis
            -fstack-protector-strong
            # _FORTIFY_SOURCE=2 is a no-op without optimization and warns at -O0; gate to
            # Release/RelWithDebInfo where -O2 or higher is guaranteed.
            $<$<CONFIG:Release,RelWithDebInfo>:-D_FORTIFY_SOURCE=2>
        )
        target_compile_definitions(${TARGET} PRIVATE
            NOMINMAX
            TBB_USE_ASSERT
            TBB_USE_THREADING_TOOLS
            TBB_SUPPRESS_DEPRECATED_MESSAGES
            TBB_USE_PERFORMANCE_WARNINGS
        )

    else()
        target_compile_options(${TARGET} PRIVATE
            -Wall
            $<$<BOOL:${BUILD_STRICT_MODE}>:-Werror>
            -msse3
            -Wno-deprecated
            -Wno-deprecated-declarations
            -Wno-unused-local-typedefs
            -m64
            -fstack-protector-strong
            # _FORTIFY_SOURCE=2 is a no-op without optimization and warns at -O0; gate to
            # Release/RelWithDebInfo where -O2 or higher is guaranteed.
            $<$<CONFIG:Release,RelWithDebInfo>:-D_FORTIFY_SOURCE=2>
        )
        target_compile_definitions(${TARGET} PRIVATE
            NOMINMAX
            TBB_USE_ASSERT
            TBB_USE_THREADING_TOOLS
            TBB_SUPPRESS_DEPRECATED_MESSAGES
            TBB_USE_PERFORMANCE_WARNINGS
        )
    endif()

    # Apply link-side hardening (RELRO, /DYNAMICBASE) via the companion helper.
    # Defined below in this file; callable here because both functions are fully
    # registered by the time any CMakeLists.txt calls usd_plugin_compile_config.
    usd_plugin_link_config(${TARGET})

endfunction()

# usd_plugin_link_config(TARGET)
#
# Apply link-side binary-hardening flags to TARGET (plugin shared library or executable).
# This is invoked by usd_plugin_compile_config; call it directly only for targets
# that don't use usd_plugin_compile_config.
# Platform behaviour:
#   Linux  : Full RELRO (-z relro -z now) + /DYNAMICBASE equivalent is PIE handled
#            separately per target.  SandboxedProcess also needs -pie at link, but
#            that is set via POSITION_INDEPENDENT_CODE on the target, not here.
#   macOS  : Mach-O has no RELRO segment; no extra link flags needed.
#   Windows: Assert /DYNAMICBASE and /HIGHENTROPYVA (default-on, but explicit to
#            prevent silent regression from linker subsystem changes).
function(usd_plugin_link_config TARGET)
    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        # /DYNAMICBASE: enable ASLR (address-space layout randomisation).
        # /HIGHENTROPYVA: use the full 64-bit address space for ASLR entropy.
        # Both are default-on for x64 MSVC but asserted here to prevent regression.
        target_link_options(${TARGET} PRIVATE
            /DYNAMICBASE
            /HIGHENTROPYVA
        )
    elseif(UNIX AND NOT APPLE)
        # Full RELRO: makes the GOT read-only after dynamic linking completes,
        # preventing attacker overwrites of function pointers.
        # -z relro alone (partial RELRO) leaves the GOT writable during execution;
        # -z now (BIND_NOW) resolves all symbols at load time so the full GOT can
        # be locked.  The combined cost is a slight increase in startup latency,
        # acceptable for a file-conversion process that is not latency-critical.
        target_link_options(${TARGET} PRIVATE
            -Wl,-z,relro
            -Wl,-z,now
        )
    endif()
endfunction()
