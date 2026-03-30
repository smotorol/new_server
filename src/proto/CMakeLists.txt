add_library(dc_proto INTERFACE)
target_include_directories(dc_proto INTERFACE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(dc_proto INTERFACE
    DC_PROTOBUF_AVAILABLE=$<IF:$<BOOL:${DC_PROTOBUF_AVAILABLE}>,1,0>
)

set(DC_PROTO_SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/schema)
file(GLOB DC_PROTO_SCHEMA_FILES ${DC_PROTO_SCHEMA_DIR}/*.proto)
add_custom_target(dc_proto_schema SOURCES ${DC_PROTO_SCHEMA_FILES})

set(DC_PROTO_GENERATED_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated/cpp)
file(GLOB DC_PROTO_GENERATED_SRCS ${DC_PROTO_GENERATED_CPP_DIR}/*.pb.cc)
file(GLOB DC_PROTO_GENERATED_HDRS ${DC_PROTO_GENERATED_CPP_DIR}/*.pb.h)

if(DC_PROTOBUF_AVAILABLE AND DC_PROTO_GENERATED_SRCS)
    add_library(dc_proto_generated STATIC ${DC_PROTO_GENERATED_SRCS} ${DC_PROTO_GENERATED_HDRS})
    dc_apply_common_options(dc_proto_generated)
    dc_set_debug_postfix(dc_proto_generated)
    target_link_libraries(dc_proto_generated PUBLIC protobuf::libprotobuf)
    target_include_directories(dc_proto_generated PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${DC_PROTO_GENERATED_CPP_DIR}
    )
    target_link_libraries(dc_proto INTERFACE dc_proto_generated)
endif()

if(ENABLE_PROTOBUF_CODEGEN AND DC_PROTOBUF_AVAILABLE)
    set(DC_PROTO_CSHARP_OUT_DIR ${CMAKE_SOURCE_DIR}/tools/proto_generated/csharp)
    add_custom_target(dc_proto_codegen
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DC_PROTO_GENERATED_CPP_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DC_PROTO_CSHARP_OUT_DIR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${DC_PROTO_SCHEMA_DIR}
            --cpp_out=${DC_PROTO_GENERATED_CPP_DIR}
            --csharp_out=${DC_PROTO_CSHARP_OUT_DIR}
            ${DC_PROTO_SCHEMA_FILES}
        DEPENDS ${DC_PROTO_SCHEMA_FILES}
        WORKING_DIRECTORY ${DC_PROTO_SCHEMA_DIR}
        COMMENT "Regenerating protobuf C++/C# sources"
        VERBATIM)
endif()
