cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(fdr VERSION 0.1)

set(Protobuf_USE_STATIC_LIBS_ON)
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
# add_subdirectory(libs/yaml-cpp)
find_library(FDRDEPLIBS libyaml-cpp.a)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS fdr_logs_schema.proto)

add_executable(fdr main.cpp fdr_policy.cpp fdr_record.cpp fdr_store.cpp dbus_accessor.cpp property_variant.hpp ${PROTO_SRCS} ${PROTO_HDRS})


target_link_libraries(fdr PUBLIC ${FDRDEPLIBS})
target_link_libraries(fdr PUBLIC ${Protobuf_LIBRARIES})

# find_library(STATIC_SQLITE libsqlite3.a)
# target_link_libraries(fdr PUBLIC ${CMAKE_SOURCE_DIR}/libsqlite3.a)

# target_link_libraries(fdr PUBLIC yaml-cpp)
target_link_libraries(fdr PUBLIC -lsystemd)
target_link_libraries(fdr PUBLIC -lsqlite3)
target_link_libraries(fdr PUBLIC -lsdbusplus)