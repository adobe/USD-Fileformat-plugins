function(usd_plugin_compile_config TARGET)

    set(CMAKE_CXX_EXTENSIONS OFF)
    target_compile_features(${TARGET} PUBLIC cxx_std_17)

    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${TARGET} PRIVATE
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
        )
        target_compile_definitions(${TARGET} PRIVATE
            NOMINMAX
            TBB_USE_ASSERT
            TBB_USE_THREADING_TOOLS
            TBB_SUPPRESS_DEPRECATED_MESSAGES
            TBB_USE_PERFORMANCE_WARNINGS
        )
    endif()

endfunction()