#!/usr/bin/env python3

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
    if len(sys.argv) < 2:
        sys.stderr.write ("""
            Usage: {} yaml-file
            """.format(os.path.basename(sys.argv[0])))
        exit(1)
    
    yaml.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, UniqueKeyLoader.construct_mapping)
    with open(sys.argv[1], "r") as f:
        doc = yaml.load(f, Loader=UniqueKeyLoader)

    print (yaml.dump(doc, indent=2, Dumper=NoAliasDumper))