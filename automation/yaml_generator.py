#!/usr/bin/env python3
import os
import re
import sys
import yaml
import datetime
import csv
import glob
import json
import argparse
from collections import defaultdict
import yaml as pyyaml

def load_input_yaml(input_yaml_path):
    """Load the input YAML file containing platform definition and constants."""
    try:
        with open(input_yaml_path, 'r') as yaml_file:
            return yaml.safe_load(yaml_file)
    except Exception as e:
        print(f"Error loading input YAML file: {e}")
        sys.exit(1)

def find_telemetry_csv(telemetry_file=None):
    """Find the telemetry CSV file, using the provided path or searching for Telemetry*.csv."""
    if telemetry_file:
        if os.path.exists(telemetry_file):
            return telemetry_file
        print(f"Warning: Specified telemetry file {telemetry_file} not found")
    csv_files = glob.glob("Telemetry*.csv")
    if not csv_files:
        return None
    return csv_files[0]

def extract_platform_id_from_csv(csv_path):
    """Extract the platform identifier from the CSV file."""
    with open(csv_path, 'r', newline='') as csvfile:
        reader = csv.reader(csvfile)
        header = next(reader)
        applicable_cols = header[18:26]
        platform_idx = -1
        applicable_idx = -1
        for i, col in enumerate(applicable_cols):
            if "Applicable" in col and "for" in col.lower():
                applicable_idx = i + 18
                print(f"Found 'Applicable for' column: Column {applicable_idx} ('{col}')")
                break
        for i, col in enumerate(applicable_cols):
            if "Platform" in col or "platform" in col.lower():
                platform_idx = i + 18
                print(f"Found platform name column: Column {platform_idx} ('{col}')")
                break
        if platform_idx == -1:
            platform_idx = applicable_idx
        if applicable_idx == -1:
            print("Warning: Could not find a column with 'Applicable for' text.")
            return None
        for row_idx, row in enumerate(reader):
            if len(row) <= applicable_idx:
                continue
            applicable_value = row[applicable_idx].strip().lower() if applicable_idx < len(row) else ""
            print(f"Row {row_idx+2}, Column {applicable_idx} ('{header[applicable_idx]}'): '{row[applicable_idx]}'")
            if applicable_value == "yes":
                if platform_idx >= 0 and platform_idx < len(row):
                    platform_info = row[platform_idx].strip()
                    print(f"Platform information from Column {platform_idx}: '{platform_info}'")
                    if not platform_info:
                        for i, col_name in enumerate(header):
                            if "Platform" in col_name and i < len(row) and row[i]:
                                platform_info = row[i].strip()
                                print(f"Found platform info in Column {i} ('{col_name}'): '{platform_info}'")
                                break
                    if platform_info:
                        platform_id = platform_info.lower().replace(" ", "")
                        platform_id = platform_id.replace("nvl", "nvl-hmc")
                        print(f"Extracted platform identifier: '{platform_id}'")
                        return platform_id
                    else:
                        print("Warning: Found 'yes' but no platform information in the same row.")
                else:
                    print(f"Warning: Platform column index {platform_idx} is out of range.")
                if platform_idx >= 0 and platform_idx < len(header):
                    platform_id = header[platform_idx].lower().replace(" ", "").replace("applicablefor", "")
                    if platform_id:
                        print(f"Using column name as platform identifier: '{platform_id}'")
                        return platform_id
            if row_idx > 10:
                break
    filename = os.path.basename(csv_path)
    if filename.startswith("Telemetry"):
        default_id = filename[9:].split('.')[0].lower()
        if default_id:
            print(f"Using CSV filename for platform identifier: '{default_id}'")
            return default_id
    print("Warning: Could not extract platform identifier from CSV.")
    return None

def extract_platform_id_from_fingerprint(platform_fingerprint):
    """Extract the platform identifier from the fingerprint check."""
    for check in platform_fingerprint:
        if "grep -q -i" in check and "/etc/os-release" in check:
            parts = check.split()
            if len(parts) >= 3:
                platform_id = parts[2]
                print(f"Extracted platform ID from fingerprint: '{platform_id}'")
                return platform_id
    print("Warning: Could not extract platform ID from fingerprint checks.")
    return None

def find_platform_column(header, platform_id):
    """Find the column in the CSV that corresponds to the platform ID."""
    if not platform_id:
        print("Warning: No platform ID provided to find_platform_column")
        return -1
    formatted_id = platform_id.upper().replace('-', ' ')
    if 'NVLHMC' in formatted_id:
        formatted_id = formatted_id.replace('NVLHMC', 'NVL HMC')
    elif 'NVL-HMC' in formatted_id:
        formatted_id = formatted_id.replace('NVL-HMC', 'NVL HMC')
    parts = []
    if 'GB300' in formatted_id or 'GB300NVL' in formatted_id or 'NVL-HMC' in formatted_id or 'gb300nvl-hmc' in platform_id.lower():
        formatted_id = "GB300 NVL HMC"
        parts = ["GB300", "NVL", "HMC"]
        print(f"Using exact format for GB300 NVL HMC platform")
    else:
        current = ""
        for char in formatted_id:
            if (char.isdigit() and current.isalpha()) or (char.isalpha() and current.isdigit()):
                if current:
                    parts.append(current)
                current = char
            else:
                current += char
        if current:
            parts.append(current)
    print(f"Looking for platform column containing: '{formatted_id}'")
    print(f"Platform parts: {parts}")

    # Search through columns S-Z (indices 18-25) for platform-specific columns
    # This range typically contains columns like "Applicable for Platform X" in the telemetry CSV
    for i in range(18, min(26, len(header))):
        col_name = header[i]
        if "Applicable for" in col_name and formatted_id in col_name:
            print(f"Found exact 'Applicable for {formatted_id}' match in S-Z range: Column {i} ('{col_name}')")
            return i
    applicable_columns = []
    for i in range(18, min(26, len(header))):
        col_name = header[i]
        if "Applicable for" in col_name:
            applicable_columns.append((i, col_name))
            matching_parts = 0
            for part in parts:
                if part in col_name:
                    matching_parts += 1
            if matching_parts > 0:
                print(f"Found 'Applicable for' column with {matching_parts}/{len(parts)} matching parts: Column {i} ('{col_name}')")
                if matching_parts >= len(parts) / 2:
                    return i
    if formatted_id == "GB300 NVL HMC" and len(header) > 24 and "Applicable for" in header[24]:
        print(f"Using column 24 for GB300 NVL HMC: '{header[24]}'")
        return 24
    if applicable_columns:
        for i, col_name in applicable_columns:
            for part in parts:
                if part in col_name:
                    print(f"Found 'Applicable for' column containing '{part}': Column {i} ('{col_name}')")
                    return i
        i, col_name = applicable_columns[0]
        print(f"Using first 'Applicable for' column as fallback: Column {i} ('{col_name}')")
        return i
    for i in range(18, min(26, len(header))):
        col_name = header[i]
        if formatted_id in col_name:
            print(f"Found column with platform name in S-Z range: Column {i} ('{col_name}')")
            return i
    print(f"Warning: Could not find column for platform '{formatted_id}'")
    return -1

def process_telemetry_csv(csv_path, platform_data, platform_id=None, override_column_idx=None):
    # Dictionary to hold parameters for all CompClasses
    comp_classes = {}
    
    with open(csv_path, 'r', newline='') as csvfile:
        reader = csv.reader(csvfile)
        # Get header row
        header = next(reader)
        
        # Print all header names to help with debugging
        for i, col_name in enumerate(header):
            print(f"Column {i}: '{col_name}'")
        
        # Find column indices
        col_b_idx = 1  # Device (CompClass)
        col_c_idx = 2  # Metric (ParamName)
        col_e_idx = 4  # Category (ParamClass)
        
        # Use hardcoded dbus_column_index from platform_data if present
        col_s_idx = platform_data.get('dbus_column_index', None)
        if col_s_idx is not None and col_s_idx < len(header):
            print(f"Using hardcoded dbus_column_index: Column {col_s_idx} ('{header[col_s_idx]}')")
        else:
            col_s_idx = -1
            for i, col_name in enumerate(header):
                if "D-Bus" in col_name or "DBus" in col_name or "Command" in col_name:
                    col_s_idx = i
                    print(f"Found DBus info column: Column {i} ('{col_name}')")
                    break
            if col_s_idx == -1:
                # Fallback to column S if we couldn't find a column with "D-Bus" in the name
                col_s_idx = 18
                print(f"Using default column S (index 18) for DBus info: '{header[col_s_idx]}'")
        
        # Use override column if provided
        platform_col_idx = -1
        if override_column_idx is not None and override_column_idx < len(header):
            platform_col_idx = override_column_idx
            print(f"Using override platform column: Column {platform_col_idx} ('{header[platform_col_idx]}')")
        # Otherwise find the platform-specific column if we have a platform_id
        elif platform_id:
            platform_col_idx = find_platform_column(header, platform_id)
        
        # Fallback to any "Applicable" column if platform column not found
        if platform_col_idx == -1:
            for i in range(18, min(26, len(header))):
                if "Applicable" in header[i]:
                    platform_col_idx = i
                    print(f"Using fallback 'Applicable' column: Column {platform_col_idx} ('{header[platform_col_idx]}')")
                    break
        
        if platform_col_idx == -1:
            print("Warning: Could not find a column for this platform or with 'Applicable for' text.")
            return comp_classes
        
        print(f"Using platform column for 'Yes' values: Column {platform_col_idx} ('{header[platform_col_idx]}')")
        
        # Process rows
        param_id_counter = 0
        applicable_rows = 0
        dbus_found_count = 0
        for row_idx, row in enumerate(reader):
            if len(row) <= max(platform_col_idx, col_s_idx):
                continue
                
            # Check if this row is applicable to our platform
            applicable_value = row[platform_col_idx].strip().lower() if platform_col_idx < len(row) else ""
            
            # Add debug prints occasionally to show what values we're seeing
            if row_idx < 5 or row_idx % 100 == 0:
                print(f"Row {row_idx+2}, Column {platform_col_idx} ('{header[platform_col_idx]}'): '{row[platform_col_idx]}'")
            
            if applicable_value == "yes":
                applicable_rows += 1
                device = row[col_b_idx].strip() if col_b_idx < len(row) else ""
                metric = row[col_c_idx].strip() if col_c_idx < len(row) else ""
                category = row[col_e_idx].strip() if col_e_idx < len(row) else ""
                
                # Extract DBus parameters from column S
                try:
                    dbus_info = row[col_s_idx].strip() if col_s_idx < len(row) else ""
                    
                    # Parse DBus parameters from the string
                    service = ""
                    object_path = ""
                    interface = ""
                    property_name = ""
                    
                    if dbus_info:
                        # Always print the first 10 DBus info strings to help with debugging
                        if row_idx < 10:
                            print(f"Row {row_idx+2}, DBus info from Column {col_s_idx}: '{dbus_info}'")
                        
                        # Parse the DBus info string - handle multiple formats
                        if "busctl" in dbus_info and "get-property" in dbus_info:
                            # Format: "busctl get-property service objectpath interface property"
                            parts = dbus_info.split()
                            try:
                                if len(parts) >= 6:
                                    # Find indices after "get-property"
                                    get_prop_idx = parts.index("get-property")
                                    service = parts[get_prop_idx + 1]
                                    object_path = parts[get_prop_idx + 2]
                                    interface = parts[get_prop_idx + 3]
                                    property_name = parts[get_prop_idx + 4]
                                    dbus_found_count += 1
                            except (ValueError, IndexError) as e:
                                print(f"Error parsing busctl command in row {row_idx+2}: {e}")
                        elif "xyz.openbmc_project" in dbus_info:
                            # Try to extract D-Bus parameters from text containing service/path/interface/property
                            try:
                                # Look for service
                                for part in dbus_info.split():
                                    if part.startswith("xyz.openbmc_project"):
                                        service = part
                                        break
                                
                                # Look for object path
                                path_match = re.search(r'(/xyz/openbmc_project/\S+)', dbus_info)
                                if path_match:
                                    object_path = path_match.group(1)
                                
                                # Look for interface
                                interface_match = re.search(r'(xyz\.openbmc_project\.[A-Za-z0-9_.]+)', dbus_info)
                                if interface_match:
                                    interface = interface_match.group(1)
                                
                                # Look for property (might be the last word, or after "Property:")
                                if "Property:" in dbus_info:
                                    prop_parts = dbus_info.split("Property:")
                                    if len(prop_parts) > 1:
                                        property_name = prop_parts[1].strip().split()[0]
                                else:
                                    # Try using the metric name as property if nothing else found
                                    property_name = metric
                                
                                if service or object_path or interface or property_name:
                                    dbus_found_count += 1
                            except Exception as e:
                                print(f"Error extracting D-Bus info from row {row_idx+2}: {e}")
                        
                        # Print extracted D-Bus parameters if any were found
                        if service or object_path or interface or property_name:
                            print(f"Row {row_idx+2} - Extracted DBus Params - Service: '{service}', ObjectPath: '{object_path}', Interface: '{interface}', Property: '{property_name}'")
                except Exception as e:
                    print(f"Error processing D-Bus info in row {row_idx+2}: {e}")
                    # Initialize empty values on error
                    service = ""
                    object_path = ""
                    interface = ""
                    property_name = ""
                
                # Skip if device field is empty
                if not device:
                    continue
                
                # Initialize CompClass if not exists
                if device not in comp_classes:
                    comp_classes[device] = {
                        "Params": [
                            {
                                "name": device.lower(),
                                "value": 1
                            }
                        ],
                        "InfoGroups": []
                    }
                    
                    # Track categories for this CompClass
                    comp_classes[device]["categories"] = {}
                
                # Create category if not exists
                if category and category not in comp_classes[device]["categories"]:
                    comp_classes[device]["categories"][category] = {
                        "ID": category,
                        "InfoList": []
                    }
                    comp_classes[device]["InfoGroups"].append(comp_classes[device]["categories"][category])
                
                # Only add parameter info if both metric and category are provided
                if metric and category:
                    # Extract DataType from column F (index 5)
                    data_type = row[5].strip() if len(row) > 5 else "string"
                    # Create parameter info with extracted DbusParams
                    param_info = {
                        "ID": metric,
                        "DataType": data_type,
                        "DbusParams": {
                            "Interface": interface,
                            "ObjectPath": object_path,
                            "Property": property_name,
                            "Service": service
                        },
                        "FetchFreqSecs": 3600,
                        "FetchMethod": "DBUS",
                        "FetchType": "Poll",
                        "ParamID": param_id_counter,
                        "StoreFreqSecs": 3600,
                        "StorePolicy": "OnChange",
                        "original_index": param_id_counter  # Store original index for reference
                    }
                    
                    # Add parameter to its info group
                    if category not in comp_classes.get(device, {}).get("categories", {}):
                        if device not in comp_classes:
                            comp_classes[device] = {
                                "Params": [
                                    {
                                        "name": device.lower(),
                                        "value": 1
                                    }
                                ],
                                "InfoGroups": [],
                                "categories": {}
                            }
                        comp_classes[device]["categories"][category] = {
                            "ID": category,
                            "InfoList": []
                        }
                        comp_classes[device]["InfoGroups"].append(comp_classes[device]["categories"][category])
                    
                    comp_classes[device]["categories"][category]["InfoList"].append(param_info)
                    param_id_counter += 1
    
    # Clean up internal tracking data
    for device in comp_classes:
        if "categories" in comp_classes[device]:
            del comp_classes[device]["categories"]
    
    print(f"Total rows with 'Yes' in Column {platform_col_idx}: {applicable_rows}")
    print(f"Total rows with D-Bus parameters successfully parsed: {dbus_found_count}")
    return comp_classes

def load_dbus_expansion(expansion_file=None, column_index=1):
    if not expansion_file:
        print("Warning: No expansion file specified")
        return {}
        
    try:
        expansions = {}
        with open(expansion_file, 'r') as csvfile:
            reader = csv.reader(csvfile)
            # Skip header row
            next(reader)
            for row in reader:
                if len(row) <= column_index:
                    print(f"Warning: Row has insufficient columns: {row}")
                    continue
                    
                var_name = row[0].strip()  # First column is always variable name
                value_range = row[column_index].strip()
                
                if '-' in value_range:
                    start, end = map(int, value_range.split('-'))
                    expansions[var_name] = list(range(start, end + 1))
                elif ',' in value_range:
                    # For comma-separated values, create a list of values
                    values = [v.strip().strip('"') for v in value_range.split(',')]
                    expansions[var_name] = values
                else:
                    try:
                        n = int(value_range)
                        expansions[var_name] = list(range(n))
                    except ValueError:
                        expansions[var_name] = [value_range.strip('"')]
                        
        return expansions
    except Exception as e:
        print(f"Error loading expansion file: {e}")
        return {}

def find_expansion_variables(data):
    variables = set()
    
    def process_item(item):
        if isinstance(item, dict):
            if 'DbusParams' in item and 'ObjectPath' in item['DbusParams']:
                path = item['DbusParams']['ObjectPath']
                # Find all patterns like [variable_name]
                square_matches = re.findall(r'\[([^\]]+)\]', path)
                variables.update(square_matches)
                # Find all patterns like {variable_name}
                curly_matches = re.findall(r'\{([^\}]+)\}', path)
                variables.update(curly_matches)
            for value in item.values():
                process_item(value)
        elif isinstance(item, list):
            for value in item:
                process_item(value)
    
    process_item(data)
    return variables

def expand_nvlink_entries(data, expansion_file=None):
    expansions = load_dbus_expansion(expansion_file)
    if not expansions:
        print("Error: No expansion data found in dbus-expansion.csv")
        return data
        
    # Find all variables that need expansion
    required_vars = find_expansion_variables(data)
    missing_vars = [var for var in required_vars if var not in expansions]
    
    if missing_vars:
        print(f"Error: Required variables not found in dbus-expansion.csv: {', '.join(missing_vars)}")
        return data

    def process_entry(entry, base_param_id):
        if 'DbusParams' in entry and 'ObjectPath' in entry['DbusParams']:
            path = entry['DbusParams']['ObjectPath']
            matches = re.findall(r'\[([^\]]+)\]', path)
            if matches:
                var_name = matches[0]  # Take first variable for now
                expanded_entries = []
                for i in expansions[var_name]:
                    new_entry = entry.copy()
                    new_entry['ID'] = f"{entry['ID']}[{i}]"
                    new_entry['DbusParams'] = entry['DbusParams'].copy()
                    # Replace both the variable in brackets and any $variable references
                    new_path = new_entry['DbusParams']['ObjectPath']
                    if '$' in i:
                        # If value contains $, use it directly
                        new_path = new_path.replace(f'[{var_name}]', i)
                    else:
                        # Otherwise use index as before
                        new_path = new_path.replace(f'[{var_name}]', str(i))
                    new_entry['DbusParams']['ObjectPath'] = new_path
                    new_entry['ParamID'] = base_param_id + i
                    expanded_entries.append(new_entry)
                return expanded_entries
        return [entry]

    def process_recursive(item, base_param_id):
        if isinstance(item, list):
            new_items = []
            for entry in item:
                if isinstance(entry, dict):
                    if 'InfoList' in entry:
                        entry['InfoList'] = process_recursive(entry['InfoList'], base_param_id)
                        new_items.append(entry)
                    else:
                        new_items.extend(process_entry(entry, base_param_id))
                else:
                    new_items.append(entry)
            return new_items
        return item

    return process_recursive(data, 0)

def write_fingerprint_section(yaml_file, platform_data):
    yaml_file.write("FingerPrint:\n")
    yaml_file.write("  Checks:\n")
    for check in platform_data['fingerprint']:
        yaml_file.write(f"  - {check}\n")
    yaml_file.write("\n")
        
def write_general_config_and_preconditions(yaml_file, general_config, preconditions):
    # Remove anchors and comments for manual writing
    logs_base_path = general_config.pop('LogsBasePath', None)
    default_compaction_sensors = general_config.pop('DefaultCompactionSensors', None)
    yaml_file.write("GeneralConfig:\n")
    # LogsFormat
    if 'LogsFormat' in general_config:
        yaml_file.write(f"  LogsFormat: {general_config['LogsFormat']}\n")
    # LogsBasePath with anchor
    if logs_base_path is not None:
        yaml_file.write(f"  LogsBasePath: &LogsBasePath\n")
        for item in logs_base_path:
            yaml_file.write(f"    - {item}\n")
    # PartitionThresoldCheckMB with comment
    if 'PartitionThresoldCheckMB' in general_config:
        yaml_file.write(f"  PartitionThresoldCheckMB: {general_config['PartitionThresoldCheckMB']} # emmc Partition threshold size check [if available space < 1.5 GB, then exit FDR]\n")
    # CompactionWindowSecs with comment
    if 'CompactionWindowSecs' in general_config:
        yaml_file.write(f"  CompactionWindowSecs: {general_config['CompactionWindowSecs']} # 1 day, every day new directory is created\n")
    # CompactionSubWindowSecs with comment
    if 'CompactionSubWindowSecs' in general_config:
        yaml_file.write(f"  CompactionSubWindowSecs: {general_config['CompactionSubWindowSecs']} # .5 hours, every 30 minutes stats are appended\n")
    # HiFiDataPreserveTimeSecs with comment
    if 'HiFiDataPreserveTimeSecs' in general_config:
        yaml_file.write(f"  HiFiDataPreserveTimeSecs: {general_config['HiFiDataPreserveTimeSecs']} # 5 minutes, <-5 minutes ---Error--- +5 minutes> so total 10 minutes worth of entires\n")
    # DefaultCompactionSensors with anchor
    if default_compaction_sensors is not None:
        yaml_file.write('  DefaultCompactionSensors: &compaction-definition-sensors\n')
        for k, v in default_compaction_sensors.items():
            yaml_file.write(f'    {k}: {v}\n')
    # Events
    if 'Events' in general_config:
        yaml_file.write(f"  Events:\n")
        for subk, subv in general_config['Events'].items():
            yaml_file.write(f"    {subk}: {subv}\n")
    yaml_file.write("\n")
    yaml.dump({"Preconditions": preconditions}, yaml_file, default_flow_style=False, sort_keys=False)
        
# Helper function to write compaction anchor for sensor blocks
def write_compaction_anchor_if_sensor(yaml_file, group_id, indent=4):
    if isinstance(group_id, str) and group_id.startswith("Sensor."):
        yaml_file.write(" " * indent + "<<: *compaction-definition-sensors\n")
        
def write_component_classes(yaml_file, comp_classes, expansions, written_anchors, shmem_namespaces):
    if not comp_classes:
        return

    for comp_class, params in comp_classes.items():
        has_content = False
        for group in params.get("InfoGroups", []):
            if group.get("InfoList", []):
                has_content = True
                break

        if not has_content:
            print(f"DEBUG: Skipping {comp_class} - no content in InfoGroups")
            continue

        print(f"\nDEBUG: Processing {comp_class}:")
        yaml_file.write("\n")
        anchor = comp_class.lower()
        # Use the correct param name for the anchor Params
        param_name = comp_class.lower() + 'id'
        if anchor not in written_anchors:
            yaml_file.write(f"{comp_class}: &{anchor}\n")
            written_anchors.add(anchor)
        else:
            yaml_file.write(f"{comp_class}:\n")

        yaml_file.write("  Params:\n")
        # Use the correct param name (e.g., gpuid, baseboardid, etc.)
        yaml_file.write(f"  - name: {param_name}\n")
        yaml_file.write(f"    value: 1\n")

        yaml_file.write("  InfoGroups:\n")
        for group_idx, group in enumerate(params.get("InfoGroups", [])):
            if not group.get("InfoList", []):
                print(f"DEBUG: Skipping empty InfoList in {comp_class} group {group['ID']}")
                continue

            print(f"DEBUG: Processing {comp_class} group {group['ID']} with {len(group['InfoList'])} items")
            if group_idx > 0:
                yaml_file.write("\n")
            yaml_file.write(f"  - ID: {group['ID']}\n")
            yaml_file.write("    InfoList:\n")
            items_written = 0

            for info_idx, info in enumerate(group.get("InfoList", [])):
                if info_idx > 0:
                    yaml_file.write("\n")

                path = info['DbusParams'].get('ObjectPath', '')
                square_matches = re.findall(r'\[([^\]]+)\]', path)
                curly_matches = re.findall(r'\{([^\}]+)\}', path)

                if square_matches or curly_matches:
                    if square_matches:
                        var_name = square_matches[0]
                        base_param_id = info.get('original_index', info['ParamID'])
                        if var_name in expansions:
                            var_range = expansions[var_name]
                            if isinstance(var_range, list) and len(var_range) > 0 and isinstance(var_range[0], str):
                                for i, value in enumerate(var_range):
                                    expanded_info = info.copy()
                                    expanded_info['ID'] = f"{info['ID']}[{i}]"
                                    expanded_info['DbusParams'] = info['DbusParams'].copy()
                                    new_path = expanded_info['DbusParams']['ObjectPath']
                                    if '$' in value:
                                        new_path = new_path.replace(f'[{var_name}]', str(value))
                                    else:
                                        new_path = new_path.replace(f'[{var_name}]', f'ProcessorModule_{str(value)}')
                                    if curly_matches:
                                        for curly_var in curly_matches:
                                            if curly_var in expansions:
                                                curly_values = expansions[curly_var]
                                                if isinstance(curly_values, list) and len(curly_values) > i:
                                                    new_path = new_path.replace(f'{{{curly_var}}}', str(curly_values[i]))
                                    expanded_info['DbusParams']['ObjectPath'] = new_path
                                    expanded_info['ParamID'] = base_param_id + i

                                    base_param_id_for_namespace = expanded_info['ID'].split('[')[0]
                                    namespace = shmem_namespaces.get(base_param_id_for_namespace)

                                    yaml_file.write(f"    - ID: {expanded_info['ID']}\n")
                                    yaml_file.write(f"      DataType: {expanded_info['DataType']}\n")
                                    if namespace:
                                        yaml_file.write("      ShmemParams:\n")
                                        key = f"{expanded_info['DbusParams']['ObjectPath']}/{expanded_info['DbusParams']['Interface']}.{expanded_info['DbusParams']['Property']}"
                                        yaml_file.write(f"        Key: {key}\n")
                                        yaml_file.write(f"        Namespace: {namespace}\n")
                                    yaml_file.write("      DbusParams:\n")
                                    for key, value in expanded_info['DbusParams'].items():
                                        yaml_file.write(f"        {key}: {value}\n")
                                    yaml_file.write(f"      FetchFreqSecs: {expanded_info['FetchFreqSecs']}\n")
                                    yaml_file.write(f"      FetchMethod: {'Shmem' if namespace else 'DBUS'}\n")
                                    yaml_file.write(f"      FetchType: {'GroupPoll' if namespace else 'Poll'}\n")
                                    yaml_file.write(f"      ParamID: {expanded_info['ParamID']}\n")
                                    yaml_file.write(f"      StoreFreqSecs: {expanded_info['StoreFreqSecs']}\n")
                                    yaml_file.write(f"      StorePolicy: {expanded_info['StorePolicy']}\n")
                                    items_written += 1
                                    if i != len(var_range) - 1:
                                        yaml_file.write("\n")
                            else:
                                for i in var_range:
                                    expanded_info = info.copy()
                                    expanded_info['ID'] = f"{info['ID']}[{i}]"
                                    expanded_info['DbusParams'] = info['DbusParams'].copy()
                                    new_path = info['DbusParams']['ObjectPath'].replace(f'[{var_name}]', str(i))
                                    if curly_matches:
                                        for curly_var in curly_matches:
                                            if curly_var in expansions:
                                                curly_values = expansions[curly_var]
                                                if isinstance(curly_values, list) and len(curly_values) > i:
                                                    new_path = new_path.replace(f'{{{curly_var}}}', str(curly_values[i]))
                                    expanded_info['DbusParams']['ObjectPath'] = new_path
                                    expanded_info['ParamID'] = base_param_id + i
                                    base_param_id_for_namespace = expanded_info['ID'].split('[')[0]
                                    namespace = shmem_namespaces.get(base_param_id_for_namespace)
                                    yaml_file.write(f"    - ID: {expanded_info['ID']}\n")
                                    yaml_file.write(f"      DataType: {expanded_info['DataType']}\n")
                                    if namespace:
                                        yaml_file.write("      ShmemParams:\n")
                                        key = f"{expanded_info['DbusParams']['ObjectPath']}/{expanded_info['DbusParams']['Interface']}.{expanded_info['DbusParams']['Property']}"
                                        yaml_file.write(f"        Key: {key}\n")
                                        yaml_file.write(f"        Namespace: {namespace}\n")
                                    yaml_file.write("      DbusParams:\n")
                                    for key, value in expanded_info['DbusParams'].items():
                                        yaml_file.write(f"        {key}: {value}\n")
                                    yaml_file.write(f"      FetchFreqSecs: {expanded_info['FetchFreqSecs']}\n")
                                    yaml_file.write(f"      FetchMethod: {'Shmem' if namespace else 'DBUS'}\n")
                                    yaml_file.write(f"      FetchType: {'GroupPoll' if namespace else 'Poll'}\n")
                                    yaml_file.write(f"      ParamID: {expanded_info['ParamID']}\n")
                                    yaml_file.write(f"      StoreFreqSecs: {expanded_info['StoreFreqSecs']}\n")
                                    yaml_file.write(f"      StorePolicy: {expanded_info['StorePolicy']}\n")
                                    items_written += 1
                                    if i != var_range[-1]:
                                        yaml_file.write("\n")
                        else:
                            print(f"DEBUG: Variable {var_name} not found in expansions for {comp_class} {group['ID']} {info['ID']}")
                    elif curly_matches:
                        var_name = curly_matches[0]
                        base_param_id = info.get('original_index', info['ParamID'])
                        if var_name in expansions:
                            var_range = expansions[var_name]
                            if isinstance(var_range, list):
                                for i, value in enumerate(var_range):
                                    expanded_info = info.copy()
                                    expanded_info['ID'] = f"{info['ID']}[{i}]"
                                    expanded_info['DbusParams'] = info['DbusParams'].copy()
                                    new_path = expanded_info['DbusParams']['ObjectPath'].replace(f'{{{var_name}}}', str(value))
                                    expanded_info['DbusParams']['ObjectPath'] = new_path
                                    expanded_info['ParamID'] = base_param_id + i
                                    base_param_id_for_namespace = expanded_info['ID'].split('[')[0]
                                    namespace = shmem_namespaces.get(base_param_id_for_namespace)
                                    yaml_file.write(f"    - ID: {expanded_info['ID']}\n")
                                    yaml_file.write(f"      DataType: {expanded_info['DataType']}\n")
                                    if namespace:
                                        yaml_file.write("      ShmemParams:\n")
                                        key = f"{expanded_info['DbusParams']['ObjectPath']}/{expanded_info['DbusParams']['Interface']}.{expanded_info['DbusParams']['Property']}"
                                        yaml_file.write(f"        Key: {key}\n")
                                        yaml_file.write(f"        Namespace: {namespace}\n")
                                    yaml_file.write("      DbusParams:\n")
                                    for key, value in expanded_info['DbusParams'].items():
                                        yaml_file.write(f"        {key}: {value}\n")
                                    yaml_file.write(f"      FetchFreqSecs: {expanded_info['FetchFreqSecs']}\n")
                                    yaml_file.write(f"      FetchMethod: {'Shmem' if namespace else 'DBUS'}\n")
                                    yaml_file.write(f"      FetchType: {'GroupPoll' if namespace else 'Poll'}\n")
                                    yaml_file.write(f"      ParamID: {expanded_info['ParamID']}\n")
                                    yaml_file.write(f"      StoreFreqSecs: {expanded_info['StoreFreqSecs']}\n")
                                    yaml_file.write(f"      StorePolicy: {expanded_info['StorePolicy']}\n")
                                    items_written += 1
                                    if i != len(var_range) - 1:
                                        yaml_file.write("\n")
                        else:
                            print(f"DEBUG: Variable {var_name} not found in expansions for {comp_class} {group['ID']} {info['ID']}")
                else:
                    base_param_id = info['ID'].split('[')[0] if '[' in info['ID'] else info['ID']
                    namespace = shmem_namespaces.get(base_param_id)
                    yaml_file.write(f"    - ID: {info['ID']}\n")
                    yaml_file.write(f"      DataType: {info['DataType']}\n")
                    if namespace:
                        yaml_file.write("      ShmemParams:\n")
                        key = f"{info['DbusParams']['ObjectPath']}/{info['DbusParams']['Interface']}.{info['DbusParams']['Property']}"
                        yaml_file.write(f"        Key: {key}\n")
                        yaml_file.write(f"        Namespace: {namespace}\n")
                    yaml_file.write("      DbusParams:\n")
                    for key, value in info['DbusParams'].items():
                        yaml_file.write(f"        {key}: {value}\n")
                    yaml_file.write(f"      FetchFreqSecs: {info['FetchFreqSecs']}\n")
                    yaml_file.write(f"      FetchMethod: {'Shmem' if namespace else 'DBUS'}\n")
                    yaml_file.write(f"      FetchType: {'GroupPoll' if namespace else 'Poll'}\n")
                    yaml_file.write(f"      ParamID: {info['ParamID']}\n")
                    yaml_file.write(f"      StoreFreqSecs: {info['StoreFreqSecs']}\n")
                    yaml_file.write(f"      StorePolicy: {info['StorePolicy']}\n")
                    items_written += 1

            print(f"DEBUG: Wrote {items_written} items for {comp_class} group {group['ID']}")
            # Insert compaction anchor if this is a sensor group (indent=4 for map property)
            write_compaction_anchor_if_sensor(yaml_file, group['ID'], indent=4)

def generate_sections_block(comp_classes, expansions):
    def str_presenter(dumper, data):
        if re.match(r'^\d+_\d+$', data):
            return dumper.represent_scalar('tag:yaml.org,2002:str', data, style='')
        return dumper.represent_scalar('tag:yaml.org,2002:str', data)
    pyyaml.SafeDumper.add_representer(str, str_presenter)

    def parse_value(val):
        try:
            if isinstance(val, str) and val.isdigit():
                return int(val)
            # Only convert to int if it's a pure digit
            return val
        except Exception:
            return val

    # Group expansion variables by prefix (before first '_')
    prefix_groups = defaultdict(list)
    for var_name in expansions:
        prefix = var_name.split('_')[0] if '_' in var_name else var_name
        prefix_groups[prefix].append(var_name)

    sections = []
    for var_name, expansion_list in expansions.items():
        if not var_name.isupper():
            continue
        section_id = var_name  # No prefix for section ID
        anchor = var_name.lower()
        param_name = var_name.lower() + 'id'
        comp_prefix = f'HGX_{var_name}'  # Prefix only for component IDs
        section = {'ID': section_id, 'Components': []}
        for i, value in enumerate(expansion_list):
            comp_id = f"{comp_prefix}_{i}"
            params = [{'name': param_name, 'value': parse_value(value)}]
            # Add extra params from grouped variables (e.g., cpu_0_*, cpu_sparechannel0id_eid)
            group_vars = prefix_groups.get(var_name.lower(), [])
            for extra_var in group_vars:
                # If the variable has an index, match pattern like cpu_0_* for index 0
                m = re.match(rf"{var_name.lower()}_(\d+)_(.+)", extra_var)
                if m:
                    extra_param_name = extra_var
                    extra_values = expansions[extra_var]
                    # If value is a list, use the ith value if available
                    if isinstance(extra_values, list):
                        if len(extra_values) == 1 and isinstance(extra_values[0], str) and ',' in extra_values[0]:
                            split_values = [v.strip() for v in extra_values[0].split(',')]
                        else:
                            split_values = [v for v in extra_values]
                        if i < len(split_values):
                            params.append({'name': extra_param_name, 'value': parse_value(split_values[i])})
                    else:
                        if i == 0:
                            params.append({'name': extra_param_name, 'value': parse_value(extra_values)})
                else:
                    # No index in variable name, assign Nth value to Nth component
                    extra_param_name = extra_var
                    extra_values = expansions[extra_var]
                    if isinstance(extra_values, list):
                        if len(extra_values) == 1 and isinstance(extra_values[0], str) and ',' in extra_values[0]:
                            split_values = [v.strip() for v in extra_values[0].split(',')]
                        else:
                            split_values = [v for v in extra_values]
                        if i < len(split_values):
                            params.append({'name': extra_param_name, 'value': parse_value(split_values[i])})
                    else:
                        if i == 0:
                            params.append({'name': extra_param_name, 'value': parse_value(extra_values)})
            comp = {'ID': comp_id, '<<': f"*{anchor}", 'Params': params}
            section['Components'].append(comp)
        sections.append(section)

    # Create the final structure with proper indentation
    final_structure = {'Sections': sections}
    
    # Dump the entire structure at once
    yaml_str = pyyaml.dump(final_structure, default_flow_style=False, sort_keys=False, Dumper=pyyaml.SafeDumper)
    
    # Replace quoted merge keys with correct YAML merge key syntax
    yaml_str = re.sub(r"'<<': '([*][^']+)'", r"<<: \1", yaml_str)
    
    # Remove quotes from value: '<digits>_<digits>'
    yaml_str = re.sub(r"(value: )'([0-9]+_[0-9]+)'", r"\1\2", yaml_str)
    
    return yaml_str

def load_shmem_namespaces(telemetry_file, shmem_column_index):
    """Load namespace mappings from TelemetryCatalog.csv."""
    namespaces = {}
    try:
        with open(telemetry_file, 'r', newline='') as csvfile:
            reader = csv.reader(csvfile)
            header = next(reader)  # Skip header
            for row in reader:
                if len(row) <= shmem_column_index:
                    continue
                param_id = row[2].strip()  # Metric column (ParamName)
                namespace = row[shmem_column_index].strip()
                if param_id and namespace:
                    namespaces[param_id] = namespace
        return namespaces
    except Exception as e:
        print(f"Error loading shmem namespaces: {e}")
        return {}

def generate_yaml_file(platform_data, general_config, preconditions, comp_classes=None, expansion_file=None, copyright_block=None, telemetry_file=None):
    column_index = platform_data.get('expansion_column_index', 1)
    shmem_column_index = platform_data.get('shmem_column_index', 16)  # Default to 16 if not specified
    
    try:
        expansions = load_dbus_expansion(expansion_file, column_index)
    except Exception as e:
        print(f"Warning: Error loading expansions from {expansion_file}: {e}")
        print("Will continue without variable expansion")
        expansions = {}
        
    # Load shmem namespaces
    shmem_namespaces = {}
    if telemetry_file:
        shmem_namespaces = load_shmem_namespaces(telemetry_file, shmem_column_index)
        print(f"Loaded {len(shmem_namespaces)} shmem namespace mappings")
    
    if not platform_data or 'name' not in platform_data:
        print("Error: Platform data missing or 'name' not found in platform data")
        print("Platform data:", platform_data)
        sys.exit(1)

    platform_name = platform_data['name']
    print(f"Using platform name: {platform_name}")
    
    output_filename = f"fdr_ppf_{platform_name}.yaml"
    output_dir = "generated"
    
    try:
        os.makedirs(output_dir, exist_ok=True)
        print(f"Ensuring output directory exists: {output_dir}")
    except Exception as e:
        print(f"Error creating output directory {output_dir}: {e}")
        sys.exit(1)
        
    output_path = os.path.join(output_dir, output_filename)
    print(f"Will generate YAML file at: {output_path}")

    print("\nDEBUG: Input data summary:")
    print(f"Number of component classes: {len(comp_classes) if comp_classes else 0}")
    if comp_classes:
        for comp_class, params in comp_classes.items():
            info_groups = params.get("InfoGroups", [])
            total_params = sum(len(group.get("InfoList", [])) for group in info_groups)
            print(f"  {comp_class}: {len(info_groups)} InfoGroups, {total_params} total parameters")
    
    written_anchors = set()
    try:
        with open(output_path, 'w') as yaml_file:
            # Write copyright block from input YAML
            if copyright_block:
                import datetime, re
                year = datetime.datetime.now().year
                block = re.sub(r'Copyright \(c\) \d{4}', f'Copyright (c) {year}', copyright_block)
                yaml_file.write(block.strip() + '\n\n')
            write_fingerprint_section(yaml_file, platform_data)
            write_general_config_and_preconditions(yaml_file, general_config, preconditions)
            write_component_classes(yaml_file, comp_classes, expansions, written_anchors, shmem_namespaces)
            # Write Sections block at the end, with a blank line before
            yaml_file.write('\n')
            sections_yaml = generate_sections_block(comp_classes, expansions)
            yaml_file.write(sections_yaml + '\n')
        print(f"\nDEBUG: Final summary of written data:")
        print(f"Output file: {output_path}")
        print(f"Total component classes processed: {len(comp_classes) if comp_classes else 0}")
        return output_path
    except Exception as e:
        print(f"Error writing YAML file {output_path}: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

def load_platform_config(input_yaml_path="ppf_input.yaml"):
    # Load input YAML
    input_data = load_input_yaml(input_yaml_path)
    
    # Extract platform data, general config, and preconditions
    platform = input_data.get('platform')
    if not platform:
        print("Error: No platform configuration found in input YAML.")
        sys.exit(1)
    
    general_config = input_data.get('general_config', {})
    preconditions = input_data.get('preconditions', {})
    
    return platform, general_config, preconditions

def process_csv_data(platform, telemetry_file=None):
    csv_path = find_telemetry_csv(telemetry_file)
    comp_classes = None
    
    if csv_path:
        print(f"Using telemetry CSV file: {csv_path}")
        
        platform_id = None
        override_column = None  # Initialize before use
        
        if 'applicable_column_index' in platform:
            override_column = platform['applicable_column_index']
            print(f"Using hard-coded column index from ppf_input.yaml: Column {override_column}")
        
            platform_id = extract_platform_id_from_fingerprint(platform['fingerprint'])
            
            if not platform_id:
                print("Warning: Using fallback platform identification method")
                platform_id = extract_platform_id_from_csv(csv_path)
        
        comp_classes = process_telemetry_csv(csv_path, platform, platform_id, override_column)
        print_comp_classes_summary(comp_classes)
    else:
        print("No telemetry CSV file found, proceeding without parameters")
    
    return comp_classes

def print_comp_classes_summary(comp_classes):
    if not comp_classes:
        return
        
    num_classes = len(comp_classes)
    print(f"Extracted parameters for {num_classes} CompClasses from CSV")
    
    # Print summary of extracted CompClasses
    for comp_class, params in comp_classes.items():
        info_group_count = len(params.get("InfoGroups", []))
        total_params = 0
        for group in params.get("InfoGroups", []):
            total_params += len(group.get("InfoList", []))
        print(f"  - {comp_class}: {info_group_count} InfoGroups, {total_params} parameters")

def main():
    print("\nStarting YAML generation...")
    
    parser = argparse.ArgumentParser(description='Generate YAML configuration file')
    parser.add_argument('--input', default='ppf_input.yaml', help='Input YAML file path')
    parser.add_argument('--expansion', help='Path to dbus-expansion.csv file')
    parser.add_argument('--telemetry', help='Path to TelemetryCatalog.csv file')
    args = parser.parse_args()
    
    print(f"\nUsing input files:")
    print(f"- Input YAML: {args.input}")
    print(f"- Expansion CSV: {args.expansion}")
    print(f"- Telemetry CSV: {args.telemetry}")

    print("\nLoading platform config...")
    input_data = load_input_yaml(args.input)
    copyright_block = input_data.get('copyright', None)
    platform = input_data.get('platform')
    if not platform:
        print("Error: No platform configuration found in input YAML.")
        sys.exit(1)
    general_config = input_data.get('general_config', {})
    preconditions = input_data.get('preconditions', {})
    print(f"Loaded platform name: {platform.get('name', 'NOT FOUND')}")
    
    print("\nProcessing CSV data...")
    comp_classes = process_csv_data(platform, args.telemetry)
    if comp_classes:
        print(f"Found {len(comp_classes)} component classes")
    else:
        print("No component classes found!")
    
    print("\nGenerating YAML file...")
    output_path = generate_yaml_file(
        platform, 
        general_config, 
        preconditions, 
        comp_classes, 
        args.expansion, 
        copyright_block,
        args.telemetry  # Add telemetry file parameter
    )
    
    print(f"\nYAML generation completed:")
    print(f"- Output file: {output_path}")
    
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print(f"\nError during execution: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)