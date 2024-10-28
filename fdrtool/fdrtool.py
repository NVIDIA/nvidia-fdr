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
# Import third-party library modules
import redfish
from datetime import datetime
# Import locally developed modules
from catalog.catalog import DECODE_FORMAT, Catalog
from tqdm import tqdm

tool_version = '1.0.0'

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
            for i in tqdm(range(int(9e6)),ncols=100,desc ="Dump collection"):
                pass
            return args.local_file
    
    except Exception as e:
        print("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n")
        logging.error("fdrtool failed!\nException caught: \n{}\n".format(e))
        

def decodingDump(binary_log_tar_file):
    #print("\n--------------------- Decoding FDR dump -----------------------")
    #print("\nFDR dump to be decoded: {}".format(binary_log_tar_file))
    # Remove existing logs directory to avoid issues with overlapping of logs in different formats
     
    log_root_dir = './fdr_logs/'
    if os.path.exists(log_root_dir):
      shutil.rmtree(log_root_dir)
    # Step-2: Unzip the .tar file
    print ("unzip the logs")
    binary_log = tarfile.open(binary_log_tar_file)
    binary_log.extractall(log_root_dir) # This will create a directory if it's not present already.
    binary_log.close()
    for i in tqdm(range(int(9e6)),ncols=100,desc ="Decoding dump"):
        pass
    #print('Successfully unzipped the tar archive of binary logs into {}.'.format(log_root_dir))
    
    return log_root_dir


def decodeBirthCertificate(file_location):
    BirthCertificate_logs = './fdr_logs/birthcertificate'
    if os.path.exists(BirthCertificate_logs):
      shutil.rmtree(BirthCertificate_logs)
    binary_log = tarfile.open(file_location)
    binary_log.extractall(BirthCertificate_logs)
    binary_log.close()
    for i in tqdm(range(int(9e6)),ncols=100,desc ="Decoding BirthCertificate dump"):
        pass
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
   
   
   argget.add_argument('-i', '--ip', type=str, help='Address of host, using http or https (example: https://123.45.6.7:8000)')
   argget.add_argument('-u', '--username', type=str, help='Username for Authentication')
   argget.add_argument('-p', '--password', type=str, help='Password for Authentication')
   argget.add_argument('-e', '--environment', type=str, help='Location of the machine. Field(FIE), Factory(FAC), Unknown(UNK)', default="UNK")

   # decode option
   group_decode_format = argget.add_mutually_exclusive_group(required=True)
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
   log_root_dir =  decodingDump(binary_log_tar_file)

   # Step-3: Create catalog of decoded binary logs
   MyCatalog = Catalog(vars(args), log_root_dir)

   # Step-4: Write the logs in intended format
   MyCatalog.WriteAllEntries()



  # Additional option to decode the Birthcertificate.
   if args.birth_certificate:
     tar_file='BirthCertificate.tar'
     os.system('cp ./fdr_logs/fdr/Bookkeeper/BirthCertificate.tar .')
     log_root_dir = decodeBirthCertificate(tar_file)
     MyCatalog = Catalog(vars(args), log_root_dir)
     MyCatalog.WriteAllEntries()

   else:
     print("Warning : To Decode Birthcertificate.tar use option -bc ")


  # Step-5: Clean up
   if MyCatalog:
    MyCatalog.Close()

  # If Coverage Report is asked:
   if not is_customer_view:
    if args.generate_cvg_report:
      for i in tqdm(range(int(9e6)),ncols=100,desc ="Generating Coverage Report.."):
        pass
      if args.fdr_output_dir is None or args.fdr_output_dir == "":
        args.fdr_output_dir="./fdr_logs/fdr"
      check_fdr_telemetry_coverage.generate_coverage_report(args=args)

    if args.value_validation_report:
      for i in tqdm(range(int(9e6)),ncols=100,desc ="Generating Value Validation Report.."):
        pass

      if args.fdr_output_dir is None or args.fdr_output_dir == "":
        args.fdr_output="./fdr_logs/fdr"
      else:
        args.fdr_output=args.fdr_output_dir
      data_validator.generate_value_validation_report(args=args)

   end_time = datetime.now()
   print('\nTotal time taken: {} seconds.'\
          .format((end_time - start_time).total_seconds()))
   

   return status_code


def CollectFdrDump(host_ip, username, password, env_tag="UNK"):
  from datetime import datetime
  start_time = datetime.now()
  
  print(f"Trying to reach the host {host_ip}....")
  # Using Python redfish library (https://github.com/DMTF/python-redfish-library)
  REDFISH_OBJ = redfish.redfish_client(base_url=host_ip, username=username, \
                      password=password, default_prefix='/redfish/v1/')
  REDFISH_OBJ.login(auth="basic")
  print('Successfully logged in to host {}'.format(host_ip))
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
    # Wait for the dump to be ready
    task = response.monitor(REDFISH_OBJ)
    import time
    while task.is_processing:
        retry_time = task.retry_after if task.retry_after else 5
        task_status = task.dict['TaskState']
        print('FDR Dump Task Status: {}. Retrying after {} seconds...'.format(task_status, retry_time))
        time.sleep(retry_time)
        task = response.monitor(REDFISH_OBJ)
  elif response.status != 200:
    raise Exception("FDR dump request failed! Response received:\n{}.".format(response))
  # To-do: need to add other cases?
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
  # binary_log_tar_file = f'fdr_dump_{dump_timestamp}_{task_id}.tar.xz'
  binary_log_tar_file = f'./tmp/HMC_{env_tag}_SN{serial_number}_{dump_timestamp.strftime("%m%d%Y_%H%M%S")}.tar.xz'
  url = f"{entry_location}/attachment"
  print(f"\nDownloading FDR dump {binary_log_tar_file}....")
  print(f"Redfish API: {url}")
  response = REDFISH_OBJ.get(url)
  with open(binary_log_tar_file, 'wb') as fd:
    fd.write(response.read)
  print(f"\nSuccessfully downloaded the FDR dump!")
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

