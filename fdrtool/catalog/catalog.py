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
import os
import logging
import json
import google.protobuf.json_format as protobuf_json_format

logging.basicConfig(level=logging.INFO)
import traceback
from enum import Enum
from exception import FileNotFound
import copy
# Import third-party library modules
from google.protobuf.internal.decoder import _DecodeVarint32
from google.protobuf.message import Message
from cobs import cobsr
# Import locally developed modules
import fdr_logs_schema_pb2 as fdr_schema

DECODE_FORMAT = Enum('DECODE_FORMAT', ['JSON', 'INFLUX', 'SQLITE'])

FDR_TABLE_SCHEMA = {
                    "CDT": {"CompClass": "TEXT", "CompID": "INTEGER", "CompLabel": "TEXT"},\
                    "PDT": {"CompClass": "TEXT", "ParamClass": "TEXT",  "ParamName": "TEXT", "ParamID": "INTEGER","ParamType": "TEXT", "ParamUnit": "TEXT", "ParamNotes": "TEXT"},\
                    "PVT": {"TimeStamp": "INTEGER", "ParamID": "INTEGER", "ParamValue": "TEXT", "BootId":"INTEGER"},\
                    "PST": {"FromTime": "INTEGER", "ToTime": "INTEGER", "ParamID": "INTEGER",\
                              "NumSamples": "INTEGER", "Min": "INTEGER", "Max": "INTEGER", "Avg": "INTEGER"},\
                    "BookOfErrors": {"BootId":"INTEGER","DeviceType":"TEXT","DeviceInstance":"TEXT","ErrorType":"TEXT","ErrorOccurTimeStamp":"INTEGER","ParamID":"INTEGER"},\
                    "BookKeeper": {"CompactDirectory":"TEXT","compactStatus":"TEXT"},\
                    "BootEvent" : {"EventTimeStamp":"INTEGER", "HMCBootCount":"TEXT", "UpTimeOfHMC":"TEXT" , "CurrentDate":"TEXT", "DataDirFormatVersion":"INTEGER"},
                    "EventDetails" : {"EventTimeStamp":"INTEGER", "EventName":"TEXT", "EventDeviceName":"TEXT" , "EventMessage":"TEXT", "EventOriginOfCondition":"TEXT", "EventAdditionalInfo":"TEXT"}
                    }

FDR_TABLE_TYPE = Enum('FDR_TABLE_TYPE', FDR_TABLE_SCHEMA) # All the keys of FDR_TABLE_SCHEMA dictionary


# Create a list of message types in the protobuf
MessageClasses = [v.DESCRIPTOR.name for v in  vars(fdr_schema).values() if isinstance(v, type) and issubclass(v, Message)]
PROTO_MSG_TYPE = Enum('PROTO_MSG_TYPE', MessageClasses)

param_description_filename = 'ParamDescription.dat'
sensor_file_extenstion = 'stat.dat'
error_book_filename = 'BookOfErrors.dat'
compactor_filename = 'Compactor.dat'
Boot_event_filename = 'BootEvent.dat'
other_file_extention = 'others.dat'
event_file_contains = '.Event_'

'''
Class for holding ALL the messages in form of CatalogEntry
'''
class Catalog:
  def __init__(self, config, log_root_dir):
    self.CatalogEntries = {key: [] for key in MessageClasses}

    self.decode_format = config['decode_format']
    self.length_delimited_binary = config['length_delimited']
    self.json_key_name = config['key_name']
    self.ParamIDClassDict = dict()
    self.ParamIDNameDict = dict()
    self.log_dir = log_root_dir
    self.DecodeErrorEvents()
    self.CreateParamDescriptions()
    self.ParamIDClassDict[9999] = "Error and Fault"
    self.catalog_name = self.GetCatalogName()
    self.CreateCatalog()   
 
    from .influx import  InfluxDBConnection
    from .sqlite import  SQLiteConnection
    self.influxClient = InfluxDBConnection(self.catalog_name, url=config['influx_url'], org=config['influx_org'], token=config['influx_token']) if self.decode_format == DECODE_FORMAT.INFLUX else None
    self.sqliteClient = SQLiteConnection(self.catalog_name, config['append']) if self.decode_format == DECODE_FORMAT.SQLITE else None
     
  def DecodeErrorEvents(self):
    param_filename = self.FindParamFilename(Boot_event_filename)

    if param_filename is None:
      raise FileNotFound("%s file is not found in the dump. " % Boot_event_filename)

    self.decode_binary_file(param_filename)

    for entry in self.CatalogEntries[PROTO_MSG_TYPE.fdr_boot_event.name]:
      for message in entry.messages:
        message_dict = entry.GetMessageDict(message)
        version_number = message_dict.get('DataDirFormatVersion')
        if version_number != 2:
          raise VersionMisMatch("Version of the fdr-dump is not matching with the FDRTool.")
        else:
          logging.info("FDRTool using the %s version for decoding the FDR dump" % version_number)
        return

  def FindParamFilename(self, file_name):
    param_filename = None
    for root, dirs, files in os.walk(self.log_dir):
      for file in files:
        if file_name in file:
          param_filename = os.path.join(root, file)
    return param_filename

  def CreateCatalog(self):
    from datetime import datetime
    start_time = datetime.now()
    for root, dirs, files in os.walk(self.log_dir):
      for filename in files:
        file_extension = os.path.splitext(filename)[1]
        if (file_extension != '.dat') or filename.endswith(param_description_filename) or filename.endswith(Boot_event_filename): # To ignore any non-log and non-stats files (e.g. BirthCertificate.tar)
          continue
        try:
          self.decode_binary_file(os.path.join(root, filename))
        except Exception as e:
          logging.error("Exception occured while decoding file {}: {}".format(os.path.join(root, filename), e))
          #traceback.print_exc()
    end_time = datetime.now()
    print("\nFinished decoding all the binary logs. Time taken: {} seconds".format((end_time - start_time).total_seconds()))
    print("creating is done")
  
  def AddEntry(self, CatalogEntry):
    self.CatalogEntries.get(CatalogEntry.msg_type.name).append(CatalogEntry)

  def decode_binary_file(self, filepath):
    # Read the binary file
    logging.debug('Decoding the file %s' % filepath)

    with open(filepath, 'rb') as fd:
      buf = fd.read()
    
    if self.decode_format == DECODE_FORMAT.JSON:
      from .json import JSONCatalogEntry
      entry = JSONCatalogEntry(filepath, self.json_key_name, self.ParamIDClassDict, self.ParamIDNameDict)
    elif self.decode_format == DECODE_FORMAT.INFLUX:  
      from .influx import InfluxDBCatalogEntry
      entry = InfluxDBCatalogEntry(filepath, self.ParamIDClassDict, self.ParamIDNameDict)
    elif self.decode_format == DECODE_FORMAT.SQLITE:
      from .sqlite import SQLiteDBCatalogEntry
      entry = SQLiteDBCatalogEntry(filepath, self.ParamIDClassDict, self.ParamIDNameDict)
    else:
      raise RuntimeError("Decoding is not implemented for {}.".format(self.decode_format))

    # Convert the binary log into json log.
    # Note that any exception thrown here will be caught by the caller (CreateCatalog method)
    if self.length_delimited_binary:
      entry.decode_length_delimited_binary(buf)
    else: # default
      entry.decode_zero_delimited_binary(buf)
    self.AddEntry(entry)
    return


  def GetCatalogName(self):
    Param_ID = self.GetSerialNumber()
    if(Param_ID == None):
      print("Param ID of the BaseBoard Serial Number is None; considering xxx as a Baseboard serial number")

    for root, dirs, files in os.walk(self.log_dir):
      for filename in files:
        if filename.startswith('Baseboard.'):
          file_extension = ('.').join(filename.split('.')[-2:])
          if (file_extension == 'others.dat'):
            self.decode_binary_file(os.path.join(root, filename))

    for entry in self.CatalogEntries[PROTO_MSG_TYPE.fdr_sample.name]:
      for message in entry.messages:
        message_dict = entry.GetMessageDict(message)
        message_ParamID = message_dict.get("ParamID")
        if(message_ParamID == Param_ID) :
          if "ParamValueString" in message_dict.keys():
            return message_dict.get("ParamValueString")
          elif "ParamValue" in message_dict.keys():
            return message_dict.get("ParamValue")
    return "xyz"

  def GetSerialNumber(self):
    for item in ParamDescription['Baseboard']['Inventory']:
        if(ParamDescription['Baseboard']['Inventory'][item]['ParamName'] == 'BRD-SERIAL') :
          return item
    return None

  def WriteAllEntries(self):
    print("Starting to write the decoded logs....")
    from datetime import datetime
    start_time = datetime.now()
    kwargs = {'influxClient': self.influxClient, 'sqliteClient': self.sqliteClient}
    table_creation_order = ['fdr_params', 'fdr_sample', 'fdr_book_of_errors', 'fdr_compactor_bookkeep', 'fdr_stat', 'fdr_boot_event', 'fdr_event_details']
    for entry_type in table_creation_order:
      print(f"Writing {entry_type} table(s)....", end = " ")
      success = 0
      fail = 0
      for entry in self.CatalogEntries[entry_type]:
        try:
          entry.WriteEntry(**kwargs)
          if len(entry.messages): # it shows if there was any entry for this table/file
            success += 1
        except Exception as e:
          logging.error("Incomplete write. Error: {}\n{}".format(e, entry))
          #traceback.print_exc()
          fail += 1
      print(f"Successful writes: {success}. Failed writes: {fail}....", end = " ")
      result_str = "Incomplete!" if fail else "Complete!"
      print(result_str)
    
    # Create the views for SQLITE.
    if self.sqliteClient:
      from .sqlite import CreateCombinedViews
      CreateCombinedViews(self.sqliteClient)
    
    end_time = datetime.now()
    print("\nFinished writing all the decoded logs. Time taken: {} seconds".format((end_time - start_time).total_seconds()))
      
  def CreateParamDescriptions(self):
    # get the path of the param description file
    
    param_filename = self.FindParamFilename(param_description_filename)

    if param_filename is None:
      raise FileNotFound("%s file is not found in the dump. " % param_description_filename)

    self.decode_binary_file(param_filename)
    
    global ParamDescription
    ParamDescription = {}
    for entry in self.CatalogEntries[PROTO_MSG_TYPE.fdr_params.name]:
      for message in entry.messages:
        message_dict = entry.GetMessageDict(message)
        message_CompClass = message_dict.get("CompClass")
        message_ParamClass = message_dict.get("ParamClass").replace('.', '_')
        message_ParamID = message_dict.get("ParamID")
        if not message_CompClass or not message_ParamClass or message_ParamID is None: # Can't do "if not message_ParamID" as 0 raises wrong condition
          raise Exception(f"Parameter Descriptions log is not complete. CompClass: {message_CompClass}, ParamClass: {message_ParamClass}, ParamID: {message_ParamID}")
        if not ParamDescription.get(message_CompClass):
          ParamDescription[message_CompClass] = {}
        if not ParamDescription[message_CompClass].get(message_ParamClass):
          ParamDescription[message_CompClass][message_ParamClass] = {}
        self.ParamIDClassDict[message_ParamID] = message_ParamClass
        self.ParamIDNameDict[message_ParamID] = message_dict.get('ParamName')
        parameter = {}
        for key in ["ParamName", "DataType", "Units", "Notes"]:
          parameter[key] = message_dict.get(key)
        ParamDescription[message_CompClass][message_ParamClass][message_ParamID] = parameter

  def Close(self):
    if self.influxClient:
      self.influxClient.Close()
    if self.sqliteClient:
      self.sqliteClient.Close()

'''
Parent class for holding a single data entry/message
'''
class CatalogEntry:
  def __init__(self, filepath, ParamIDClassDict = None, ParamIDNameDict= None):
    self.messages = []
    self.ParamIDClassDict = ParamIDClassDict
    self.ParamIDNameDict = ParamIDNameDict
    file_name = os.path.split(filepath)[1]
    self.compClass = file_name.split('.')[0]
    self.compID = file_name.split('.')[1].split('_')[-1]
    self.bootid = CatalogEntry.parse_bootid(filepath)
    
    self.is_other_file = False
    if ".others.dat" in filepath:
      self.is_other_file = True

    # Check the message type from file extension
    if filepath.endswith(sensor_file_extenstion):
      self.msg_type = PROTO_MSG_TYPE.fdr_stat
    elif filepath.endswith(param_description_filename):
      self.msg_type = PROTO_MSG_TYPE.fdr_params
    elif filepath.endswith(error_book_filename):
      self.msg_type = PROTO_MSG_TYPE.fdr_book_of_errors
    elif filepath.endswith(compactor_filename):
      self.msg_type = PROTO_MSG_TYPE.fdr_compactor_bookkeep
    elif filepath.endswith(Boot_event_filename):
      self.msg_type = PROTO_MSG_TYPE.fdr_boot_event
    elif event_file_contains in filepath: # Both for log and hifilog
      self.msg_type = PROTO_MSG_TYPE.fdr_event_details
    else: # Both for log and hifilog
      self.msg_type = PROTO_MSG_TYPE.fdr_sample


  def FindTablename(self):
    tablename = None
    self.paramClass = ""
    if self.msg_type == PROTO_MSG_TYPE.fdr_stat or self.msg_type == PROTO_MSG_TYPE.fdr_sample:
      tablename_prefix = 'PST' if self.msg_type == PROTO_MSG_TYPE.fdr_stat else 'PVT'
      tablename_suffix = '_{}'.format(self.compID) if self.compID else ''
      tablename = '{}_{}_{}{}'.format(tablename_prefix, self.paramClass, self.compClass, tablename_suffix)
    elif self.msg_type == PROTO_MSG_TYPE.fdr_params:
      tablename = 'PDT'
    elif self.msg_type == PROTO_MSG_TYPE.fdr_book_of_errors:
      tablename = 'BookOfErrors'
    elif self.msg_type == PROTO_MSG_TYPE.fdr_compactor_bookkeep:
      tablename = 'BookKeeper'
    elif self.msg_type == PROTO_MSG_TYPE.fdr_boot_event:
      tablename = 'BootEvent'
    elif self.msg_type == PROTO_MSG_TYPE.fdr_event_details:
      tablename = 'EventDetails'
    else:
      tablename = 'Unknown'
    return tablename

  def decode_length_delimited_binary(self, buf):
    # Since each log file can have multiple messages, we need to separate the messages from each other
    # The delimiter used while encoding is the length of each message.
    # So, first we need read the size of each message, which is a varint.
    n = 0
    while n < len(buf):
      msg_len, new_pos = _DecodeVarint32(buf, n)
      n = new_pos
      msg_buf = buf[n:n+msg_len]
      n += msg_len
      proto_msg = getattr(fdr_schema, self.msg_type.name)()

      proto_msg.ParseFromString(msg_buf)
      is_event_type = False
      if self.is_other_file:
        proto_msg_temp = json.loads(protobuf_json_format.MessageToJson(proto_msg))
        if "ParamID" not in proto_msg_temp.keys():
          proto_msg = getattr(fdr_schema, PROTO_MSG_TYPE.fdr_event.name)()
          proto_msg.ParseFromString(msg_buf)
          is_event_type = True

      #print(proto_msg)
      # At this point, proto_msg is of type protobuf message...
      # For example, either fdr_logs_schema_pb2.fdr_sample and fdr_logs_schema_pb2.fdr_stats
      self.AddMessage(proto_msg, is_event_type)
    return
  
  def decode_zero_delimited_binary(self, buf):
    # Since each log file can have multiple messages, we need to separate the messages from each other
    # The delimiter used while encoding is '\0'. For zero delimiters, the FDR uses the COBS-R encoding
    # to remove any 0s in he actual data.
    # So, first we need to find the zero delimiters, then decode using COBS-R, and finally decode to protobuf.
    
    split_buf = filter(None, buf.split(b'\x00'))
    for msg_buf in split_buf:
      msg_buf = cobsr.decode(msg_buf)
      proto_msg = getattr(fdr_schema, self.msg_type.name)()
      proto_msg.ParseFromString(msg_buf)
      is_event_type = False
      if self.is_other_file:
        proto_msg_temp = json.loads(protobuf_json_format.MessageToJson(proto_msg))
        if "ParamID" not in proto_msg_temp.keys():
          proto_msg = getattr(fdr_schema, PROTO_MSG_TYPE.fdr_event.name)()
          proto_msg.ParseFromString(msg_buf)
          is_event_type = True


      # At this point, proto_msg is of type protobuf message...
      # For example, either fdr_logs_schema_pb2.fdr_sample and fdr_logs_schema_pb2.fdr_stats
      self.AddMessage(proto_msg, is_event_type)
    return

  def GetBrdSerial(self):
    brd_serial = None
    paramID = self.GetParamIdFromName('BRD-SERIAL')
    for message in self.messages:
      brd_serial = self.GetParamValue(message, paramId=paramID, paramName='BRD-SERIAL')
      if brd_serial is not None: # Can't do "if not brd_serial" as empty string raises wrong condition
        break
    return brd_serial
  

  # If compClass and paramClass are not provided, this method will use the predefined values
  # which were parsed from the filepath
  def GetParamNameFromID(self, paramID, compClass=None, paramClass=None):
    if paramID in self.ParamIDNameDict.keys():
      return self.ParamIDNameDict[paramID]
    else:
      print(f"WARNING: {paramID} {type(paramID)}not found in PDT.")
      return None
  
  def GetParamNameFromMsg(self, proto_msg):
    match self.msg_type:
      case PROTO_MSG_TYPE.fdr_sample | PROTO_MSG_TYPE.fdr_stat | PROTO_MSG_TYPE.fdr_book_of_errors:
        return self.GetParamNameFromID(str(proto_msg.ParamID))
      case _:
        return None      
  
  def GetParamIdFromName(self, paramName):
    result = None
    all_ids = ParamDescription.get(self.compClass, {}).get(self.paramClass, {})
    for curr_id in all_ids:
      if all_ids.get(curr_id).get("ParamName") == paramName:
        result = curr_id
    if result is None: # paramId can be 0, so "if not result" will raise false condition
      print(f"WARNING: {self.compClass}.{self.paramClass}.{paramName} not found in PDT.")
    return result

  
  @staticmethod
  def parse_bootid(filepath):
    bootid = None
    if 'BootCount' in filepath:
      from pathlib import Path
      for pathpart in Path(filepath).parts:
        if 'BootCount' in pathpart:
          import re
          try:
            fields = re.findall(r'(\d+)', pathpart)
            if len(fields) == 2: 
              bootid, timestamp = fields
            elif len(fields) == 1:
              bootid = 0
              timestamp = fields[0]
          except Exception as e:
            bootid = None
    if 'Bookkeeper' not in filepath and bootid is None and 'fdr.log' not in filepath: # bootid can be 0, so "if not result" will raise false condition
      print(f"WARNING: BootCount cannot be retrieved from filepath {filepath}")
    return bootid

  @staticmethod
  def get_message_values(proto_msg, values_names):
    values = {}
    all_fields = proto_msg.ListFields()
    for fields in all_fields:
      key = 'ParamValue' if 'ParamValue' in fields[0].name else fields[0].name
      values[key] = fields[1]
      if 'ParamName' in key:
        values[key] = values[key].replace('[', '_').replace(']', '') # in case there is a subcomponent ID
      if 'ParamClass' in key:
        values[key] = values[key].replace('.', '_')
    # Fill up the missing fields with None
    for field_name in values_names:
      values.setdefault(field_name)
    return values
  
  def __repr__(self): 
    return "Logs:\n{}\n".format(self.messages)