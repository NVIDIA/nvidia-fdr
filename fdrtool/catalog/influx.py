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
import logging
# Import third-party library modules
import influxdb_client
from influxdb_client.client.exceptions import InfluxDBError
from influxdb_client.domain.write_precision import WritePrecision
import google.protobuf.json_format as protobuf_json_format
# Import locally developed modules
import fdr_logs_schema_pb2 as fdr_schema
from catalog.catalog import CatalogEntry, PROTO_MSG_TYPE

'''
Class responsible for InfluxDB connection
'''
class InfluxDBConnection:
  def __init__(self, catalog_name, url, org, token):
    self.callback = InfluxBatchingCallback()
    # Connect to InfluxDB and create a database if not exists
    self.database_name = 'telemetry_db_hgx_serial_num_' + catalog_name
    print('InfluxDB bucket name: {}'.format(self.database_name))
    
    self.org = org

    client = influxdb_client.InfluxDBClient(
      url=url, token=token, org=org)
  
    buckets_api = client.buckets_api()
    # Create the bucket if it doesn't exist already
    if buckets_api.find_bucket_by_name(self.database_name) is None:
        created_bucket = buckets_api.create_bucket(
            bucket_name=self.database_name, retention_rules=None, org=org)

    # Warning from InfluxDB:
    # The WriteApi in batching mode (default mode) is suppose to run as a singleton.
    # To flush all your data you should wrap the execution using with client.write_api(...) 
    # as write_api: statement or call write_api.close() at the end of your script.
    self.write_api = client.write_api(success_callback=self.callback.success,\
                                      error_callback=self.callback.error,\
                                      retry_callback=self.callback.retry)

  def WritePoint(self, point):
    # if isinstance(point, influxdb_client.client.write.point.Point):
    #   return
    self.write_api.write(bucket=self.database_name, org=self.org, record=point)

  def Close(self):
    self.write_api.close()

class InfluxBatchingCallback(object):
    def success(self, conf: (str, str, str), data: str):
        """Successfully writen batch."""
        # print(f"Written batch: {conf}, data: {data}")

    def error(self, conf: (str, str, str), data: str, exception: InfluxDBError):
        """Unsuccessfully writen batch."""
        logging.error(f"Cannot write batch: {conf}, data: {data} due: {exception}")

    def retry(self, conf: (str, str, str), data: str, exception: InfluxDBError):
        """Retryable error."""
        print(f"Retryable error occurs for batch: {conf}, data: {data} retry: {exception}")

'''
Child class for holding a single data entry/message in line-protocol format for InfluxDB
'''
class InfluxDBCatalogEntry(CatalogEntry):
  def __init__(self, filepath):
    super().__init__(filepath)
    self.tablename = self.FindTablename()

  def AddMessage(self, proto_msg):
    match self.msg_type:
      case PROTO_MSG_TYPE.fdr_sample:
        #values = {'ParamValue': None} # Need to do it as sometimes the 'ParamValue' is missing in the logs
        field_key = self.GetParamNameFromID(proto_msg.ParamID)
        if field_key is None:
          logging.error(f'Skipping ParamID {proto_msg.ParamID} in {self.tablename} as parameter name is undefined.')
          return
        values = CatalogEntry.get_message_values(proto_msg, ['ParamValue'])
        point_data = influxdb_client.Point(self.tablename).tag('BootId', self.bootid).field(field_key, values['ParamValue']).time(proto_msg.TimeStamp, write_precision=WritePrecision.S)
        self.messages.append(point_data)
      
      case PROTO_MSG_TYPE.fdr_stat:
        # Temporary change: Using 0 as the protobuf didn't serialize default values. Need to figure out a solution.
        #values = {'Min': 0, 'Max': 0, 'Avg': 0} # Should be None by default. The values won't show up in database
        field_key = self.GetParamNameFromID(proto_msg.ParamID)
        if field_key is None:
          logging.error(f'Skipping {proto_msg.ParamID} in {self.tablename} as parameter name is undefined.')
          return
        values = CatalogEntry.get_message_values(proto_msg, ['FromTime','ToTime', 'Min', 'Max', 'Avg'])
        point_data = influxdb_client.Point(self.tablename)\
                                    .tag('agg-type', 'Min')\
                                    .field(field_key, values['Min'])\
                                    .time(values['ToTime'], write_precision=WritePrecision.S)
        self.messages.append(point_data)

        point_data = influxdb_client.Point(self.tablename)\
                                    .tag('agg-type', 'Max')\
                                    .field(field_key, values['Max'])\
                                    .time(values['ToTime'], write_precision=WritePrecision.S)
        self.messages.append(point_data)

        point_data = influxdb_client.Point(self.tablename)\
                                    .tag('agg-type', 'Avg')\
                                    .field(field_key, values['Avg'])\
                                    .time(values['ToTime'], write_precision=WritePrecision.S)
        self.messages.append(point_data)

        point_data = influxdb_client.Point(self.tablename)\
                                    .tag('agg-type', 'FromTime')\
                                    .field(field_key, values['FromTime'])\
                                    .time(values['ToTime'], write_precision=WritePrecision.S)
        self.messages.append(point_data)
      
      case PROTO_MSG_TYPE.fdr_params:
        # Won't be pushed to InfluxDB, so save it in proto format
        self.messages.append(proto_msg)
        
      case PROTO_MSG_TYPE.fdr_book_of_errors:
        pass
        
      case PROTO_MSG_TYPE.fdr_compactor_bookkeep:
        pass
      
      case _:
        print("Proto msg format didn't match")

  # This method finds the param value given either the paramId or paramName.
  # If both are given, paramName gets precedence
  def GetParamValue(self, message, paramId=None, paramName=None):
    paramValue = None
    if paramName is None:
      paramName = self.GetParamNameFromID(paramId)
    # Here message is a InfluxDB Point object.
    if paramName in message._fields.keys():
      paramValue = message._fields.get(paramName)
    return paramValue
  
  def GetMessageDict(self, message):
    if self.msg_type == PROTO_MSG_TYPE.fdr_params:
      return protobuf_json_format.MessageToDict(message)
    return message._fields
  
  def WriteEntry(self, **kwargs):
    influxClient = kwargs.get('influxClient')
    if influxClient:
      for point in self.messages:
        influxClient.WritePoint(point) 
    else:
      print("Missing influxClient")

  def __repr__(self): 
    return "Logs for {}:\n{}\n".format(self.tablename, self.messages)
