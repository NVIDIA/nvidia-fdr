import telemetry_catalog
import configargparse
import pprint
import json
import yaml
import csv
import sys
import os
import re

def parse_fdr_dump(fdr_logs_dir):
    # Parse the fdr dump
    fdr_logs_ids = {}
    for (root,dirs,files) in os.walk(fdr_logs_dir, topdown=True):
        root_name = os.path.basename(root)
        if("BootCount" in root_name):
            # print("[INFO]", "Processing BootCount directory", root_name)
            _, boot_count, _, timestamp = root_name.split("_")
            if not boot_count in fdr_logs_ids:
                fdr_logs_ids[boot_count] = set()
            for file in files:
                if "hifi.dat" in file: continue
                comp_class, comp_id, filetype = file.split(".", 2)
                print("[INFO]", "Filename", os.path.join(root_name, file))
                comp_class = comp_class.upper()
                with open(os.path.join(root, file), "r") as f:
                    for line in f:
                        fdr_json_log = json.loads(line)
                        try:
                            param_id = fdr_json_log["ParamID"]
                            # FDR dump should be decoded with -kn option.
                            param_name = fdr_json_log["ParamName"]
                            # Sometimes there is no ParamValue in the FDR dump logs.
                        except Exception as e:
                            if "ParamID" not in fdr_json_log:
                                print("[ERROR] ParamID missing in the file", os.path.join(root_name, file))
                            if "ParamName" not in fdr_json_log:
                                print("[ERROR] ParamName missing for ID", param_id, "in the file", os.path.join(root_name, file))
                            continue
                        try:
                            # Formatting the ID as: comp_class-param_name[0][comp_id][param_idx]
                            suffix = f"[{comp_id.split('_')[-1]}]"
                            indices = re.findall(r'\[(\d+)\]', param_name)
                            if len(indices) == 1:
                                suffix += f"[{indices[0]}]"
                            else:
                                suffix += "[0]"
                            id = f"{comp_class}-{param_name.split('[')[0]}{suffix}"
                            fdr_logs_ids[boot_count].add(id)

                        except Exception as e:
                            print("[Exception]", e)
    return fdr_logs_ids

def parse_exempt_list(exempt_list):
    global exempt_dict
    if exempt_list == None: 
        exempt_dict = {"TGUID": [], "ParamClass": []}

    with open(exempt_list, "r") as f:
        exempt_dict = yaml.safe_load(f.read())

    exempt_dict["TGUID"].insert(0, "exclude")
    exempt_dict["ParamClass"].insert(0, "exclude")

    return exempt_dict


if __name__ == "__main__":
    argget = configargparse.ArgParser()
    argget.add_argument('-c', '--config_file', required=False, is_config_file=True, help='Config file path.')
    argget.add_argument('-t', '--telemetry_catalog', required=True, type=str, help='Path to the telemetry catalog.')
    argget.add_argument('-u', '--telemetry_uri_exp', required=True, type=str, help='Path to the URI expansion.')
    argget.add_argument('-p', '--platform', required=False, default="Vulcan", type=str, help='Target platform to test')
    argget.add_argument('-f', '--fdr_output_dir', required=True, type=str, help='Path to the fdr directory of a FDR dump.')
    argget.add_argument('-e', '--exempt_list', required=False, default=None, type=str, help='Target platform to test')
    
    # argget.add_argument('-o', '--output', required=False, default="./tmp", type=str, help='Path to the output directory.')
    args = argget.parse_args()

    global MyCatalog

    parse_exempt_list(args.exempt_list)

    # TODO: Skip the catalog entries that are in the exempt list.
    MyCatalog = telemetry_catalog.Catalog(
        args.telemetry_catalog,
        args.platform,
        None,
        None,
        None
    )

    telemetry_catalog.ReadInURIExpansionLogic(args.telemetry_uri_exp, args.platform)
    MyCatalog.Sanitize(exempt_dict["ParamClass"], exempt_dict["TGUID"])
    MyCatalog.Expand()

    fdr_dump_ids = parse_fdr_dump(args.fdr_output_dir)

    # Store the results for each boot count
    results = {}

    for boot_count, ids in fdr_dump_ids.items():
        print("[INFO] Checking Boot ID:", boot_count)
        count_found = 0
        count_missi = 0
        
        results[boot_count] = {}

        for tc in MyCatalog.CatalogEntries:
            id = tc.TGUID.split('[')[0]
            id += f"[{tc.COMPID}]"
            id += f"[{tc.PARAMIDX}]" if tc.PARAMIDX != None and tc.PARAMIDX != "" else "[0]" 
            # print("[LOG] ID", id)
            if id in ids:
                # print("[Found]", tc.TGUID)
                count_found += 1
            else:
                print("[Not found]", id)
                count_missi += 1
        
        results[boot_count]["count_found"] = count_found
        results[boot_count]["count_missi"] = count_missi
    
    for boot_count, res in results.items():
        print("Result for boot count:", boot_count)
        print("Found:", res["count_found"])
        print("Missing:", res["count_missi"])
        print("Coverage %:", res["count_found"] / (res["count_found"] + res["count_missi"]) * 100)
        
        
    # for boot_count, ids in fdr_dump_ids.items():
    #     print("[LOG] Boot count:", boot_count)
    #     for id in ids:
    #         if "PCIERETIMERTOPO" in id: print("[INFO]", id)