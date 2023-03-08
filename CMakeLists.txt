cmake_minimum_required(VERSION 3.10)
# cmake_policy(SET CMP0135 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(fdr VERSION 0.1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Add protobuf
#set(Protobuf_USE_STATIC_LIBS_ON)
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

# Compile the proto file
include_directories(${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS fdr_logs_schema.proto)

option(YOCTO "Enable Building in Yocto" OFF)

if(NOT YOCTO)
  include(ExternalProject)
  ExternalProject_Add(sdbusplus
    PREFIX ${CMAKE_SOURCE_DIR}/sdbusplus
    GIT_REPOSITORY https://github.com/openbmc/sdbusplus.git
    # This commit id is C++17 based, while the latest were C++20 based.
    # If latest version is desired, it can be compiled by g++-12 on Ubuntu 22.04
    GIT_TAG 1778b12b1d304b759fd6b86f25f8efb74870a953
    CONFIGURE_COMMAND ""
    BUILD_COMMAND cd ${CMAKE_SOURCE_DIR}/sdbusplus/src/sdbusplus && meson build -Dtests=disabled -Dexamples=disabled  -Ddefault_library=static && ninja -C build libsdbusplus.a
    INSTALL_COMMAND ""
  )
  include_directories (${CMAKE_SOURCE_DIR}/sdbusplus/src/sdbusplus/include)
  link_directories (${CMAKE_SOURCE_DIR}/sdbusplus/src/sdbusplus/build)
endif()

add_executable(fdr main.cpp fdr_policy.cpp fdr_record.cpp fdr_store.cpp dbus_accessor.cpp property_variant.hpp fdr_http.cpp fdr_redfish.cpp ${PROTO_SRCS} ${PROTO_HDRS})

target_link_libraries(fdr PRIVATE ${Protobuf_LIBRARIES})
target_link_libraries(fdr PRIVATE -lyaml-cpp)
target_link_libraries(fdr PRIVATE -lprotobuf)
target_link_libraries(fdr PRIVATE -lsqlite3)
target_link_libraries(fdr PRIVATE -lsdbusplus)
target_link_libraries(fdr PRIVATE -lsystemd)
target_link_libraries(fdr PRIVATE -lcurl)

if (NOT YOCTO)
  add_dependencies (fdr sdbusplus)
endif ()

# Install fdr
install(TARGETS fdr RUNTIME DESTINATION bin)
install(DIRECTORY platforms DESTINATION /etc/fdr)
