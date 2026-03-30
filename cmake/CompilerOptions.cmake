function(dc_apply_common_options target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /utf-8
            /permissive-
            /W4
            /EHsc
            /Zc:__cplusplus
            /bigobj
            /MP

            $<$<CONFIG:Debug>:/Od>
            $<$<CONFIG:Debug>:/Zi>
            $<$<CONFIG:Debug>:/Ob0>
            $<$<CONFIG:Debug>:/RTC1>

            $<$<CONFIG:Release>:/O2>

            $<$<CONFIG:RelWithDebInfo>:/O2>
            $<$<CONFIG:RelWithDebInfo>:/Zi>
        )

        target_link_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:/DEBUG>
            $<$<CONFIG:RelWithDebInfo>:/DEBUG>
        )

        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )

        set_property(TARGET ${target_name} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic

            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-g>

            $<$<CONFIG:Release>:-O3>

            $<$<CONFIG:RelWithDebInfo>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-g>
        )
    endif()
endfunction()

function(dc_set_debug_postfix target_name)
    set_target_properties(${target_name} PROPERTIES DEBUG_POSTFIX "_d")
endfunction()
