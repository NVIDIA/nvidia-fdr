import configargparse
import pprint
import json
import csv
import sys
import os
import re
import logging

csv.field_size_limit(sys.maxsize)
pp = pprint.PrettyPrinter(indent=2, width=30, compact=True)

def comparator(redfish_val, fdrlog_val):
    try:
        if redfish_val.lower() == "true": redfish_val = 1
        elif redfish_val.lower() == "false": redfish_val = 0
        if fdrlog_val.lower() == "true": redfish_val = 1
        elif fdrlog_val.lower() == "false": redfish_val = 0
        if "xyz.openbmc_project" in fdrlog_val:
            if redfish_val == fdrlog_val.split(".")[-1]:
                return True
    except: pass
    try:
        redfish_val = float(redfish_val)
        fdrlog_val = float(fdrlog_val)
        return abs(redfish_val - fdrlog_val) <= 0.10 * redfish_val
    except: pass
    try: 
        return str(redfish_val) == str(fdrlog_val) 
    except: pass
    return False


def parse_telemetry_agent_output(telemetry_agent_output_f):
    ta_result_dict = dict()
    with open(telemetry_agent_output_f, mode ='r') as file:
        csvFile = csv.DictReader(file)
        for row in csvFile:
            PARAMNAME = row["TGUID"].split("[")[0]
            indices = re.findall(r'\[(\d+)\]', row["TGUID"])
            match len(indices):
                case 3:
                    COMPID = indices[1]
                    PARAMIDX = indices[2]
                case 2:
                    COMPID = indices[1]
                    PARAMIDX = "0"
                    if indices[0] != "0":
                       COMPID = indices[0]
                       PARAMIDX = indices[1]
                case 1:
                    COMPID = indices[0]
                    PARAMIDX = "0"
                case _:
                    COMPID = "0"
                    PARAMIDX = "0"
            id = f"{PARAMNAME}[{PARAMIDX}][{COMPID}]"
            ta_result_dict[id] = row["VALUE"]   
    return ta_result_dict
    

def parse_fdr_dump(fdr_logs_dir, debug_log):
    # Parse the fdr dump
    fdr_logs_dict = {}
    for (root,dirs,files) in os.walk(fdr_logs_dir, topdown=True):
        root_name = os.path.basename(root)
        if("BootCount" in root_name):
            # print("[INFO]", "Processing BootCount directory", root_name)
            _, boot_count, _, timestamp = root_name.split("_")
            if not boot_count in fdr_logs_dict:
                fdr_logs_dict[boot_count] = {}
            for file in files:
                comp_class, comp_id, filetype = file.split(".", 2)
                # Skip the following files 
                if any(needle in file for needle in ["hifi.dat", ".Event", ".stat"]): continue
            
                # print("[INFO]", "Filename", os.path.join(root_name, file))
                comp_class = comp_class.upper()
                with open(os.path.join(root, file), "r") as f:
                    for line in f:
                        fdr_json_log = json.loads(line)
                        try:
                            param_id = fdr_json_log["ParamID"]
                            # FDR dump should be decoded with -kn option.
                            param_name = fdr_json_log["ParamName"]
                            # Sometimes there is no ParamValue in the FDR dump logs.
                            param_value = "PARAM_VALUE_NOT_FOUND"
                        except Exception as e:
                            if "ParamID" not in fdr_json_log:
                                debug_log.error(f"[ERROR] ParamID missing in the file {os.path.join(root_name, file)}")
                            if "ParamName" not in fdr_json_log:
                                debug_log.error(f"[ERROR] ParamName missing for ID: {param_id} in the file {os.path.join(root_name, file)}")
                            continue
                        # Param value can be in one of the following keys:
                        # ParamValueInt64, ParamValueDouble, ParamValueString 
                        for key, value in fdr_json_log.items():
                            if "ParamValue" in key:
                                param_value = value
                                break
                        try:
                            # Formatting the ID as: [param_idx][comp_id] 
                            # There may be some parameters with same name and different param_idx.
                            # E.g.: SPEED-MAX[0], SPEED-MAX[1], ..., SPEED-MAX[31]
                            if '[' not in param_name: param_name += '[0]'
                            id = f"{comp_class}-{param_name}[{comp_id.split('_')[-1]}]"
                            fdr_logs_dict[boot_count][id] = param_value

                        except Exception as e:
                            debug_log.error(f"[Exception]{e}")
    return fdr_logs_dict

def generate_value_validation_report(args=None):
    if args is None:
        argget = configargparse.ArgParser()
        argget.add_argument('-c', '--config_file', required=False, is_config_file=True, help='Config file path.')
        argget.add_argument('-t', '--telemetry_agent_output', required=True, type=str, help='Path to the csv file generated by Telemetry Agent.')
        argget.add_argument('-f', '--fdr_output', required=True, type=str, help='Path to the fdr directory of a FDR dump.')
        args = argget.parse_args()

    log_directory = f"{args.fdr_output}/value_validation_report"
    os.makedirs(log_directory, exist_ok=True)

    debug_log_file_path = os.path.join(log_directory, 'validation_debug.log')
    debug_log = logging.getLogger('validation_debug_log')
    debug_log.setLevel(logging.DEBUG)
    debug_log.propagate = False
    debug_handler = logging.FileHandler(debug_log_file_path)
    debug_handler.setFormatter(logging.Formatter('%(levelname)s - %(message)s'))
    if debug_log.hasHandlers():
        debug_log.handlers.clear()
    debug_log.addHandler(debug_handler)

    report_log_file_path = os.path.join(log_directory, 'validation_report.log')
    report_log = logging.getLogger('validation_report')
    report_log.setLevel(logging.DEBUG)
    report_log.propagate = False
    report_handler = logging.FileHandler(report_log_file_path)
    report_handler.setFormatter(logging.Formatter('%(message)s'))
    report_log.addHandler(report_handler)

    ta_result_dict = parse_telemetry_agent_output(args.telemetry_agent_output)
    fdr_logs_dict = parse_fdr_dump(args.fdr_output, debug_log=debug_log)
    # present = {boot_count: [] for boot_count in fdr_logs_dict.keys()}
    # absent = {boot_count: [] for boot_count in fdr_logs_dict.keys()}
    missing_count = {}
    found_match_count = {}
    found_mismatch_count = {}

    debug_log.info("Checking the IDs from TelemetryAgent in FDR logs")
    for t_id, t_val in ta_result_dict.items():
        for boot_count, fdr_boot_data in fdr_logs_dict.items():
            if boot_count not in missing_count: missing_count[boot_count] = 0
            if boot_count not in found_match_count: found_match_count[boot_count] = 0
            if boot_count not in found_mismatch_count: found_mismatch_count[boot_count] = 0
            
            try:
                # print("[INFO]", t_id, t_val, fdr_boot_data[t_id])
                if comparator(t_val, fdr_boot_data[t_id]):
                    status = "Match"
                    found_match_count[boot_count] += 1
                else:
                    status = "Mismatch"
                    found_mismatch_count[boot_count] += 1
                debug_log.info(f"[Bootcount: {boot_count}] {status} at: {t_id} Telemetry_Agent_value: {t_val} FDR_value: {fdr_boot_data[t_id]}")
            except Exception as e:
                debug_log.error(f"[Bootcount: {boot_count}] Missing ID from FDR logs: {e} Telemetry_Agent_value: {t_val}")
                missing_count[boot_count] += 1
                # absent[boot_count].append(t_id.split("[")[0])
    

    debug_log.info("-------Generating Report-------")
    for boot_count in fdr_logs_dict.keys():
        report_log.info(f"BOOTCOUNT: {boot_count}")
        report_log.info(f"Total TGUIDs: {len(ta_result_dict.keys())}")
        report_log.info(f"Missing count: {missing_count[boot_count]}")
        report_log.info(f"Found and matched count: {found_match_count[boot_count]}")
        report_log.info(f"Found and mismatched count: {found_mismatch_count[boot_count]}")
        report_log.info(f"Match percentage: {found_match_count[boot_count] / len(ta_result_dict.keys()) * 100}")
        report_log.info(f"Coverage percentage: {(found_match_count[boot_count] + found_mismatch_count[boot_count]) / len(ta_result_dict.keys()) * 100}")
        # print("BOOTCOUNT:", boot_count)
        # print("Total TGUIDs:", len(ta_result_dict.keys()))
        # print("Missing count:", missing_count[boot_count])
        # print("Found and matched count:", found_match_count[boot_count])
        # print("Found and mismatched count:", found_mismatch_count[boot_count])
        # print("Match percentage:", found_match_count[boot_count] / len(ta_result_dict.keys()) * 100)
        # print("Coverage percentage:", (found_match_count[boot_count] + found_mismatch_count[boot_count]) / len(ta_result_dict.keys()) * 100)
        # print("\n\n")
        print(f"=> Validation Report is generated at: {log_directory}")

if __name__ == "__main__":
    generate_value_validation_report()
        
