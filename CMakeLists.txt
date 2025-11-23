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

set(PROTO_FILES proto/ocr_service.proto)
set(PROTO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/proto")

add_library(ocr_grpc_proto ${PROTO_FILES})
target_link_libraries(ocr_grpc_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
)
target_include_directories(ocr_grpc_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET ocr_grpc_proto LANGUAGE cpp)
protobuf_generate(TARGET ocr_grpc_proto LANGUAGE grpc 
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

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
    Qt6::Core
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