incdir = include_directories('../')

# Shared stub sources for tests that link production code with
# heavy integration deps (DBus, event logging, global state).
stubs_src = files('test_stubs.cpp')
record_stubs_src = files('test_record_stubs.cpp')

# ===========================================================================
# Group 1: Header-only / lightweight tests (no production .cpp sources)
# ===========================================================================

simple_tests = [
    'property_variant_test',
    'fdr_policy_test',
]

foreach t : simple_tests
    test(
        t,
        executable(
            t.underscorify(),
            t + '.cpp',
            implicit_include_directories: true,
            dependencies: deps + [gtest],
            include_directories: incdir,
        ),
        workdir: meson.current_source_dir(),
    )
endforeach

# ===========================================================================
# Group 2: Tests linking a few production sources + stubs
# ===========================================================================

test(
    'fdr_http_test',
    executable(
        'fdr_http_test',
        'fdr_http_test.cpp',
        '../fdr_log.cpp',
        '../fdr_http.cpp',
        implicit_include_directories: true,
        dependencies: deps + [gtest],
        include_directories: incdir,
    ),
    workdir: meson.current_source_dir(),
)

test(
    'fdr_log_test',
    executable(
        'fdr_log_test',
        'fdr_log_test.cpp',
        '../fdr_log.cpp',
        implicit_include_directories: true,
        dependencies: deps + [gtest],
        include_directories: incdir,
    ),
    workdir: meson.current_source_dir(),
)

test(
    'fdr_redfish_test',
    executable(
        'fdr_redfish_test',
        'fdr_redfish_test.cpp',
        '../fdr_redfish.cpp',
        '../fdr_http.cpp',
        '../fdr_log.cpp',
        implicit_include_directories: true,
        dependencies: deps + [gtest],
        include_directories: incdir,
    ),
    workdir: meson.current_source_dir(),
)

# ===========================================================================
# Group 3: Tests that need stubs for globals / rfEvent / EventSignalHandler
# ===========================================================================

test(
    'fdr_common_test',
    executable(
        'fdr_common_test',
        'fdr_common_test.cpp',
        '../fdr_common.cpp',
        '../fdr_log.cpp',
        stubs_src,
        pbgenerated,
        implicit_include_directories: true,
        dependencies: deps + [gtest],
        include_directories: incdir,
    ),
    workdir: meson.current_source_dir(),
)

ppf_test_srcdir = '-DTEST_SOURCE_DIR="@0@"'.format(meson.current_source_dir())

test(
    'ppf_sanity_test',
    executable(
        'ppf_sanity_test',
        'ppf_sanity_test.cpp',
        '../ppf_sanity.cpp',
        '../fdr_common.cpp',
        '../fdr_log.cpp',
        stubs_src,
        pbgenerated,
        implicit_include_directories: true,
        cpp_args: ppf_test_srcdir,
        dependencies: deps + [gtest],
        include_directories: incdir,
    ),
    workdir: meson.current_source_dir(),
)

test(
    'fdr_store_test',
    executable(
        'fdr_store_test',
        'fdr_store_test.cpp',
        '../fdr_store.cpp',
        '../fdr_common.cpp',
        '../fdr_log.cpp',
        stubs_src,
        pbgenerated,
        implicit_include_directories: true,
        dependencies: deps + [gtest],
        include_directories: incdir,
    ),
    workdir: meson.current_source_dir(),
)

# ===========================================================================
# Group 4: Tests that need record stubs (global fdr pointer, DBus accessors)
# ===========================================================================

test(
    'fdr_record_test',
    executable(
        'fdr_record_test',
        'fdr_record_test.cpp',
        '../fdr_record.cpp',
        '../fdr_store.cpp',
        '../fdr_common.cpp',
        '../fdr_log.cpp',
        stubs_src,
        record_stubs_src,
        pbgenerated,
        implicit_include_directories: true,
        dependencies: deps + [gtest],
        include_directories: incdir,
    ),
    workdir: meson.current_source_dir(),
)
