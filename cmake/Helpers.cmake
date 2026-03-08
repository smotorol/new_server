function(dc_target_defaults target_name)
    dc_apply_common_options(${target_name})
    dc_set_debug_postfix(${target_name})
    enable_msvc_release_pdb(${target_name})
endfunction()

function(dc_target_src_root target_name)
    target_include_directories(${target_name} PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    )
endfunction()

function(dc_set_vs_workdir target_name)
    if(MSVC)
        set_target_properties(${target_name} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG}"
        )
    endif()
endfunction()

function(collect_sources out_var base_dir)
  file(GLOB_RECURSE _src CONFIGURE_DEPENDS
    ${base_dir}/*.cpp
    ${base_dir}/*.c
    ${base_dir}/*.h
    ${base_dir}/*.hpp
    ${base_dir}/*.inl
  )
  set(${out_var} ${_src} PARENT_SCOPE)
endfunction()

function(group_sources base_dir prefix files)
  if (MSVC)
    # Visual Studio 솔루션 필터를 실제 폴더 트리처럼 보여줌
    source_group(TREE ${base_dir} PREFIX "${prefix}" FILES ${files})
  endif()
endfunction()

function(enable_msvc_release_pdb target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:/Zi>
            $<$<CONFIG:RelWithDebInfo>:/Zi>
        )

        target_link_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:/DEBUG>
            $<$<CONFIG:RelWithDebInfo>:/DEBUG>
        )
    endif()
endfunction()
