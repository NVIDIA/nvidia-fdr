#!/usr/bin/env python3

import os
import sys
import yaml

class NoAliasDumper(yaml.SafeDumper):
    # This make sure aliases are expanded/copied when dumping
    def ignore_aliases(self, data):
        return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.stderr.write ("""
Usage: {} yaml-file

""".format(os.path.basename(sys.argv[0])))
        exit(1)

    with open(sys.argv[1], "r") as f:
        doc = yaml.safe_load(f)

    print (yaml.dump(doc, indent=2, Dumper=NoAliasDumper))