cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(fdr VERSION 0.1)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_executable(fdr main.cpp fdr_policy.cpp fdr_record.cpp fdr_store.cpp)

find_library(FDRDEPLIBS libyaml-cpp.a)
target_link_libraries(fdr PUBLIC ${FDRDEPLIBS})
target_link_libraries(fdr PUBLIC -lsystemd)
target_link_libraries(fdr PUBLIC -lsqlite3)
