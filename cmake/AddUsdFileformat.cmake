# Empty list that will be populated with the sandboxed file formats
set(USD_FILEFORMATS_SANDBOXED_EXTENSIONS)

# Add a new file format to the build. This macro will add the relevant subdirectory and determine
# if the format should be sandboxed. If it is, it will add the extensions to the
# USD_FILEFORMATS_SANDBOXED_EXTENSIONS list and create relevant variables.
#
# New variables:
# - USD${FILEFORMAT}_DESTINATION: Where the fileformat libraries will be installed. This will be
#                                 either bin/plugin/usd (for regular fileformats) or
#                                 bin/plugin_sandboxed/usd (for sandboxed fileformats).
#                                 Example: USDFBX_DESTINATION
#
# This function also requires the fileformat CMakeLists.txt to set ${FILEFORMAT}_EXT_LIST to a
# list of extensions that the format supports. These must be case sensitive, so the sandbox proxy
# resolver can find all variants of a file extension.
#
# @param SUBDIRECTORY_NAME The name of the subdirectory to add.
# @param SANDBOXED True if the fileformat should be sandboxed, false otherwise.
macro(add_usd_fileformat SUBDIRECTORY_NAME SANDBOXED)
    # Ensure the format name is uppercase for use in variables
    string(TOUPPER ${SUBDIRECTORY_NAME} FILEFORMAT)
    if(NOT DEFINED USD${FILEFORMAT}_DESTINATION)
        if (${SANDBOXED})
            set(USD${FILEFORMAT}_DESTINATION "plugin_sandboxed/usd")
            # Set before add_subdirectory so the child scope can read it
            set(_FILEFORMAT_SANDBOXED TRUE)
        else()
            set(USD${FILEFORMAT}_DESTINATION "plugin/usd")
            set(_FILEFORMAT_SANDBOXED FALSE)
        endif()
    endif()

    add_subdirectory(${SUBDIRECTORY_NAME})

    # The fileformat CMakeLists.txt should have set ${FILEFORMAT}_EXT_LIST, so we can save it
    if (${SANDBOXED})
        if (NOT DEFINED ${FILEFORMAT}_EXT_LIST)
            message(FATAL_ERROR "The fileformat ${FILEFORMAT} does not define the "
                                "${FILEFORMAT}_EXT_LIST variable required for sandboxing.")
        endif()
        list(APPEND USD_FILEFORMATS_SANDBOXED_EXTENSIONS ${${FILEFORMAT}_EXT_LIST})
    endif()
endmacro()
