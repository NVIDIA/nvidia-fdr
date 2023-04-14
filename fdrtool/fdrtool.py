#! /usr/bin/env python3

'''
Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
# Import locally developed modules
from catalog.catalog import DECODE_FORMAT, LOG_DIRECTORY, Catalog

tool_version = '1.0.0'

'''
Main method
'''
def main(arglist=None, configFile=None):
  """Main command

  Args:
    argslist ([type], optional): List of arguments in the form of argv. Defaults to None.
  """ 

  from datetime import datetime
  start_time = datetime.now()

  argget = configargparse.ArgParser(prog='nvidia-fdrtool',\
                                  description='FDR tool to decode fdr logs received from HMC, version {}'.format(tool_version))

  # Arguements:
  argget.add_argument('-c', '--config_file', required=False, is_config_file=True, help='Config file path')
    
  # host info
  # Redfish or local file for decoding (for development/test purpose)
  argget.add_argument('-ul', '--use_local', default=False, action='store_true', help='Option to use local tar archive of binary logs if Redfish API for FDR dump is not available.')
  
  argget.add_argument('-l', '--local_file', type=str, help='Local tar archive of binary logs if Redfish API for FDR dump is not available.')
  
  argget.add_argument('-i', '--ip', type=str, help='Address of host, using http or https (example: https://123.45.6.7:8000)')
  argget.add_argument('-u', '--username', type=str, help='Username for Authentication')
  argget.add_argument('-p', '--password', type=str, help='Password for Authentication')

  # decode option
  group_decode_format = argget.add_mutually_exclusive_group(required=True)
  group_decode_format.add_argument("--json", default=False, action="store_const", const = DECODE_FORMAT.JSON,  help="Decode the binary protobuf logs to JSON and store them locally.", dest = 'decode_format')
  group_decode_format.add_argument("--influx", default=False, action="store_const", const = DECODE_FORMAT.INFLUX, help="Decode the binary protobuf logs to appropriate format (line-protocol) to be written to InfluxDB server.", dest = 'decode_format')
  group_decode_format.add_argument("--sqlite", default=False, action="store_const", const = DECODE_FORMAT.SQLITE, help="Decode the binary protobuf logs to appropriate format (line-protocol) to be written to local SQLite database.", dest = 'decode_format')

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
  
  if args.decode_format == DECODE_FORMAT.INFLUX and (not args.influx_url or not args.influx_token or not args.influx_org):
    logging.error('Missing InfluxDB information.')
    arg_error = True
  
  if args.append and args.decode_format != DECODE_FORMAT.SQLITE:
    logging.error('--append should be provided only while decoding to sqlite.')
    arg_error = True
  
  if arg_error:
    argget.print_help()
    return 1

  # Basic execution flow
  # Step-1: Redfish API call to get the zip file of fdr logs from HMC.
  MyCatalog = None
  status_code = 0
  try:
    binary_log_tar_file = ''
    if not args.use_local:
      binary_log_tar_file = CollectFdrDump(args.ip, args.username, args.password)
    else: # Retrieve the zip file from local machine
      binary_log_tar_file = args.local_file

    # Remove existing logs directory to avoid issues with overlapping of logs in different formats
    if os.path.exists(LOG_DIRECTORY):
      shutil.rmtree(LOG_DIRECTORY)
     # Step-2: Unzip the .tar file
    binary_log = tarfile.open(binary_log_tar_file)
    binary_log.extractall(LOG_DIRECTORY) # This will create a directory if it's not present already.
    binary_log.close()
    print('Successfully unzipped the tar archive of binary logs.')

    # Step-3: Create catalog of decoded binary logs
    MyCatalog = Catalog(vars(args))

    # Step-4: Write the logs in intended format
    MyCatalog.WriteAllEntries()

  except Exception as e:
    logging.error("fdrtool failed!")
    traceback.print_exc()
    status_code = 1

  # Step-5: Clean up
  if MyCatalog:
    MyCatalog.Close()

  end_time = datetime.now()
  print('***********End of fdrtool. Time taken: {} seconds.***********'\
          .format((end_time - start_time).total_seconds()))

  return status_code

def CollectFdrDump(host_ip, username, password):
  # Using Python redfish library (https://github.com/DMTF/python-redfish-library)
  REDFISH_OBJ = redfish.redfish_client(base_url=host_ip, username=username, \
                      password=password, default_prefix='/redfish/v1/')
  REDFISH_OBJ.login(auth="basic")
  print('Logged in to host {}'.format(host_ip))
  # Trigger the FDR dump first.
  body = {"DiagnosticDataType":"OEM", "OEMDiagnosticDataType":"DiagnosticType=FDR"}
  response = REDFISH_OBJ.post("/redfish/v1/Systems/HGX_Baseboard_0/LogServices/Dump/Actions/LogService.CollectDiagnosticData/", body=body)
  entry_id = response.dict['Id']
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
    raise Exception("FDR dump request failed! Response received: {}.".format(response))
  # To-do: need to add other cases?
  # Verify the dump status
  task = REDFISH_OBJ.get(response.dict['@odata.id'])
  if task.dict['TaskState'] == 'Completed':
    print('FDR dump is ready to be downloaded!')
  else:
    raise Exception("FDR dump request failed! Task status received: {}.".format(task))
  # Collect dump after TaskState becomes "Completed"
  binary_log_tar_file = f'fdr_dump_{entry_id}.tar.xz'
  response = REDFISH_OBJ.get(f"/redfish/v1/Systems/HGX_Baseboard_0/LogServices/Dump/Entries/{entry_id}/attachment")
  with open(binary_log_tar_file, 'w') as fd:
    fd.write(response.text)
  REDFISH_OBJ.logout()
  return binary_log_tar_file

if __name__ == '__main__':
    status_code = main()
    sys.exit(status_code)