The yaml-cpp library handles anchors and aliases in an unusual way. To workaround the issue, the yaml need to be converted by duplicating the content of anchors.

However, the tools like `yq` or `yaml2json` are not available in OpenBMC, hence `tools/yaml-convert.py` which use `PyYAML` were created for that conversion.

Furthermore, the conversion should be done in build time instead of runtime.