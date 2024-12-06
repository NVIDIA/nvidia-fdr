#!/usr/bin/env python3

import os
import sys
import yaml
import zipfile
import subprocess

class NoAliasDumper(yaml.SafeDumper):
    # This ensures aliases are expanded/copied when dumping
    def ignore_aliases(self, data):
        return True

# Special loader with duplicate key checking
class UniqueKeyLoader(yaml.SafeLoader):
    def construct_mapping(self, node, deep=False):
        mapping = {}
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)
            if key in mapping:
                err_msg = f'Duplicate key "{key}" found in YAML. Values: "{mapping[key]}" and "{value_node.value}"'
                raise ValueError(err_msg)
            if isinstance(node, yaml.nodes.MappingNode):
                self.flatten_mapping(node)
            mapping[key] = self.construct_object(value_node, deep=deep)
        return super().construct_mapping(node, deep)

def zip_and_rename(file_path):
    # Create a tar file

    tar_path = f"{file_path}.tar.xz"   
    subprocess.run(['tar', '-caf', tar_path, file_path])

    print(f"Tarred output to: {tar_path}")
    
    # Delete the original file
    os.remove(file_path)
    print(f"Deleted original file: {file_path}")
    
    # Rename the zip file to the output name
    renamed_zip_path = file_path
    os.rename(tar_path, renamed_zip_path)
    print(f"Renamed zip file to: {renamed_zip_path}")

if __name__ == "__main__":
    if not (2 <= len(sys.argv) <= 3):
        sys.stderr.write(f"""
Usage: {os.path.basename(sys.argv[0])} input.yaml [output.yaml]

""")
        exit(1)

    input_file = sys.argv[1]

    if len(sys.argv) == 3:
        output_file = sys.argv[2]
        if os.path.abspath(input_file) == os.path.abspath(output_file):
            sys.stderr.write(f"""
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
    
    if len(sys.argv) == 3:
        out.close()
        zip_and_rename(output_file)
