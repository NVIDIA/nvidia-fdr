cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(fdr VERSION 0.1)

add_subdirectory(libs/yaml-cpp)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")

add_executable(fdr main.cpp fdr_policy.cpp fdr_record.cpp fdr_store.cpp)

# find_library(FDRDEPLIBS libyaml-cpp.a)
# target_link_libraries(fdr PUBLIC ${FDRDEPLIBS})

# find_library(STATIC_SQLITE libsqlite3.a)
# target_link_libraries(fdr PUBLIC ${CMAKE_SOURCE_DIR}/libsqlite3.a)

target_link_libraries(fdr PUBLIC yaml-cpp)
target_link_libraries(fdr PUBLIC -lsystemd)
target_link_libraries(fdr PUBLIC -lsqlite3)
# target_link_libraries(fdr PUBLIC ${CMAKE_SOURCE_DIR}/libs/libsqlite3.a)