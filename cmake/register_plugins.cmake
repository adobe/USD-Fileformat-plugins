include_guard(GLOBAL)

function(fileformats_register_plugin plugin)
    get_property(_enabled_plugins GLOBAL PROPERTY USD_FILEFORMATS_ENABLED_PLUGINS)

    list(APPEND _enabled_plugins ${plugin})

    list(REMOVE_DUPLICATES _enabled_plugins)

    set_property(GLOBAL PROPERTY USD_FILEFORMATS_ENABLED_PLUGINS ${_enabled_plugins})

    if(_FILEFORMAT_SANDBOXED)
        set_property(GLOBAL APPEND PROPERTY USD_FILEFORMATS_SANDBOXED_PLUGINS ${plugin})
    endif()
endfunction()
