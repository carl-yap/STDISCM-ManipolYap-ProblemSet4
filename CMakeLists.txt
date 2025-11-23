# CMakeList.txt : CMake project for ManipolYap_ProblemSet4, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.16)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("ManipolYap_ProblemSet4")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(VCPKG_ROOT "C:/tools/vcpkg/installed/x64-windows")
include_directories(${VCPKG_ROOT}/include)
link_directories(${VCPKG_ROOT}/lib)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(Tesseract REQUIRED)

# Set protobuf variables for compatibility
set(PROTOBUF_PROTOC_EXECUTABLE $<TARGET_FILE:protobuf::protoc>)

# Generate gRPC code
set(PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/proto/ocr_service.proto")
set(PROTO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/proto")

# Get generated file names
set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/ocr_service.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/ocr_service.pb.h")
set(GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/ocr_service.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/ocr_service.grpc.pb.h")

# Find protoc and grpc_cpp_plugin
find_program(PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# Custom command to generate protobuf and gRPC files
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
         -I${PROTO_PATH}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating protobuf and gRPC files from ${PROTO_FILES}"
    VERBATIM
)

# Create a custom target to ensure generation happens first
add_custom_target(generate_proto_files DEPENDS ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS})

# Create library with generated files
add_library(ocr_grpc_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS})
add_dependencies(ocr_grpc_proto generate_proto_files)
target_link_libraries(ocr_grpc_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
        gRPC::grpc++_reflection
)
target_include_directories(ocr_grpc_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# Mark generated files
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS} 
    PROPERTIES GENERATED TRUE)

# Server's exec
add_executable (ps4_server
	src/server/main.cpp
    src/server/ocr_service.h
    src/server/ocr_service.cpp
    src/server/ocr_processor.h
    src/server/ocr_processor.cpp
)
target_link_libraries(ps4_server PRIVATE 
	ocr_grpc_proto
    tesseract55
    leptonica-1.85.0
    gRPC::grpc++
)
target_include_directories(ps4_server PRIVATE 
    ${Tesseract_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Client's exec
add_executable(ps4_client
    src/client/main.cpp
)
target_link_libraries(ps4_client PRIVATE 
	ocr_grpc_proto
    Qt6::Core 
    Qt6::Widgets
    gRPC::grpc++
)
target_include_directories(ps4_client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})