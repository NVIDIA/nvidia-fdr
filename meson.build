project(
  'nvidia-fdr', 'cpp',
  version : '0.1',
  meson_version: '>=1.1.1',
  default_options: [
    'prefix=/usr',
    'cpp_std=c++23',
    'warning_level=3',
    #'werror=true',
  ],
)

# slience BOOST_HEADER_DEPRECATED if desired
# add_global_arguments('-DBOOST_ALLOW_DEPRECATED_HEADERS', language : 'cpp')

cmake = import('cmake')

# Generate src from proto file
protoc = find_program('protoc', required : true)
pbgen = generator(protoc, \
  output    : ['@BASENAME@.pb.cc', '@BASENAME@.pb.h'],
  arguments : ['--experimental_allow_proto3_optional', '--proto_path=@CURRENT_SOURCE_DIR@', '--cpp_out=@BUILD_DIR@', '@INPUT@'])
pbgenerated = pbgen.process('fdr_logs_schema.proto')


# Subprojects for dependencie
# 3 Scenarios:
#   - OpenBMC (bitbake):  all dependecies should be available.
#   - Ubuntu amd64 (native build): some OpenBMC specified packages are not available.
#   - Ubuntu Unit Test (docker): some OpenBMC & distro devel packages are not available
libcurl_dep = dependency(
  'libcurl',
  version: '>=7.58.0',
  default_options: [
    'warning_level=0',
    'werror=false',
  ])

sdbusplus_dep = dependency(
  'sdbusplus', 
  fallback : ['sdbusplus', 'sdbusplus_dep'],
  default_options: [ 'tests=disabled' ])

stdplus_dep = dependency(
  'stdplus', 
  fallback : ['stdplus', 'stdplus_dep'],
  default_options: [])

sdeventplus_dep = dependency(
  'sdeventplus', 
  fallback : ['sdeventplus', 'sdeventplus_dep'],
  default_options: [])

yamlcpp_dep = dependency('yaml-cpp', version: '>= 0.5.1', required: false)
if not yamlcpp_dep.found()
    yamlcpp_opts = cmake.subproject_options()
    yamlcpp_opts.add_cmake_defines({
      'CMAKE_BUILD_TYPE'                : 'Release',
      'CMAKE_POSITION_INDEPENDENT_CODE' : 'ON',
      'YAML_CPP_BUILD_TOOLS'            : 'OFF',
      'YAML_CPP_BUILD_TESTS'            : 'OFF',
      'YAML_CPP_BUILD_CONTRIB'          : 'OFF',
      'BUILD_SHARED_LIBS'               : 'OFF'
    })
    yamlcpp_dep = cmake.subproject(
      'yaml-cpp',
      options: yamlcpp_opts
    ).dependency('yaml-cpp')
endif

deps = [
  libcurl_dep,
  dependency('fmt'),
  dependency('protobuf'),
  sdbusplus_dep,
  dependency('systemd'),
  yamlcpp_dep,
  stdplus_dep,
  sdeventplus_dep
]

if get_option('logging') == 'spdlog'
  add_project_arguments('-DFDR_USE_SPDLOG', language : 'cpp')

  spdlog_dep = dependency(
    'spdlog', 
    fallback : ['spdlog', 'spdlog_dep'],
    default_options: [
      'tests=false'
  ])

  deps += spdlog_dep

else
  phosphor_logging_dep = dependency(
    'phosphor-logging', 
    fallback : ['phosphor-logging', 'phosphor_logging_dep'],
    default_options: [
      'tests=disabled'
  ])

  deps += phosphor_logging_dep

endif

shmem = dependency('nvidia-shmem')
deps += shmem

if get_option('tests').enabled()
    gtest = dependency('gtest', main: true, disabler: true, required: false)
    gmock = dependency('gmock', disabler: true, required: false)
    if not gtest.found() or not gmock.found()
        gtest_proj = import('cmake').subproject('googletest', required: false)
        if gtest_proj.found()
            gtest = declare_dependency(
                dependencies: [
                    dependency('threads'),
                    gtest_proj.dependency('gtest'),
                    gtest_proj.dependency('gtest_main'),
                ]
            )
            gmock = gtest_proj.dependency('gmock')
        else
            assert(
                not get_option('tests').enabled(),
                'Googletest is required if tests are enabled'
            )
        endif
    endif
endif

src = [
  'dbus_accessor.cpp',
  'fdr.cpp',
  'fdr_bookoferrors.cpp',
  'fdr_common.cpp',
  'fdr_compact.cpp',
  'fdr_events.cpp',
  'fdr_http.cpp',
  'fdr_log.cpp',
  'fdr_policy.cpp',
  'fdr_record.cpp',
  'fdr_redfish.cpp',
  'fdr_store.cpp',
  'fdr_timers.cpp',
  'main.cpp',
  'ppf_sanity.cpp',
  'property_variant.hpp',
  'fdr_grp_update.cpp'
]

executable('nvidia-fdr',
  src, pbgenerated,
  dependencies: deps,
  install: true,
)

subdir('platforms')
subdir('tools')

if get_option('tests').enabled()
  subdir('tests')
endif