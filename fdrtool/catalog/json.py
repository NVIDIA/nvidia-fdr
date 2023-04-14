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
import json
# Import third-party library modules
import google.protobuf.json_format as protobuf_json_format
# Import locally developed modules
from catalog.catalog import CatalogEntry

'''
Child class for holding a single data entry/message in JSON format
'''
class JSONCatalogEntry(CatalogEntry):
  def __init__(self, filepath):
    super().__init__(filepath)
    self.filepath = filepath

  def AddMessage(self, proto_msg):
    # MessageToJson method converts the protobuf message into JSON format. However,
    # to make JSON logs consistent with the formatting in FDR, we're removing the '\n' between the key-values,
    # as well as all the spaces.
    json_str = str(protobuf_json_format.MessageToJson(proto_msg)).replace('\n', '').replace(' ', '') 
    json_str += '\n' # Add a new line to separate between messages
    self.messages.append(json_str)

  def GetParamValue(self, message, paramName):
    paramValue = None
    json_object = json.loads(message)
    if json_object.get('ParamName') == paramName:
      for key in json_object.keys():
        if 'ParamValue' in key:
          paramValue = json_object.get(key)
          break
    return paramValue
  
  def WriteEntry(self, **kwargs):
    with open(self.filepath, 'w') as fd:
      fd.writelines(self.messages)

  def __repr__(self): 
    return "Logs for {}:\n{}\n".format(self.filepath, self.messages)

