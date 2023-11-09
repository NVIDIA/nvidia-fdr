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
import traceback
from enum import Enum
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
                    "PDT": {"CompClass": "TEXT", "ParamClass": "TEXT", "ParamID": "INTEGER", "ParamName": "TEXT", "ParamType": "TEXT", "Units": "TEXT", "Notes": "TEXT"},\
                    "PVT": {"TimeStamp": "INTEGER", "ParamID": "INTEGER", "ParamValue": "TEXT", "BootId":"INTEGER"},\
                    "PST": {"FromTime": "INTEGER", "ToTime": "INTEGER", "ParamID": "INTEGER",\
                              "NumSamples": "INTEGER", "Min": "INTEGER", "Max": "INTEGER", "Avg": "INTEGER"},\
                    "BookOfErrors": {"BootId":"INTEGER","DeviceType":"TEXT","DeviceInstance":"TEXT","ErrorType":"TEXT","ErrorOccurTimeStamp":"INTEGER","ParamID":"INTEGER"},\
                    "BookKeeper": {"CompactDirectory":"TEXT","compactStatus":"TEXT"}
                    }

FDR_TABLE_TYPE = Enum('FDR_TABLE_TYPE', FDR_TABLE_SCHEMA) # All the keys of FDR_TABLE_SCHEMA dictionary


# Create a list of message types in the protobuf
MessageClasses = [v.DESCRIPTOR.name for v in  vars(fdr_schema).values() if isinstance(v, type) and issubclass(v, Message)]
PROTO_MSG_TYPE = Enum('PROTO_MSG_TYPE', MessageClasses)

'''
Class for holding ALL the messages in form of CatalogEntry
'''
class Catalog:
  def __init__(self, config, log_root_dir):
    self.CatalogEntries = {key: [] for key in MessageClasses}
    self.decode_format = config['decode_format']
    self.length_delimited_binary = config['length_delimited']
    self.json_key_name = config['key_name']
    
    self.log_dir = log_root_dir
    self.CreateParamDescriptions()
    self.CreateCatalog()

    self.catalog_name = self.GetCatalogName()
    from .influx import  InfluxDBConnection
    from .sqlite import  SQLiteConnection
    self.influxClient = InfluxDBConnection(self.catalog_name, url=config['influx_url'], org=config['influx_org'], token=config['influx_token']) if self.decode_format == DECODE_FORMAT.INFLUX else None
    self.sqliteClient = SQLiteConnection(self.catalog_name, config['append']) if self.decode_format == DECODE_FORMAT.SQLITE else None
     
  def FindParamFilename(self):
    param_filename = None
    for root, dirs, files in os.walk(self.log_dir):
      for filename in files:
        if 'ParamDescription.log' in filename:
          param_filename = os.path.join(root, filename)
    return param_filename

  def CreateCatalog(self):
    from datetime import datetime
    start_time = datetime.now()
    for root, dirs, files in os.walk(self.log_dir):
      for filename in files:
        file_extension = os.path.splitext(filename)[1]
        if (file_extension != '.log' and file_extension != '.stats' and file_extension != '.hifilog') or filename.endswith('ParamDescription.log'): # To ignore any non-log and non-stats files (e.g. BirthCertificate.tar)
          continue
        try:
          self.decode_binary_file(os.path.join(root, filename))
        except Exception as e:
          logging.error("Exception occured while decoding file {}: {}".format(os.path.join(root, filename), e))
          #traceback.print_exc()
    end_time = datetime.now()
    print("\nFinished decoding all the binary logs. Time taken: {} seconds".format((end_time - start_time).total_seconds()))
  
  def AddEntry(self, CatalogEntry):
    self.CatalogEntries.get(CatalogEntry.msg_type.name).append(CatalogEntry)

  def decode_binary_file(self, filepath):
    # Read the binary file
    with open(filepath, 'rb') as fd:
      buf = fd.read()
    
    if self.decode_format == DECODE_FORMAT.JSON:
      from .json import JSONCatalogEntry
      entry = JSONCatalogEntry(filepath, self.json_key_name)
    elif self.decode_format == DECODE_FORMAT.INFLUX:  
      from .influx import InfluxDBCatalogEntry
      entry = InfluxDBCatalogEntry(filepath)
    elif self.decode_format == DECODE_FORMAT.SQLITE:
      from .sqlite import SQLiteDBCatalogEntry
      entry = SQLiteDBCatalogEntry(filepath)
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
    brd_serial = None
    for entry in self.CatalogEntries[PROTO_MSG_TYPE.fdr_sample.name]:
      # Looking for Inventory.log under Baseboard
      if entry.compClass == 'Baseboard' and entry.paramClass == 'Inventory':
        brd_serial = entry.GetBrdSerial()
    if brd_serial is None: # Can't do "if not brd_serial" as empty string raises wrong condition
      brd_serial = 'xxx'
      print("Couldn't retrieve Baseboard Serial Number. Setting it to '{}'.".format(brd_serial))
    return brd_serial
    
  def WriteAllEntries(self):
    print("Starting to write the decoded logs....")
    from datetime import datetime
    start_time = datetime.now()
    kwargs = {'influxClient': self.influxClient, 'sqliteClient': self.sqliteClient}
    table_creation_order = ['fdr_params', 'fdr_sample', 'fdr_book_of_errors', 'fdr_compactor_bookkeep', 'fdr_stat']
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
    param_filename = self.FindParamFilename()
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
  def __init__(self, filepath):
    self.messages = []
    self.paramClass, self.compClass, self.compID = CatalogEntry.parse_param_and_component(filepath)
    self.bootid = CatalogEntry.parse_bootid(filepath)

    # Check the message type from file extension
    if filepath.endswith('.stats'):
      self.msg_type = PROTO_MSG_TYPE.fdr_stat
    elif filepath.endswith('ParamDescription.log'):
      self.msg_type = PROTO_MSG_TYPE.fdr_params
    elif filepath.endswith('BookOfErrors.log'):
      self.msg_type = PROTO_MSG_TYPE.fdr_book_of_errors
    elif filepath.endswith('Compactor.log'):
      self.msg_type = PROTO_MSG_TYPE.fdr_compactor_bookkeep
    else: # Both for log and hifilog
      self.msg_type = PROTO_MSG_TYPE.fdr_sample

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
      # At this point, proto_msg is of type protobuf message...
      # For example, either fdr_logs_schema_pb2.fdr_sample and fdr_logs_schema_pb2.fdr_stats
      self.AddMessage(proto_msg)
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
      # At this point, proto_msg is of type protobuf message...
      # For example, either fdr_logs_schema_pb2.fdr_sample and fdr_logs_schema_pb2.fdr_stats
      self.AddMessage(proto_msg)
    return

  def GetBrdSerial(self):
    brd_serial = None
    paramID = self.GetParamIdFromName('BRD-SERIAL')
    for message in self.messages:
      brd_serial = self.GetParamValue(message, paramId=paramID, paramName='BRD-SERIAL')
      if brd_serial is not None: # Can't do "if not brd_serial" as empty string raises wrong condition
        break
    return brd_serial
  
  def FindTablename(self):
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
    else:
      tablename = 'Unknown'
    return tablename
  
  # If compClass and paramClass are not provided, this method will use the predefined values
  # which were parsed from the filepath
  def GetParamNameFromID(self, paramID, compClass=None, paramClass=None):
    compClass = compClass if compClass else self.compClass
    paramClass = paramClass if paramClass else self.paramClass
    result = ParamDescription.get(compClass, {}).get(paramClass, {}).get(str(paramID), {}).get("ParamName")
    if not result:
      print(f"WARNING: {compClass}.{paramClass}.{paramID} not found in PDT.")
    return result
  
  def GetParamNameFromMsg(self, proto_msg):
    match self.msg_type:
      case PROTO_MSG_TYPE.fdr_sample | PROTO_MSG_TYPE.fdr_stat:
        return self.GetParamNameFromID(proto_msg.ParamID)
      case PROTO_MSG_TYPE.fdr_book_of_errors:
        return self.GetParamNameFromID(proto_msg.ParamID, proto_msg.CompClass, proto_msg.ParamClass)
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
  def parse_param_and_component(filepath):
    # Parse filepath to get ParamClass and Component
    filepath, ParamClass = os.path.split(filepath)

    ParamClass = ParamClass.replace('.log', '') # e.g.: Config.log becomes Config
    ParamClass = ParamClass.replace('.stats', '') # e.g.: Config.stats becomes Config
    ParamClass = ParamClass.replace('.hifilog', '') # e.g.: Sensor.Thermal.hifilog becomes Sensor.Thermal
    
    ParamClass = ParamClass.replace('.', '_') # e.g.: Sensor.Clock becomes Sensor_Clock
    filepath, Component = os.path.split(filepath)

    import re
    if re.search(r'(\d+)', Component):
      CompClass, CompID = filter(None, re.split(r'(\d+)', Component))
    else:
      CompClass, CompID = Component, None
    
    # One directory above is the CompClass (e.g. in /PCIeRetimer/Retimer0/, CompClass is PCIeRetimer)
    CompClass = os.path.split(filepath)[1]
    return ParamClass, CompClass, CompID
  
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