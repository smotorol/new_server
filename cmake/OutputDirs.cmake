set(DC_OUTPUT_ROOT ${CMAKE_BINARY_DIR}/out)

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} cfg_upper)

    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} ${DC_OUTPUT_ROOT}/${cfg}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} ${DC_OUTPUT_ROOT}/${cfg}/bin)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} ${DC_OUTPUT_ROOT}/${cfg}/bin)
endforeach()
