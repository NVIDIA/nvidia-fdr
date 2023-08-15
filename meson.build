project(
  'nvidia-fdr', 'cpp',
  version : '0.1',
  default_options: [
    'prefix=/usr',
    'cpp_std=c++20',
    'warning_level=3',
    #'werror=true',
  ],
)

# Generate src from proto file
protoc = find_program('protoc', required : true)
pbgen = generator(protoc, \
  output    : ['@BASENAME@.pb.cc', '@BASENAME@.pb.h'],
  arguments : ['--proto_path=@CURRENT_SOURCE_DIR@', '--cpp_out=@BUILD_DIR@', '@INPUT@'])
pbgenerated = pbgen.process('fdr_logs_schema.proto')

# If sdbusplus exist (OpenBMC case): use the distro lib,
# Otherwise (non OpenBMC case): static link from the subprojects
sdbusplus_dep = dependency(
  'sdbusplus', 
  fallback : ['sdbusplus', 'sdbusplus_dep'],
  default_options: ['default_library=static'])

# other deps
deps = [
  dependency('libcurl'),
  dependency('protobuf'),
  dependency('spdlog'),
  dependency('sqlite3'),
  dependency('systemd'),
  dependency('yaml-cpp'),
  sdbusplus_dep
]

src = [
  'dbus_accessor.cpp',
  'fdr.cpp',
  'fdr_bookoferrors.cpp',
  'fdr_common.cpp',
  'fdr_compact.cpp',
  'fdr_events.cpp',
  'fdr_http.cpp',
  'fdr_policy.cpp',
  'fdr_record.cpp',
  'fdr_redfish.cpp',
  'fdr_store.cpp',
  'main.cpp',
  'ppf_sanity.cpp',
  'property_variant.hpp',
]

executable('nvidia-fdr',
  src, pbgenerated,
  dependencies: deps,
  install: true,
)

subdir('platforms')
subdir('tools')