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
LOG_DIRECTORY = './fdr_logs'

FDR_TABLE_SCHEMA = {
                    "CDT": {"CompClass": "TEXT", "CompID": "INTEGER", "CompLabel": "TEXT"},\
                    "PDT": {"CompClass": "TEXT", "ParamClass": "TEXT", "ParamID": "INTEGER", "ParamName": "TEXT", "ParamType": "TEXT", "Units": "TEXT", "Notes": "TEXT"},\
                    "PVT": {"TimeStamp": "INTEGER", "ParamID": "INTEGER", "ParamValue": "TEXT"},\
                    "STATS": {"FromTime": "INTEGER", "ToTime": "INTEGER", "ParamID": "INTEGER",\
                              "NumSamples": "INTEGER", "Min": "INTEGER", "Max": "INTEGER", "Avg": "INTEGER"}
                    }

FDR_TABLE_TYPE = Enum('FDR_TABLE_TYPE', FDR_TABLE_SCHEMA) # All the keys of FDR_TABLE_SCHEMA dictionary


# Create a list of message types in the protobuf
MessageClasses = [v.DESCRIPTOR.name for v in  vars(fdr_schema).values() if isinstance(v, type) and issubclass(v, Message)]
PROTO_MSG_TYPE = Enum('PROTO_MSG_TYPE', MessageClasses)

'''
Class for holding ALL the messages in form of CatalogEntry
'''
class Catalog:
  def __init__(self, config):
    self.CatalogEntries = {key: [] for key in MessageClasses}
    self.decode_format = config['decode_format']
    self.length_delimited_binary = config['length_delimited']
    self.json_key_name = config['key_name']
  
    self.CreateParamDescriptions()
    self.CreateCatalog()

    self.catalog_name = self.GetCatalogName()
    from .influx import  InfluxDBConnection
    from .sqlite import  SQLiteConnection
    self.influxClient = InfluxDBConnection(self.catalog_name, url=config['influx_url'], org=config['influx_org'], token=config['influx_token']) if self.decode_format == DECODE_FORMAT.INFLUX else None
    self.sqliteClient = SQLiteConnection(self.catalog_name, config['append']) if self.decode_format == DECODE_FORMAT.SQLITE else None
    
  def CreateCatalog(self):
    from datetime import datetime
    start_time = datetime.now()
    for root, dirs, files in os.walk(LOG_DIRECTORY):
      for filename in files:
        file_extension = os.path.splitext(filename)[1]
        if (file_extension != '.log' and file_extension != '.stats') or filename.endswith('ParamDescription.log'): # To ignore any non-log and non-stats files (e.g. BirthCertificate.tar)
          continue
        try:
          self.decode_binary_file(os.path.join(root, filename))
        except Exception as e:
          logging.error("Exception occure while decoding file {}: {}".format(os.path.join(root, filename), e))
          traceback.print_exc()
    end_time = datetime.now()
    print("Finished decoding all the binary logs. Time taken: {} seconds".format((end_time - start_time).total_seconds()))
  
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
    try:
      if self.length_delimited_binary:
        entry.decode_length_delimited_binary(buf)
      else: # default
        entry.decode_zero_delimited_binary(buf)
      self.AddEntry(entry)
    except Exception as e:
      logging.error("Exception occured while decoding file {}: {}".format(filepath, e))
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
    from datetime import datetime
    start_time = datetime.now()
    kwargs = {'influxClient': self.influxClient, 'sqliteClient': self.sqliteClient}
    table_creation_order = ['fdr_params', 'fdr_sample', 'fdr_stat']
    for entry_type in table_creation_order:
      for entry in self.CatalogEntries[entry_type]:
        try:
          entry.WriteEntry(**kwargs)
        except Exception as e:
          logging.error("Incomplete write. Error: {}\n{}".format(e, entry))
          traceback.print_exc()
    end_time = datetime.now()
    print("Finished writing all the decoded logs. Time taken: {} seconds".format((end_time - start_time).total_seconds()))
    
    # Create the views for SQLITE
    if self.sqliteClient:
      from .sqlite import CreateCombinedViews
      CreateCombinedViews(self.sqliteClient)
      
  def CreateParamDescriptions(self):
    param_filename = os.path.join(LOG_DIRECTORY, "Schema/ParamDescription.log")
    self.decode_binary_file(param_filename)
    
    global ParamDescription
    ParamDescription = {}
    for entry in self.CatalogEntries[PROTO_MSG_TYPE.fdr_params.name]:
      for message in entry.messages:
        message_dict = entry.GetMessageDict(message)
        message_CompClass = message_dict.get("CompClass")
        message_ParamClass = message_dict.get("ParamClass")
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

    # Check the message type from file extension
    if filepath.endswith('.stats'):
      self.msg_type = PROTO_MSG_TYPE.fdr_stat
    elif filepath.endswith('ParamDescription.log'):
      self.msg_type = PROTO_MSG_TYPE.fdr_params
    else:
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
    for message in self.messages:
      brd_serial = self.GetParamValue(message, 'BRD-SERIAL')
      if brd_serial is not None: # Can't do "if not brd_serial" as empty string raises wrong condition
        break
    return brd_serial
  
  def FindTablename(self):
    if self.msg_type == PROTO_MSG_TYPE.fdr_stat or self.msg_type == PROTO_MSG_TYPE.fdr_sample:
      tablename_prefix = 'STATS' if self.msg_type == PROTO_MSG_TYPE.fdr_stat else 'PVT'
      tablename_suffix = '_{}'.format(self.compID) if self.compID else ''
      tablename = '{}_{}_{}{}'.format(tablename_prefix, self.paramClass, self.compClass, tablename_suffix)
    elif self.msg_type == PROTO_MSG_TYPE.fdr_params:
      tablename = 'PDT'
    else:
      tablename = 'Unknown'
    return tablename
  
  def GetParamName(self, paramID):
    result = ParamDescription.get(self.compClass, {}).get(self.paramClass, {}).get(str(paramID), {}).get("ParamName")
    if not result:
      print(f"WARNING: {self.compClass}.{self.paramClass}.{paramID} not found in PDT.")
    return result

  @staticmethod
  def parse_param_and_component(filepath):
    # Parse filepath to get ParamClass and Component
    filepath, ParamClass = os.path.split(filepath)

    ParamClass = ParamClass.replace('.log', '') # e.g.: Config.log becomes Config
    ParamClass = ParamClass.replace('.stats', '') # e.g.: Config.stats becomes Config
    
    ParamClass = ParamClass.replace('.', '_') # e.g.: Sensor.Clock becomes Sensor_Clock
    filepath, Component = os.path.split(filepath)

    import re
    if re.search(r'(\d+)', Component):
      CompClass, CompID = filter(None, re.split(r'(\d+)', Component))
    else:
      CompClass, CompID = Component, None
    return ParamClass, CompClass, CompID

  @staticmethod
  def get_message_values(proto_msg, values_names):
    values = {}
    all_fields = proto_msg.ListFields()
    for fields in all_fields:
      key = 'ParamValue' if 'ParamValue' in fields[0].name else fields[0].name
      values[key] = fields[1]
      if 'ParamName' in key:
        values[key] = values[key].replace('[', '_').replace(']', '') # in case there is a subcomponent ID
    # Fill up the missing fields with None
    for field_name in values_names:
      values.setdefault(field_name)
    return values
  
  def __repr__(self): 
    return "Logs:\n{}\n".format(self.messages)