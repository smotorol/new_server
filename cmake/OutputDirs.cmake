set(DC_RUNTIME_ROOT ${CMAKE_SOURCE_DIR}/Bin)
set(DC_LIBRARY_ROOT ${CMAKE_SOURCE_DIR}/Lib)

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} cfg_upper)

    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} ${DC_LIBRARY_ROOT}/${cfg})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} ${DC_RUNTIME_ROOT}/${cfg})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} ${DC_RUNTIME_ROOT}/${cfg})
endforeach()
