python3 = find_program('python3')
# yamlconverter = find_program('../tools/yaml-converter.py')

ppf = ''

if get_option('platform_ppf') != ''
    ppf = get_option('platform_ppf')

else
    ppf = 'fdr_ppf_vulcan.yaml'
endif

custom_target(
    ppf,
    build_by_default: true,
    output: ppf,
    input: ppf,
    command: [python3, join_paths(meson.global_source_root(), 'tools', 'yaml-converter.py'), '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: '/etc/nvidia-fdr/platforms',
    install_tag: ['runtime'],
)
