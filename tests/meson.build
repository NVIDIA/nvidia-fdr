test_src = declare_dependency(
          sources: [
            '../fdr_http.cpp'])

tests = [
  'fdr_http_test',
]

incdir = include_directories('../')


foreach t : tests
  test(t, executable(t.underscorify(), t + '.cpp',
                     implicit_include_directories: true,
                     #link_args: dynamic_linker,
                     #build_rpath: get_option('oe-sdk').enabled() ? rpath : '',
                     dependencies: deps + [gtest, test_src],
                     include_directories: incdir,
                     ),
       workdir: meson.current_source_dir())
endforeach
