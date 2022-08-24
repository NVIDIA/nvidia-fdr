cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(fdr VERSION 0.1)

# add_subdirectory(libs/yaml-cpp)
find_library(FDRDEPLIBS libyaml-cpp.a)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(fdr main.cpp fdr_policy.cpp fdr_record.cpp fdr_store.cpp dbus_accessor.cpp property_variant.hpp)


target_link_libraries(fdr PUBLIC ${FDRDEPLIBS})

# find_library(STATIC_SQLITE libsqlite3.a)
# target_link_libraries(fdr PUBLIC ${CMAKE_SOURCE_DIR}/libsqlite3.a)

# target_link_libraries(fdr PUBLIC yaml-cpp)
target_link_libraries(fdr PUBLIC -lsystemd)
target_link_libraries(fdr PUBLIC -lsqlite3)
target_link_libraries(fdr PUBLIC -lsdbusplus)