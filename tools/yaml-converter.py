#!/usr/bin/env python3

"""
yaml-cpp is lacking anchor/alias support
https://github.com/jbeder/yaml-cpp/issues/353

This tools is created as a workaround, it will be no longer needed 
once migrated to different yaml lib with proper anchor/alias support.
"""

import os
import sys
import yaml

class NoAliasDumper(yaml.SafeDumper):
    # This make sure aliases are expanded/copied when dumping
    def ignore_aliases(self, data):
        return True
    
# special loader with duplicate key checking
class UniqueKeyLoader(yaml.SafeLoader):
    def construct_mapping(self, node, deep=False):
        mapping = {}
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)
            if key in mapping:
                err_msg = "Duplicate key \"{}\" found in YAML. Values: \"{}\" and \"{}\"".format(key, mapping[key], value_node.value)
                raise ValueError(err_msg)
            if isinstance(node, yaml.nodes.MappingNode):
                self.flatten_mapping(node)
            mapping[key] = self.construct_object(value_node, deep=deep)
        return super().construct_mapping(node, deep)

if __name__ == "__main__":
    if not (2 <= len(sys.argv) <= 3):
        sys.stderr.write ("""
Usage: {} input.yaml [output.yaml]

""".format(os.path.basename(sys.argv[0])))
        exit(1)

    input_file = sys.argv[1]

    if len(sys.argv) == 3:
        output_file = sys.argv[2]
        if os.path.abspath(input_file) == os.path.abspath(output_file):
            sys.stderr.write ("""
Input/Output are the same file.
Please don't build in source tree, instead setup a separate build directory.

    cmake -B build
    make -C build

""")
            exit(1)
        out = open(output_file, "w")
    else:
        out = sys.stdout
    
    yaml.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, UniqueKeyLoader.construct_mapping)
    with open(input_file, "r") as f:
        doc = yaml.load(f, Loader=UniqueKeyLoader)

    out.write(yaml.dump(doc, indent=2, Dumper=NoAliasDumper))