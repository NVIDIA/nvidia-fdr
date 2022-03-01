cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(fdr VERSION 0.1)

set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS fdr_logs_schema.proto)
add_executable(fdr main.cpp fdr_policy.cpp fdr_record.cpp fdr_store.cpp ${PROTO_SRCS} ${PROTO_HDRS})

find_library(FDRDEPLIBS libyaml-cpp.a)
target_link_libraries(fdr PUBLIC ${FDRDEPLIBS})
target_link_libraries(fdr PUBLIC ${Protobuf_LIBRARIES})
target_link_libraries(fdr PUBLIC -lsystemd)
