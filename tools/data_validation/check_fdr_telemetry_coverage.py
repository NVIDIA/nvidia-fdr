import telemetry_catalog
import configargparse
import pprint
import json
import yaml
import csv
import sys
import os
import re
import logging

def parse_fdr_dump(fdr_logs_dir, log):
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
                if ".Event" in file: continue
            
                comp_class, comp_id, filetype = file.split(".", 2)
                # print("[INFO]", "Filename", os.path.join(root_name, file))
                log.info(f"Parsing Filename: {os.path.join(root_name, file)}")
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
                                # print("[ERROR] ParamID missing in the file", os.path.join(root_name, file))
                                log.error(f"ParamID missing in the file: {os.path.join(root_name, file)}")
                            if "ParamName" not in fdr_json_log:
                                # print("[ERROR] ParamName missing for ID", param_id, "in the file", os.path.join(root_name, file))
                                log.error(f"ParamName missing for ID {param_id} in the file {os.path.join(root_name, file)}")
                            continue
                        try:
                            # Formatting the ID as: comp_class-param_name[0][comp_id][param_idx]
                            suffix = re.findall(r'\d+', comp_id)
                            indices = re.findall(r'\[(\d+)\]', param_name)
                            if len(suffix) > 1:
                                suffix = ''.join(f'[{indices}]' for indices in suffix)
                            else:
                                suffix = f"[{comp_id.split('_')[-1]}]"
                                if len(indices) == 1:
                                    suffix += f"[{indices[0]}]"
                                else:
                                    suffix += "[0]"

                            id = f"{comp_class}-{param_name.split('[')[0]}{suffix}"
                            #print(f"[FDR ID]: {id}")
                            fdr_logs_ids[boot_count].add(id)

                        except Exception as e:
                            print("[Exception]", e)
                            # log.error(f"Exception: {e}")
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

def update_device_list(devices_dict, device_name, increment_true=0, increment_false=0):
    if device_name in devices_dict:
        # Device found, update the counts
        devices_dict[device_name]["found"] += increment_true
        devices_dict[device_name]["missing"] += increment_false
    else:
        # Device not found, add it with default values
        devices_dict[device_name] = {"found": increment_true, "missing": increment_false}

def generate_summary_report(device_wise_data, total_summary, html_path):
    total_coverage_per_boot = 0
    total_boots = 0
    boot_wise_results = (
        "<table>"
        "<thead>"
        "<tr><th>Boot ID</th><th>Found</th><th>Missing</th><th>Coverage</th>"
        "</thead><tbody>"
    )
    device_wise_results = (
        "<table>"
        "<thead>"
        "<tr><th>Device</th><th>Found</th><th>Missing</th><th>Coverage</th>"
        "</thead><tbody>"
    )

    for boot_count, res in total_summary.items():
        coverage_percentage = res["count_found"] / (res["count_found"] + res["count_missi"]) * 100
        total_coverage_per_boot +=coverage_percentage
        total_boots += 1
        boot_wise_results += f"<tr><td>{boot_count}</td><td>{res['count_found']}</td><td>{res['count_missi']}</td><td>{coverage_percentage}</td></tr>"

    boot_wise_results+= "</tbody></table>"
    
    for device, res in device_wise_data.items():
        coverage_percentage = res["found"] / (res["found"] + res["missing"]) * 100
        device_wise_results += f"<tr><td>{device}</td><td>{res['found']}</td><td>{res['missing']}</td><td>{coverage_percentage}</td></tr>"

    avg_coverage = total_coverage_per_boot / total_boots
    device_wise_results+= "</tbody></table>"
    html= "<!DOCTYPE html><head><title>FDR Coverage Report</title></head><body style='width:100%; height:100%;text-align:center'>"
    html+= "<h1>FDR Coverage Report</h1><br/>"
    html+= f"<p> Average Coverage Per Boot: {avg_coverage}% </p>"
    html+= f"<br/><br/>{boot_wise_results}<br/>"
    html+= f"<br/><br/>{device_wise_results}<br/>"
    html+= f"</body></html>"

    with open(html_path, "+w") as report:
        report.write(html)
    


def generate_coverage_report(args=None):
    if args is None:
        argget = configargparse.ArgParser()
        argget.add_argument('-c', '--config_file', required=False, is_config_file=True, help='Config file path.')
        argget.add_argument('-t', '--telemetry_catalog', required=True, type=str, help='Path to the telemetry catalog.')
        argget.add_argument('-u', '--telemetry_uri_exp', required=True, type=str, help='Path to the URI expansion.')
        argget.add_argument('-p', '--platform', required=False, default="Vulcan", type=str, help='Target platform to test')
        argget.add_argument('-f', '--fdr_output_dir', required=True, type=str, help='Path to the fdr directory of a FDR dump.')
        argget.add_argument('-e', '--exempt_list', required=False, default=None, type=str, help='Target platform to test')

        # argget.add_argument('-o', '--output', required=False, default="./tmp", type=str, help='Path to the output directory.')
        args = argget.parse_args()

    log_directory = f"{args.fdr_output_dir}/coverage_report"
    os.makedirs(log_directory, exist_ok=True)

    debug_log_file_path = os.path.join(log_directory, 'debug.log')
    debug_log = logging.getLogger('debug_log')
    debug_log.setLevel(logging.DEBUG)
    debug_log.propagate = False
    debug_handler = logging.FileHandler(debug_log_file_path)
    debug_handler.setFormatter(logging.Formatter('%(levelname)s - %(message)s'))
    if debug_log.hasHandlers():
        debug_log.handlers.clear()
    debug_log.addHandler(debug_handler)

    report_log_file_path = os.path.join(log_directory, 'report.log')
    report_log = logging.getLogger('report_log')
    report_log.setLevel(logging.DEBUG)
    report_log.propagate = False
    report_handler = logging.FileHandler(report_log_file_path)
    report_handler.setFormatter(logging.Formatter('%(message)s'))
    report_log.addHandler(report_handler)

    report_html_path = os.path.join(log_directory, 'fdr_coverage_report.html')

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

    fdr_dump_ids = parse_fdr_dump(args.fdr_output_dir, debug_log)

    # Store the results for each boot count
    results = {}

    for boot_count, ids in fdr_dump_ids.items():
        debug_log.info(f"Checking Boot ID: {boot_count}")
        count_found = 0
        count_missi = 0
        
        results[boot_count] = {}
        devicewise_result = {}
        categorised_report = {}
        default_data = {"missing": 0, "found": 0}
        for tc in MyCatalog.CatalogEntries:
            id = tc.TGUID.split('[')[0]
            id += f"[{tc.COMPID}]"
            id += f"[{tc.PARAMIDX}]" if tc.PARAMIDX != None and tc.PARAMIDX != "" else "[0]" 
            # print("[LOG] ID", id)
            indices = re.findall(r'\[(\d+)\]',id)
            if len(indices) == 1:
                id = f"{id}[0]"
            if id in ids:
                # print("[Found]", tc.TGUID)
                # debug_log.info(f"[Found]: {id}")
                count_found += 1
                update_device_list(devices_dict=devicewise_result, device_name=tc.COMPCLASS, increment_true=1)
            else:
                debug_log.info(f"[Not found]: {id}")
                #print("[Not found]", id)
                count_missi += 1
                update_device_list(devices_dict=devicewise_result, device_name=tc.COMPCLASS, increment_false=1)
        
        results[boot_count]["count_found"] = count_found
        results[boot_count]["count_missi"] = count_missi

    for boot_count, res in results.items():
        coverage_percentage = res["count_found"] / (res["count_found"] + res["count_missi"]) * 100
        report_log.info(f"Result for boot count: {boot_count}")
        report_log.info(f"Found: {res['count_found']}")
        report_log.info(f"Missing: {res['count_missi']}")
        report_log.info(f"Coverage %: {coverage_percentage}")
        # print("Result for boot count:", boot_count)
        # print("Found:", res["count_found"])
        # print("Missing:", res["count_missi"])
        # print("Coverage %:", coverage_percentage)

    report_log.info(f"Device wise Result: {devicewise_result}")
    report_log.info(f"Boot Count wise Report: {results}")
    generate_summary_report(devicewise_result, results, report_html_path)
    print(f"=> Coverage Report is generated at: {log_directory}")


if __name__ == "__main__":
    generate_coverage_report()
