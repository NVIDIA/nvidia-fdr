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
from catalog.catalog import CatalogEntry, PROTO_MSG_TYPE

'''
Child class for holding a single data entry/message in JSON format
'''
class JSONCatalogEntry(CatalogEntry):
  def __init__(self, filepath, key_name=False):
    super().__init__(filepath)
    self.filepath = filepath
    self.primary_key_name = key_name

  def AddMessage(self, proto_msg):
    proto_msg_str = json.loads(protobuf_json_format.MessageToJson(proto_msg))
    if self.primary_key_name and self.msg_type != PROTO_MSG_TYPE.fdr_params and self.msg_type != PROTO_MSG_TYPE.fdr_compactor_bookkeep:
      # Replace the ParamID with ParamName
      paramName = self.GetParamNameFromMsg(proto_msg)
      if paramName:
        append_to_json = {"ParamName": paramName}
        proto_msg_str.update(append_to_json)
        del proto_msg_str["ParamID"]
      else:
        print(f"NOTE: Skipping the ParamName update in {self.filepath} as it couldn't be retrieved.")
      
    # MessageToJson method converts the protobuf message into JSON format. However,
    # to make JSON logs consistent with the formatting in FDR, we're removing the '\n' between the key-values,
    # as well as all the spaces by doing a load and then dump.
    json_str = json.dumps(proto_msg_str)
    json_str += '\n' # Add a new line to separate between messages
    self.messages.append(json_str)

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

