cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(bench VERSION 0.1)

set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS sample.proto)
add_executable(benchTest benchmarkTest.cpp ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(benchTest PUBLIC ${Protobuf_LIBRARIES})
target_link_libraries(benchTest PUBLIC -lstdc++)
target_link_libraries(benchTest PUBLIC -lsqlite3)
