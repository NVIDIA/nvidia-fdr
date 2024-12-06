python3 = find_program('python3')
# yamlconverter = find_program('../tools/yaml-converter.py')

ppf_m = ''

if get_option('platform_ppf') != ''
    ppf_m = get_option('platform_ppf')
    ppf_m = ppf_m.split(',')

else
    ppf_m = ['fdr_ppf_vulcan.yaml']
endif


foreach ppf : ppf_m
custom_target(
    ppf,
    build_by_default: true,
    input: ppf,
    output: ppf,
    command: [python3, join_paths(meson.global_source_root(), 'tools', 'yaml-converter.py'), '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: '/etc/nvidia-fdr/platforms',
    install_tag: ['runtime'],
)
endforeach