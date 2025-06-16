import configargparse
import pprint
import json
import csv
import sys
import os
import re
import logging
import yaml
from tqdm import tqdm

csv.field_size_limit(sys.maxsize)
pp = pprint.PrettyPrinter(indent=2, width=30, compact=True)

def comparator(redfish_val, fdrlog_val):
    try:
        if redfish_val.lower() == fdrlog_val.lower():
            return "Match"

        if redfish_val.lower() == "true": redfish_val = 1
        elif redfish_val.lower() == "false": redfish_val = 0

        if fdrlog_val.lower() == "true": fdrlog_val = 1
        elif fdrlog_val.lower() == "false": fdrlog_val = 0

        if "xyz.openbmc_project" in fdrlog_val:
            if redfish_val == fdrlog_val.split(".")[-1]:
                return "Match"
    except: pass

    try:
        redfish_val = float(redfish_val)
        fdrlog_val = float(fdrlog_val)
        if fdrlog_val == 0:
           if redfish_val == 0:
               return "Match"
           else:
               return "Mismatch"
        else:
            return "Partial-Match"
    except: pass
    return "Mismatch"


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

def get_validation_total_steps(fdr_output_dir):
    """
    Estimates the total number of steps for validation progress tracking
    """
    base_steps = 4  # For fixed operations
    # Estimate the number of telemetry IDs to process
    # This is a rough estimate and will be refined when we actually load the data
    estimated_id_count = 200
    return base_steps + estimated_id_count

def generate_value_validation_report(args=None):
    if args is None:
        argget = configargparse.ArgParser()
        argget.add_argument('-c', '--config_file', required=False, is_config_file=True, help='Config file path.')
        argget.add_argument('-t', '--telemetry_agent_output', required=True, type=str, help='Path to the csv file generated by Telemetry Agent.')
        argget.add_argument('-f', '--fdr_output', required=True, type=str, help='Path to the fdr directory of a FDR dump.')
        argget.add_argument('-e', '--exempt_list', required=False, default=None, type=str, help='Path to exempt list yaml file')
        args = argget.parse_args()
    print(f"\n\u00BB Generating Value Validation report")
    # Create overall progress bar
    total_steps = get_validation_total_steps(args.fdr_output)
    progress_bar = tqdm(total=total_steps, desc="Generating validation report", ncols=100)

    # Step 1: Setup logging
    progress_bar.set_description("Setting up logging...")
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
    progress_bar.update(1)

    # Step 2: Parse telemetry agent output
    progress_bar.set_description("Parsing telemetry agent output...")
    ta_result_dict = parse_telemetry_agent_output(args.telemetry_agent_output)
    progress_bar.update(1)

    # Step 3: Parse FDR dump
    progress_bar.set_description("Parsing FDR dump...")
    fdr_logs_dict = parse_fdr_dump(args.fdr_output, debug_log=debug_log)
    progress_bar.update(1)

    # Initialize counters
    missing_count = {}
    found_match_count = {}
    found_partial_match_count = {}
    found_mismatch_count = {}
    exempted_items_count = {}
    loss_in_ta_count = {}

    # Load exempt list
    progress_bar.set_description("Loading exempt list...")
    exempt_list = []
    if args.exempt_list:
        with open(args.exempt_list) as stream:
            try:
                exempt_list = yaml.safe_load(stream)['TGUID']
            except yaml.YAMLError as exc:
                debug_log.error(f"Exception in exempt list: {exc}")

    # Update total steps now that we know the actual ID count
    actual_id_count = len(ta_result_dict)
    progress_bar.total = 4 + actual_id_count
    progress_bar.refresh()

    # Step 4: Process telemetry IDs
    debug_log.info("Checking the IDs from TelemetryAgent in FDR logs")

    # Process each telemetry ID with progress tracking
    for i, (t_id, t_val) in enumerate(ta_result_dict.items()):
        progress_bar.set_description(f"Validating ID {i+1}/{actual_id_count}")

        for boot_count, fdr_boot_data in fdr_logs_dict.items():
            if boot_count not in missing_count: missing_count[boot_count] = 0
            if boot_count not in found_match_count: found_match_count[boot_count] = 0
            if boot_count not in found_mismatch_count: found_mismatch_count[boot_count] = 0
            if boot_count not in found_partial_match_count: found_partial_match_count[boot_count] = 0
            if boot_count not in exempted_items_count: exempted_items_count[boot_count] = 0
            if boot_count not in loss_in_ta_count: loss_in_ta_count[boot_count] = 0
            tguid = t_id.split('[')[0]
            if tguid in exempt_list:
                debug_log.info(f"[Bootcount: {boot_count}] Exempting {t_id} Telemetry_Agent_value: {t_val}")
                exempted_items_count[boot_count] += 1
            else:
                if t_val == '':
                    try:
                        debug_log.info(f"[Bootcount: {boot_count}] Skipping {t_id} Telemetry_Agent_value: {t_val} FDR_value: {fdr_boot_data[t_id]}")
                    except Exception as e:
                        debug_log.info(f"[Bootcount: {boot_count}] Skipping {t_id}, Missing ID from FDR logs also")
                    loss_in_ta_count[boot_count] += 1
                else:
                    try:
                        status = comparator(t_val, fdr_boot_data[t_id])
                        if status  == "Match":
                            found_match_count[boot_count] += 1
                        elif status == "Partial-Match":
                            found_partial_match_count[boot_count] += 1
                        else:
                            found_mismatch_count[boot_count] += 1
                        debug_log.info(f"[Bootcount: {boot_count}] {status} at: {t_id} Telemetry_Agent_value: {t_val} FDR_value: {fdr_boot_data[t_id]}")
                    except Exception as e:
                        debug_log.error(f"[Bootcount: {boot_count}] Missing ID from FDR logs: {e} Telemetry_Agent_value: {t_val}")
                        missing_count[boot_count] += 1

        # Update progress after processing each ID
        progress_bar.update(1)

    # Step 5: Generate report
    progress_bar.set_description("Generating validation report")
    debug_log.info("-------Generating Report-------")
    for boot_count in fdr_logs_dict.keys():
        report_log.info(f"BOOTCOUNT: {boot_count}")
        try:
            remaining_ta_count = len(ta_result_dict.keys()) - exempted_items_count[boot_count] - loss_in_ta_count[boot_count]
            coverage_percentage = ((found_match_count[boot_count] + found_mismatch_count[boot_count] + found_partial_match_count[boot_count]) / remaining_ta_count) * 100
            details = f"Total Telemetry TGUIDs: {len(ta_result_dict.keys())} | Total Considered TGUIDs: {remaining_ta_count} | Exact Match Count: {found_match_count[boot_count]} | Non-Zero / Partial Match Count: {found_partial_match_count[boot_count]} | Mismatch Count: {found_mismatch_count[boot_count]} | Missing in FDR count: {missing_count[boot_count]} | Exempted Items Count: {exempted_items_count[boot_count]} | Missing in Telemetry Agent Count: {loss_in_ta_count[boot_count]}"
            report_log.info(details)
            report_log.info(f"Match percentage: {found_match_count[boot_count] / remaining_ta_count * 100}")
            report_log.info(f"Coverage percentage: {coverage_percentage}")
        except Exception as e:
            report_log.info(f"Error in calculating Validation Coverage: {e}")


    progress_bar.update(1)

    progress_bar.set_description("Report Generated")
    progress_bar.close()
    print(f"\u00BB Find the validation report at: {log_directory}")

if __name__ == "__main__":
    generate_value_validation_report()
        
