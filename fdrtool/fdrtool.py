#! /usr/bin/env python3

'''
Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
'''
# Import standard library modules



import sys
import logging
import argparse
import configargparse
import tarfile
import traceback
import shutil
import os
import subprocess
# Import third-party library modules
import redfish
from datetime import datetime
import time
# Import locally developed modules
from catalog.catalog import DECODE_FORMAT, Catalog
from tqdm import tqdm
from pathlib import Path
from typing import List, Set
from fnmatch import fnmatch

tool_version = '3.0'

logging.basicConfig(
    level=logging.INFO,
    format='%(levelname)s:%(name)s:%(message)s',
    stream=sys.stdout  # or to a file
)

def dumpCollection(args):
    #print("**********************Nvidia fdrtool**********************")
    #print("Selected option: {}".format("Use local tarball." if args.use_local else "Retrieve logs from HMC."))
    # Basic execution flow
    # Step-1: Redfish API call to get the zip file of fdr logs from HMC.
    MyCatalog = None
    try:
        binary_log_tar_file = ''
        if not args.use_local:
            #print("\n----------- Collecting FDR dump from host {} -----------".format(args.ip))
            # Store the downloaded fdr dumps in ./tmp/
            os.makedirs("./tmp/", exist_ok=True)
            binary_log_tar_file = CollectFdrDump(args.ip, args.username, args.password, args.environment)
            #binary_log_tar_file = CollectFdrDump_DEMO(args.ip, args.username, args.password)
            return binary_log_tar_file 
        else:# Retrieve the zip file from local machine
            for i in tqdm(range(int(9e6)),ncols=100,desc ="Dump collection", file=sys.stderr):
                pass
            return args.local_file
    
    except Exception as e:
        print("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n")
        logging.error("fdrtool failed!\nException caught: \n{}\n".format(e))

def cleanUpFDRLogDirectory(ip, username, password):
  print(f"Trying to reach the host {ip}....")
  REDFISH_OBJ = redfish.redfish_client(
    base_url=ip,
    username=username,
    password=password,
    default_prefix='/redfish/v1/'
  )
  REDFISH_OBJ.login(auth="basic")
  url = "/redfish/v1/Systems/HGX_Baseboard_0/LogServices/FDR/Actions/LogService.ClearLog/"
  response = REDFISH_OBJ.post(url)
  if response.status != 200:
    raise Exception("Failed to clear FDR log directory! Response received:\n{}.".format(response))
  else:
    print(f"Successfully cleared FDR log directory!")

def decodingDump(binary_log_tar_file, log_root_dir='./fdr_logs/'):
    if os.path.exists(log_root_dir):
        shutil.rmtree(log_root_dir)

    print("Extracting the dump...")
    binary_log = tarfile.open(binary_log_tar_file)

    # Get total number of files in the archive
    members = binary_log.getmembers()
    total_files = len(members)

    # Extract with real progress tracking
    for i, member in enumerate(tqdm(members, desc="Extracting files", ncols=100, file=sys.stderr)):
        binary_log.extract(member, log_root_dir)
    # Check for compressed YAML files recursively
    print("Checking for compressed YAML files...")
    yaml_files = []
    for root, dirs, files in os.walk(log_root_dir):
        for file in files:
            if file.endswith('.yaml'):
                yaml_path = os.path.join(root, file)
                yaml_files.append(yaml_path)

    for yaml_path in yaml_files:
        result = subprocess.run(['file', yaml_path], capture_output=True, text=True)
        output = result.stdout.lower()

        if any(keyword in output for keyword in ['compressed', 'archive', 'tar']):
            # print(f"Found compressed YAML file: {yaml_path}")
            temp_dir = f"{yaml_path}_temp_extract"
            os.makedirs(temp_dir, exist_ok=True)
            try:
                subprocess.run(
                    ['tar', '--strip-components=1', '-xf', yaml_path, '-C', temp_dir], 
                    check=True
                )
                extracted_files = [f for f in os.listdir(temp_dir) if f.endswith('.yaml')]
                if extracted_files:
                    extracted_file_path = os.path.join(temp_dir, extracted_files[0])
                    shutil.move(extracted_file_path, yaml_path)
                    # print(f"Successfully replaced {yaml_path} with uncompressed version")
                # else:
                    # print(f"No YAML files found in the extracted content of {yaml_path}")
                shutil.rmtree(temp_dir)
            except subprocess.CalledProcessError as e:
                print(f"Failed to decompress {yaml_path}: {e}")
                if os.path.exists(temp_dir):
                    shutil.rmtree(temp_dir)
    binary_log.close()
    return log_root_dir


def decodeBirthCertificate(file_location, log_root_dir='./fdr_logs/'):
    BirthCertificate_logs = os.path.join(log_root_dir, 'BirthCertificate')
    if os.path.exists(BirthCertificate_logs):
        shutil.rmtree(BirthCertificate_logs)

    binary_log = tarfile.open(file_location)

    # Get members and show real progress
    members = binary_log.getmembers()
    for member in tqdm(members, desc="Extracting Birth Certificate", ncols=100, file=sys.stderr):
        binary_log.extract(member, BirthCertificate_logs)

    binary_log.close()
    return BirthCertificate_logs

    
    
    
def main(arglist=None):
   is_customer_view = True
   try:
      with open("./.customer_view", 'r+') as view:
        is_customer_view = bool(int(view.read().strip()))
   except:
      pass
  #  is_customer_view = os.path.exists("./.customer_view")
   if not is_customer_view:
     try:
       import check_fdr_telemetry_coverage
       import data_validator
     except:
       pass
   start_time = datetime.now()
   status_code = 0
   argget = configargparse.ArgParser(prog='nvidia-fdrtool',\
                                  description='FDR tool to decode fdr logs received from HMC, version {}'.format(tool_version))
   # Arguements:
   argget.add_argument('-c', '--config_file', required=False, is_config_file=True, help='Config file path')
   
   # host info
   # Redfish or local file for decoding (for development/test purpose)
   argget.add_argument('-ul', '--use_local', default=False, action='store_true', help='Option to use local tar archive of binary logs if Redfish API for FDR dump is not available.')
   argget.add_argument('-bc', '--birth_certificate', default=False, action='store_true', help='Option to decode BirthCertificate.tar')
   argget.add_argument('-l', '--local_file', type=str, help='Local tar archive of binary logs if Redfish API for FDR dump is not available.')
   argget.add_argument('-lr', '--log_root_dir', type=str, default='./fdr_logs/', help='Directory to store the logs.')
   
   
   argget.add_argument('-i', '--ip', type=str, help='Address of host, using http or https (example: https://123.45.6.7:8000)')
   argget.add_argument('-u', '--username', type=str, help='Username for Authentication')
   argget.add_argument('-p', '--password', type=str, help='Password for Authentication')
   argget.add_argument('-e', '--environment', type=str, help='Location of the machine. Field(FIE), Factory(FAC), Unknown(UNK)', default="UNK")

   # decode option
   group_decode_format = argget.add_mutually_exclusive_group(required=False)
   group_decode_format.add_argument("--json", default=False, action="store_const", const = DECODE_FORMAT.JSON,  help="Decode the binary protobuf logs to JSON and store them locally.", dest = 'decode_format')
   group_decode_format.add_argument("--influx", default=False, action="store_const", const = DECODE_FORMAT.INFLUX, help="Decode the binary protobuf logs to appropriate format (line-protocol) to be written to InfluxDB server.", dest = 'decode_format')
   group_decode_format.add_argument("--sqlite", default=False, action="store_const", const = DECODE_FORMAT.SQLITE, help="Decode the binary protobuf logs to appropriate format (line-protocol) to be written to local SQLite database.", dest = 'decode_format')

  # json info
   argget.add_argument('-kn', '--key_name', default=False, action='store_true', help='Option to replace the ParamIDs with ParamName in the decoded logs.')
   if not is_customer_view:
    argget.add_argument('-gcr', '--generate_cvg_report', default=False, action='store_true', help='Option to also generate coverage report if enabled and decode format is json')
    argget.add_argument('-tc', '--telemetry_catalog', required=False, type=str, help='Used for Coverage Report, Provide Path to the telemetry catalog.')
    argget.add_argument('-tu', '--telemetry_uri_exp', required=False, type=str, help='Used for Coverage Report, Provide Path to the URI expansion.')
    argget.add_argument('-pl', '--platform', required=False, default="Vulcan", type=str, help='Used for Coverage Report, Provide Target platform to test')
    argget.add_argument('-el', '--exempt_list', required=False, default=None, type=str, help='Used for Coverage Report, Provide Exempt List CSV')
    argget.add_argument('-fd', '--fdr_output_dir', required=False, type=str, help='Path to the directory where dump has been decoded into JSON')
    # data validation
    argget.add_argument('-vvr', '--value_validation_report', default=False, action='store_true', help='Option to also generate Data validation report if enabled and decode format is json')
    argget.add_argument('-ltc', '--telemetry_agent_output', required=False, type=str, help='Used for Data Validation Report, Provide Path to the Latest Telemetry CSV for specified platform.')
    argget.add_argument('-fdo', '--fdr_output', required=False, type=str, help='Path to the directory where dump has been decoded in JSON')
    argget.add_argument('-flc', '--clean_fdr_log_dir', default=False, action='store_true', help='Option to cleanup FDR log directory on BMC/HMC')

    
   # influxDB info
   argget.add_argument("--influx_url", type=str, help="InfluxDB Host URL")
   argget.add_argument("--influx_org", type=str, help="InfluxDB Host Orginization")
   argget.add_argument("--influx_token", type=str, help="Token for authenticating to InfluxDB Host")
  
  # sqlite info
   argget.add_argument('-a', '--append', default=False, action='store_true', help='Option to append the logs to existing database. If this flag is not provided, any existing database with same name will be deleted first.')
  
  # binary file format (for development purpose to support both length and zero delimited binary)
   argget.add_argument('-ld', '--length_delimited', default=False, action='store_true', help='Option to support decoding of length-delimited binary. If this flag is not provided, the decoding will be performed for zero-delimited COBS-R binary.')
  
  # Parse the arguments
   args = argget.parse_args()
   arg_error = False

   if not is_customer_view and args.clean_fdr_log_dir:
     if not args.ip or not args.username or not args.password:
       logging.error('Missing host information for clean up.')
       arg_error = True
     if arg_error:
       argget.print_help()
       return 1
     cleanUpFDRLogDirectory(args.ip, args.username, args.password)
     return 0

   if not args.decode_format:
     logging.error('One of the arguments --json --influx --sqlite is required.')
     arg_error = True

   if not args.use_local and (not args.ip or not args.username or not args.password):
    logging.error('Missing host information.')
    arg_error = True

   if args.use_local and not args.local_file:
    logging.error('Missing local logs file information.')
    arg_error = True

   if args.key_name and args.decode_format != DECODE_FORMAT.JSON:
    logging.error('--key_name should be provided only while decoding to json.')
    arg_error = True

   if args.decode_format == DECODE_FORMAT.INFLUX and (not args.influx_url or not args.influx_token or not args.influx_org):
    logging.error('Missing InfluxDB information.')
    arg_error = True

   if args.append and args.decode_format != DECODE_FORMAT.SQLITE:
    logging.error('--append should be provided only while decoding to sqlite.')
    arg_error = True

   if args.environment not in ["FIE", "FAC", "UNK"]:
    logging.error('Invalid environment provided')
    args.environment = "UNK"

   if not is_customer_view:
    if args.generate_cvg_report:
      if args.decode_format != DECODE_FORMAT.JSON:
        logging.error('Coverage report can be generated only when decode format is json')
        return
      else:
        if not args.telemetry_catalog:
          logging.error('\n--telemetry_catalog and --telemetry_uri_exp are required for generating coverage report')
          return

        if not args.telemetry_uri_exp:
          logging.error('\n--telemetry_catalog and --telemetry_uri_exp are required for generating coverage report')
          return

    if args.value_validation_report:
      if args.decode_format != DECODE_FORMAT.JSON:
        logging.error('Data Validation report can be generated only when decode format is json')
        return
      else:
        if not args.telemetry_agent_output:
          logging.error('\n--telemetry_agent_output csv is required to generate data validation report')
          return

   if arg_error:
    argget.print_help()
    return 1
   
   # Step-1: Redfish API call to get the zip file of fdr logs from HMC.
   binary_log_tar_file = dumpCollection(args)

   # Step-2: Unzip the .tar file
   log_root_dir =  decodingDump(binary_log_tar_file, args.log_root_dir)

   # Step-3: Create catalog of decoded binary logs
   MyCatalog = Catalog(vars(args), log_root_dir)

   # Step-4: Write the logs in intended format
   MyCatalog.WriteAllEntries()



  # Additional option to decode the Birthcertificate.
   if args.birth_certificate:
     tar_file = os.path.join(log_root_dir, 'fdr/Bookkeeper/BirthCertificate.tar')
     log_root_dir = decodeBirthCertificate(tar_file, args.log_root_dir)
     try:
      MyCatalog = Catalog(vars(args), log_root_dir)
      MyCatalog.WriteAllEntries()
     except Exception as e:
      shutil.rmtree(log_root_dir)
      print("=========================================================")
      print(f"    {e}        ")
      print("  Skipping BirthCertificate Decode. Continuting....      ")
      print("=========================================================")

   else:
     print("=========================================================")
     print("      To Decode Birthcertificate.tar use option -bc      ")
     print("=========================================================")


  # Step-5: Clean up
   if MyCatalog:
    MyCatalog.Close()

  # If Coverage Report is asked:
   if not is_customer_view:
    if args.generate_cvg_report:
      if args.fdr_output_dir is None or args.fdr_output_dir == "":
        args.fdr_output_dir= f"{args.log_root_dir}/fdr"
      check_fdr_telemetry_coverage.generate_coverage_report(args=args)

    if args.value_validation_report:
      if args.fdr_output_dir is None or args.fdr_output_dir == "":
        args.fdr_output= f"{args.log_root_dir}/fdr"
      else:
        args.fdr_output=args.fdr_output_dir
      data_validator.generate_value_validation_report(args=args)
    # Checking if Important files are present in decoded dir.
    search_path = Path(args.log_root_dir).resolve()
    patterns_to_find = [
      'Bookkeeper',
      'fdr_manifest.txt',
      'fdr_ppf_*.yaml',
      'journalctl*',
    ]
    unmatched_patterns = set(patterns_to_find)
    matched_files = set()

    for root, dirs, files in os.walk(search_path):
      current_items = files + dirs
      for item in current_items:
        for pattern in list(unmatched_patterns):
          if fnmatch(item, pattern):
            matched_files.add(item)
            unmatched_patterns.discard(pattern)

    if unmatched_patterns:
      print("\nDid not find following files:")
      for pattern in sorted(unmatched_patterns):
        print(f"- {pattern}")

   end_time = datetime.now()
   print('\nTotal time taken: {} seconds.'\
          .format((end_time - start_time).total_seconds()))
   

   return status_code

def CheckIfNvidaFdrIsActive(REDFISH_OBJ):
  print('Checking if service is enabled...')
  url = "/redfish/v1/Systems/HGX_Baseboard_0/LogServices/FDR"
  response = REDFISH_OBJ.get(url)
  service_enabled = response.dict.get('ServiceEnabled')
  if service_enabled:
    print("Nvidia-FDR service is enabled. Continuing...")
  else:
    raise Exception("Nvidia-FDR service is not enabled on HMC, Exiting...")

def CollectFdrDump(host_ip, username, password, env_tag="UNK"):
  start_time = datetime.now()
  print(f"Trying to reach the host {host_ip}....")
  REDFISH_OBJ = redfish.redfish_client(base_url=host_ip, username=username, \
                      password=password, default_prefix='/redfish/v1/')
  REDFISH_OBJ.login(auth="basic")
  print('Successfully logged in to host {}'.format(host_ip))
  CheckIfNvidaFdrIsActive(REDFISH_OBJ)
  # Trigger the FDR dump first.
  body = {"DiagnosticDataType":"OEM", "OEMDiagnosticDataType":"DiagnosticType=FDR"}  
  url = "/redfish/v1/Systems/HGX_Baseboard_0/LogServices/FDR/Actions/LogService.CollectDiagnosticData/"
  url_serial_number = "/redfish/v1/Chassis/HGX_BMC_0"
  
  response = REDFISH_OBJ.get(url_serial_number)
  serial_number = response.dict.get('SerialNumber')

  print(f"\nTriggering FDR dump....")
  print(f"Redfish API: {url} {body}")
  response = REDFISH_OBJ.post(url, body=body)
  task_id = response.dict.get('Id')

  if response.is_processing:
    task = response.monitor(REDFISH_OBJ)
    task_start_time = time.time()

    # Suppress redfish logging during progress bar display
    import logging
    redfish_logger = logging.getLogger('redfish')
    original_level = redfish_logger.level
    redfish_logger.setLevel(logging.ERROR)  # Only show errors, not info/debug

    # Create progress bar for task monitoring with better configuration
    with tqdm(desc="FDR Dump Task", unit="", ncols=100, 
              bar_format='{l_bar}{bar}| {desc} [{elapsed}<{remaining}]', file=sys.stderr) as pbar:
      status_count = 0
      while task.is_processing:
        retry_time = task.retry_after if task.retry_after else 5
        task_status = task.dict['TaskState']

        # Update progress bar with current status and elapsed time
        status_count += 1
        elapsed_time = time.time() - task_start_time

        # Create a more descriptive status message
        if task_status == 'Running':
          status_desc = f"Collecting FDR data... ({elapsed_time:.0f}s)"
          pbar.set_description(f"🔄 {status_desc}")
        elif task_status == 'Completed':
          status_desc = f"Task completed! ({elapsed_time:.0f}s)"
          pbar.set_description(f"✅ {status_desc}")
          break
        else:
          status_desc = f"{task_status}... ({elapsed_time:.0f}s)"
          pbar.set_description(f"📊 {status_desc}")

        # Update progress bar (just to show activity, not actual progress)
        pbar.update(0)  # Don't increment counter, just refresh display

        if status_count % 2 == 0:
          pbar.set_postfix_str("⏳ Processing")
        else:
          pbar.set_postfix_str("⏳ Processing.")

        time.sleep(retry_time)
        task = response.monitor(REDFISH_OBJ)

        if time.time() - task_start_time > 300:  # 5 minutes timeout
          pbar.set_description("❌ Task timed out")
          redfish_logger.setLevel(original_level)  # Restore logging
          raise Exception("FDR dump task timed out after 5 minutes")

    # Restore redfish logging
    redfish_logger.setLevel(original_level)
    print(f"\n✅ FDR dump task completed successfully!")

  elif response.status != 200:
    raise Exception("FDR dump request failed! Response received:\n{}.".format(response))

  # Verify the dump status
  url = response.dict['@odata.id']
  print(f'Checking FDR dump task status: {url}')
  task = REDFISH_OBJ.get(url)
  if task.dict['TaskState'] == 'Completed':
    print('\nFDR dump is ready to be downloaded!')
  else:
    raise Exception("FDR dump request failed! Task status received:\n{}.".format(task))

  # Collect dump after TaskState becomes "Completed"
  entry_location=None
  for http_header in task.dict['Payload']['HttpHeaders']:
    if 'Location' in  http_header:
      entry_location=http_header.split(': ')[1]
  if entry_location is None:
    raise Exception("FDR dump path could not be found in the response! Response received:\n{}.".format(task))

  dump_timestamp = datetime.now()
  binary_log_tar_file = f'./tmp/HMC_{env_tag}_SN{serial_number}_{dump_timestamp.strftime("%m%d%Y_%H%M%S")}.tar.xz'
  url = f"{entry_location}/attachment"
  print(f"\nDownloading FDR dump {binary_log_tar_file}....")
  print(f"Redfish API: {url}")
  # Download with progress bar - Fixed for Redfish response
  response = REDFISH_OBJ.get(url)
  
  # Suppress redfish logging during download progress bar
  redfish_logger = logging.getLogger('redfish')
  original_level = redfish_logger.level
  redfish_logger.setLevel(logging.ERROR)  # Only show errors, not info/debug
  
  # Get the response data
  response_data = response.read
  
  # Show download progress (since we can't stream, show a simple progress bar)
  if response_data:
    data_size = len(response_data)
    print(f"📦 Download size: {data_size:,} bytes ({data_size/1024/1024:.1f} MB)")
    
    # Write data with progress bar
    with tqdm(total=data_size, unit='B', unit_scale=True, 
              desc="📥 Downloading", ncols=100,
              bar_format='{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}, {rate_fmt}]', file=sys.stderr) as pbar:
      with open(binary_log_tar_file, 'wb') as fd:
        # Write in chunks to show progress
        chunk_size = 8192
        for i in range(0, data_size, chunk_size):
          chunk = response_data[i:i + chunk_size]
          fd.write(chunk)
          pbar.update(len(chunk))
  else:
    # Fallback to original method if no data
    print("⚠️  No data received from server, attempting fallback download...")
    with tqdm(desc="📥 Downloading (fallback)", ncols=100, file=sys.stderr) as pbar:
      with open(binary_log_tar_file, 'wb') as fd:
        fd.write(response.read)
        pbar.update(1)
    
    # Verify file was downloaded successfully
    if os.path.getsize(binary_log_tar_file) == 0:
      redfish_logger.setLevel(original_level)  # Restore logging
      raise Exception("❌ Downloaded file is empty. Download may have failed.")
  
  # Restore redfish logging
  redfish_logger.setLevel(original_level)
  print(f"\n✅ Successfully downloaded the FDR dump!")
  REDFISH_OBJ.logout()
  print("\nLogged out of host {}".format(host_ip))
  
  end_time = datetime.now()
  print("\nFinished collecting FDR dump from {}. Time taken: {} seconds".format(host_ip, (end_time - start_time).total_seconds()))
  return binary_log_tar_file

def CollectFdrDump_DEMO(host_ip, username, password):
  from datetime import datetime
  start_time = datetime.now()
  
  print(f"Trying to reach the host {host_ip}....")
  # Using Python redfish library (https://github.com/DMTF/python-redfish-library)
  #REDFISH_OBJ = redfish.redfish_client(base_url=host_ip, username=username, \
  #                    password=password, default_prefix='/redfish/v1/')
  #REDFISH_OBJ.login(auth="basic")
  print('Successfully logged in to host {}'.format(host_ip))
  # Trigger the FDR dump first.
  body = {"DiagnosticDataType":"OEM", "OEMDiagnosticDataType":"DiagnosticType=FDR"}
  #url = "/redfish/v1/Systems/HGX_Baseboard_0/LogServices/Dump/Actions/LogService.CollectDiagnosticData/"
  url = "/redfish/v1/Systems/HGX_Baseboard_0/LogServices/FDR/Actions/LogService.CollectDiagnosticData/"
  print(f"\nTriggering FDR dump....")
  print(f"Redfish API: {url} {body}")
  #response = REDFISH_OBJ.post(url, body=body)
  #task_id = response.dict.get('Id')
  task_id=5802
  download_time = 60 # seconds. total time = 4min
  wait_time = 0
  import time
  while wait_time < download_time:
    retry_time = 30
    task_status = 'Running'
    print('FDR Dump Task Status: {}. Retrying after {} seconds...'.format(task_status, retry_time))
    time.sleep(retry_time)
    wait_time += retry_time
  # To-do: need to add other cases?
  # Verify the dump status
  print(f'Checking FDR dump task status.')
  print('\nFDR dump is ready to be downloaded!')
  
  # Collect dump after TaskState becomes "Completed"
  entry_id = 11
  entry_location=f'/redfish/v1/Systems/HGX_Baseboard_0/LogServices/FDR/Entries/{entry_id}/attachment'
  dump_timestamp = datetime.now()
  binary_log_tar_file = f'fdr_dump_{task_id}.tar.xz'
  url = f"{entry_location}/attachment"
  print(f"\nDownloading FDR dump {binary_log_tar_file}....")
  print(f"Redfish API: {url}")
  #response = REDFISH_OBJ.get(url)
  # with open(binary_log_tar_file, 'wb') as fd:
  #   fd.write(response.read)
  print(f"\nSuccessfully downloaded the FDR dump!")
  #REDFISH_OBJ.logout()
  print("\nLogged out of host {}".format(host_ip))
  
  end_time = datetime.now()
  print("\nFinished collecting FDR dump from {}. Time taken: {} seconds".format(host_ip, (end_time - start_time).total_seconds()))
  binary_log_tar_file = '/home/afsanac/fdr/fdr_dumps/fdr_dump_5802.tar.xz'
  return binary_log_tar_file

if __name__ == '__main__':
    status_code = main()
    sys.exit(status_code)

