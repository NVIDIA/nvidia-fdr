cmake_minimum_required(VERSION 3.10)
# cmake_policy(SET CMP0135 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(fdr VERSION 0.1)

# add_compile_options(-D__ARM_PCS_VFP)


#Download & build yaml-cpp
include(ExternalProject)
ExternalProject_Add(
	dependency-yaml-cpp
  PREFIX my-yaml-cpp
  GIT_REPOSITORY "https://github.com/jbeder/yaml-cpp.git"
  GIT_TAG yaml-cpp-0.6.3
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)
include_directories("${PROJECT_BINARY_DIR}/my-yaml-cpp/include")
link_directories(${PROJECT_BINARY_DIR}/my-yaml-cpp/lib)

#Download & build protobuf
ExternalProject_Add(
  dependency-protobuf
  PREFIX my-protobuf
  URL https://github.com/protocolbuffers/protobuf/archive/refs/tags/v21.12.tar.gz
  # URL https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.12.4.tar.gz
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -Dprotobuf_BUILD_TESTS:BOOL=OFF 
)
include_directories("${PROJECT_BINARY_DIR}/my-protobuf/include")
link_directories(${PROJECT_BINARY_DIR}/my-protobuf/lib)

add_custom_command(
  OUTPUT fdr_logs_schema.pb.cc
  # WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND protoc --cpp_out=. --proto_path ${CMAKE_CURRENT_SOURCE_DIR} fdr_logs_schema.proto
  DEPENDS fdr_logs_schema.proto
  VERBATIM
  )
include_directories("${PROJECT_BINARY_DIR}")

add_executable(fdr main.cpp fdr_policy.cpp fdr_record.cpp fdr_store.cpp dbus_accessor.cpp property_variant.hpp fdr_logs_schema.pb.cc)
target_link_libraries(fdr PRIVATE -lyaml-cpp)
target_link_libraries(fdr PRIVATE -lprotobuf)
target_link_libraries(fdr PRIVATE -lsqlite3)
target_link_libraries(fdr PRIVATE -lsdbusplus)
target_link_libraries(fdr PRIVATE -lsystemd)

add_dependencies(fdr dependency-yaml-cpp dependency-protobuf)


