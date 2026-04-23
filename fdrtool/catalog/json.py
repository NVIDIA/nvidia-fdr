#! /usr/bin/env python3

'''
Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
'''
# Import standard library modules
import json
import re
import os
# Import third-party library modules
import google.protobuf.json_format as protobuf_json_format
from tqdm import tqdm
# Import locally developed modules
from catalog.catalog import CatalogEntry, PROTO_MSG_TYPE

'''
Child class for holding a single data entry/message in JSON format
'''
class JSONCatalogEntry(CatalogEntry):
  def __init__(self, filepath, key_name=False, ParamIDClassDict = None, ParamIDNameDict= None):
    super().__init__(filepath, ParamIDClassDict, ParamIDNameDict)
    self.filepath = filepath
    self.primary_key_name = key_name
    

  def AddMessage(self, proto_msg, is_event_type = False):
    try:
      proto_msg_str = json.loads(protobuf_json_format.MessageToJson(proto_msg, always_print_fields_with_no_presence=True))
    except TypeError:
      # older protobuf versions use including_default_value_fields
      proto_msg_str = json.loads(protobuf_json_format.MessageToJson(proto_msg, including_default_value_fields=True))
    
    if (not is_event_type) and self.primary_key_name and self.msg_type != PROTO_MSG_TYPE.fdr_params \
      and self.msg_type != PROTO_MSG_TYPE.fdr_compactor_bookkeep and \
        self.msg_type != PROTO_MSG_TYPE.fdr_event_details and \
        self.msg_type != PROTO_MSG_TYPE.fdr_boot_event  :
      # Replace the ParamID with ParamName
      if proto_msg_str["ParamID"] != "9999":

        # Extracting The CompClass form the Filename 
        parts = self.filepath.split('/')
        CompClass = None
        if len(parts) > 4:
            filename = parts[-1].split('.')
            CompClass= filename[0]

        paramName = self.GetParamNameFromID(proto_msg,CompClass)
        paramClass = self.GetParamClassFromName(paramName)
        
        #Appending the 'ParamName' to the JSON before writing it into the log files.
        if paramName:
          append_to_json = {"ParamName": paramName}
          proto_msg_str.update(append_to_json)

        else:
          #Exception for the files if not present in self.filepath (./fdr_logs/fdr/BootCount~)
          warning_file="warning.txt"
          with open(warning_file, "a") as file:
              file.write(str("Skipping the -kn update for file {self.filepath} Not it self.filepath") + "\n")
              
        #Appending the 'paramClass' to the JSON before writing it into the log files.
        if paramClass:
          append_to_json = {"ParamClass": paramClass}
          proto_msg_str.update(append_to_json)

        else:
          #Exception for the files if not present in self.filepath (./fdr_logs/fdr/BootCount~)
          warning_file="warning.txt"
          with open(warning_file, "a") as file:
              file.write(str("Skipping the -kn update for file {self.filepath} Not it self.filepath") + "\n")
        
        #Appending the 'CompClass' to the JSON before writing it into the log files.
        if CompClass:
          append_to_json = {"CompClass":CompClass}
          proto_msg_str.update(append_to_json)
          #del proto_msg_str["ParamID"]
        else:
          #Exception for the files if not present in self.filepath (./fdr_logs/fdr/BootCount~)
          warning_file="warning.txt"
          with open(warning_file, "a") as file:
              file.write(str("Skipping the -kn update for file {self.filepath} Not it self.filepath") + "\n")
          #print(f"Warning : Skipping the ParamName update for file {self.filepath} Not it self.filepath")
    

    proto_msg_str = {key: str(value) if not isinstance(value, str) else value for key, value in proto_msg_str.items()}


    # MessageToJson method converts the protobuf message into JSON format. However,
    # to make JSON logs consistent with the formatting in FDR, we're removing the '\n' between the key-values,
    # as well as all the spaces by doing a load and then dump.
    json_str = json.dumps(proto_msg_str)
    json_str += '\n' # Add a new line to separate between messages
    self.messages.append(json_str)
    

    data = json.loads(json_str)
    if data.get("ParamName") == "BRD-SERIAL":
        with open("brd_serial", 'w') as file:
            serial_number= data.get("ParamValueString")
            if serial_number: 
              file.write(serial_number)

    return


  def GetParamValue(self, message, paramId=None, paramName=None):
    paramValue = None
    json_object = json.loads(message)  
    if (paramId is not None and json_object.get('ParamID') == paramId) or (paramName is not None and json_object.get('ParamName') == paramName):
      for key in json_object.keys():
        if 'ParamValue' in key:
          paramValue = json_object.get(key)
          break
    return paramValue
  
  def GetMessageDict(self, message):
    return json.loads(message)
  
  def WriteEntry(self, **kwargs):
    with open(self.filepath, 'w') as fd:
      fd.writelines(self.messages)
  
  def __repr__(self): 
    return "Logs for {}:\n{}\n".format(self.filepath, self.messages)

  def WriteAllEntries(self):
    total_entries = len(self.entries)
    for i, entry in enumerate(tqdm(self.entries, desc="Writing JSON files", ncols=100)):
        entry.WriteEntry()

