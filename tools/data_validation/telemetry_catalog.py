#! /usr/bin/env python3

'''
Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
'''

import os
import sys
import csv
import dataclasses
from dataclasses import dataclass
import requests
import json
import copy
import time
from datetime import datetime, timezone
from requests.auth import HTTPBasicAuth
import urllib3
import re
import logging

from prometheus_client import Gauge, Histogram, Info, push_to_gateway

try:
    import influxdb_client
    from influxdb_client import InfluxDBClient, BucketRetentionRules
    from influxdb_client.client.write_api import SYNCHRONOUS
    influx_libs_available=True
except ImportError:
    influx_libs_available=False

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
session = requests.session()
MRDValues = {}

def find_column_index(header_row, column_name):
    for i in range(len(header_row)):
        if column_name in header_row[i]:
            col_chr=chr(i+ord('A'))
            # print(f'{column_name} --> {col_chr} {i}')
            return i
    print(f'Couldnt Locate column {column_name}')
    #exit()
    return 999

def ContentOfColumn(row, col_name):
    for idx in range(len(catalog_header)):
        if col_name in catalog_header[idx]:
            return row[idx] if len(row) > idx else ''
    
    #print(f'Couldnt Locate column {col_name}')
    return ''

def decide_poll_period(paramclass):
    slowness_factor = 30  # 60
    if paramclass in ('Sensor.Thermal', 'Sensor.Power'):
        return 1
    elif paramclass in ('Sensor.Clock', 'Sensor.Other', 'Sensor.Pressure', 'Sensor.Voltage', 'Error'):
        return 10*slowness_factor
    elif paramclass in ('Sensor.Perf'):
        return 10*slowness_factor
    elif paramclass in ('Sensor.Speed'):
        return 10*slowness_factor
    elif paramclass in ('Config', 'Status'):
        return 60*slowness_factor
    elif paramclass in ('Inventory', 'Specs'):
        return 60*60
    elif paramclass in ('Unknown'):
        return 1
    else:
        print(f'Undefined ParamClass {paramclass}')
        exit()

class CatalogEntry:

    TGUID = ''
    COMPCLASS = ''
    PARAMNAME = ''
    DESCRIPTION = ''
    PARAMCLASS = ''
    SMBPBI = ''
    RFAPI = ''
    URI = ''
    FIELD = ''
    BODY = ''
    VALUE = ''
    COMPID = ''         # Device Index i.e. 1-8 for GPU, 0-3 for NVSwitch etc.
    PARAMIDX = ''       # Index for the parameter, applicable for things like NvLink port numbers
    PRIORITY = ''
    AVAILABILITY = ''
    NVML = ''
    NVSMI = ''
    DCGM = ''

    def __init__(self, row):
        global PLATFORM
        self.RAWROW = row
        if self.RAWROW == '':
            self.RESULT = 'SKIPPED'  # Empty Row
            return

        self.RFAPI = ContentOfColumn(row, "OOB API - Wildcards\n(Redfish URI and Field)")
        
        self.PRIORITY = ContentOfColumn(row, "Applicable for \n"+PLATFORM)
        self.AVAILABILITY = ContentOfColumn(row, "Availability")
        self.TGUID = ContentOfColumn(row, "Telemetry GUID")
        self.SMBPBI = ContentOfColumn(row, "SMBPBI")
        self.NVML = ContentOfColumn(row, "NVML")
        self.NVSMI = ContentOfColumn(row, "nvidia-smi")
        self.DCGM = ContentOfColumn(row, "DCGM")
        self.DRVRREQ = ContentOfColumn(row, "OOB path \ndriver dependency")
        self.COMPCLASS = ContentOfColumn(row, "CompClass")
        self.PARAMCLASS = ContentOfColumn(row, "ParamClass")
        self.PARAMNAME = ContentOfColumn(row, "Metric\n(ParamName)")
        # self.POLL_PERIOD = human_to_machine_time_s(ContentOfColumn(row, "Recommended\nPoll Freq"))
        self.BODY = None
        self.VALUE = None
        self.TIMESTAMP = None
        self.TIMESTAMP_UNIX = None
        self.COMPID = '0'
        self.PARAMIDX = ''
        self.RESULT = 'NOTTESTED'
        self.response = None
        self.LastPolledAt = datetime(1971, 1, 1, 0, 0, 0)
        self.TIMETOOK = None
        self.MRD = None
        self.CACHED = None
        self.MRD_URI = None
        self.MRD_METRIC_URI = None
        self.MRD_ITEM_IN_CATALOG = None
        # print(f'{self.TGUID}: {self.URI}.{self.FIELD} = {self.VALUE} {self.RESULT}')
        

    def ValidateAndParse(self, filter_paramclass=None, filter_tguid=None):

        if filter_paramclass is not None:
            # Handle both filter out and filter in case
            if 'exclude' == filter_paramclass[0].lower() and self.PARAMCLASS in filter_paramclass:
                return False            
            elif 'exclude' != filter_paramclass[0].lower() and self.PARAMCLASS not in filter_paramclass:
                return False

        if filter_tguid is not None:
            # Handle both filter out and filter in case
            if 'exclude' == filter_tguid[0].lower() and self.TGUID in filter_tguid:
                return False  
            elif 'exclude' != filter_tguid[0].lower() and self.TGUID not in filter_tguid:
                return False

        if self.PRIORITY != "Yes":
            return False
        
        if self.AVAILABILITY not in ("Available", "Available OOB"):
            return False
        
        # Split API into sechma version, URI and Fieldname
        RFAPI = self.RFAPI.split('\n')
        if len(RFAPI) < 3:
            return False

        if RFAPI[1] == '<TBD>':
            return False
        
        if RFAPI[0] in ("TBD", "2022.1g", "Future"):
            return False
        
        self.SCHEMAVER = RFAPI[0]
        self.URI = RFAPI[1]
        self.FIELD = RFAPI[2]

        self.POLL_PERIOD = decide_poll_period(self.PARAMCLASS)
        return True
    
    def PrintResult(self):
        try:
            if type(self.VALUE) not in (list, dict) and self.VALUE is not None:
                print(f'{self.VALUE :>20} {self.RESULT:>20} {self.TIMETOOK:>20} {self.MRD:>10} {self.CACHED:>10}')
            else:
                # print(f'{str(self.VALUE) :>20} {self.RESULT:>20}')
                print(f'{"--see csv--" :>20} {self.RESULT:>20}')
        except TypeError:
            strangetype = type(self.VALUE)
            print(f'Encountered Value of type {strangetype}')
            print(f'{self.VALUE} {self.RESULT}')
            exit()
        return

    def CallRedfishAPI(self, cached_tc=None):

        global RFHOST, RFUSER, RFPASSWD
        ts_dt = datetime.now(timezone.utc)
        self.TIMESTAMP = ts_dt.isoformat() #datetime.now()
        self.TIMESTAMP_UNIX = ts_dt.timestamp()
              
        if cached_tc is not None and cached_tc.response is not None:
            # Use response provided from previous call on same URI
            self.response = cached_tc.response
            self.LastPolledAt = cached_tc.LastPolledAt
            self.TIMETOOK=0 #cached_tc.TIMETOOK
            self.MRD = 0
            self.CACHED = 1
            self.MRD_URI = None
        elif self.URI+"#/"+self.FIELD in MRDValues:
            # print('Found!!!', end='', flush=True)
            self.VALUE = MRDValues[self.URI+"#/"+self.FIELD]['Value']
            self.TIMESTAMP = MRDValues[self.URI+"#/"+self.FIELD]['Timestamp']
            self.TIMESTAMP_UNIX = datetime.fromisoformat(self.TIMESTAMP).timestamp() 
            self.RESULT = "PASS"
            self.LastPolledAt = datetime.now()
            self.MRD = 1
            self.CACHED = 0
            self.TIMETOOK = 0
            self.MRD_URI = MRDValues[self.URI+"#/"+self.FIELD]['metric_mrd_uri']
            self.MRD_METRIC_URI = MRDValues[self.URI+"#/"+self.FIELD]['metric_uri']
            self.MRD_ITEM_IN_CATALOG = MRDValues[self.URI+"#/"+self.FIELD]['metric_in_catalog'] = True
            return
        elif self.URI in MRDValues and self.FIELD == "Reading":
            # print('Found!!!', end='', flush=True)
            self.VALUE = MRDValues[self.URI]['Value']
            self.TIMESTAMP = MRDValues[self.URI]['Timestamp']
            self.TIMESTAMP_UNIX = datetime.fromisoformat(self.TIMESTAMP).timestamp()
            self.RESULT = "PASS"
            self.LastPolledAt = datetime.now()
            self.MRD = 1
            self.CACHED = 0
            self.TIMETOOK = 0
            self.MRD_URI = MRDValues[self.URI]['metric_mrd_uri']
            self.MRD_METRIC_URI = MRDValues[self.URI]['metric_uri']
            self.MRD_ITEM_IN_CATALOG = MRDValues[self.URI]['metric_in_catalog'] = True
            return
        elif self.URI in MRDValues and self.TGUID == "<MISSING IN CATALOG>":
            self.VALUE = MRDValues[self.URI]['Value']
            self.TIMESTAMP = MRDValues[self.URI]['Timestamp']
            self.TIMESTAMP_UNIX = datetime.fromisoformat(self.TIMESTAMP).timestamp()
            self.RESULT = "PASS"
            self.LastPolledAt = datetime.now()
            self.MRD = 1
            self.CACHED = 0
            self.TIMETOOK = 0
            self.MRD_URI = MRDValues[self.URI]['metric_mrd_uri']
            self.MRD_METRIC_URI = MRDValues[self.URI]['metric_uri']
            self.MRD_ITEM_IN_CATALOG  = False
            return            
        else:
            # Call the API
            self.MRD = 0
            self.CACHED = 0
            try:
                start_time=time.time()
                if (RFHOST.find('https') == -1):
                    # self.response = requests.get(RFHOST+self.URI)
                    self.response = session.get(RFHOST+self.URI)                  
                else:
                    # self.response = requests.get(RFHOST+self.URI, verify=False, auth = HTTPBasicAuth(RFUSER, RFPASSWD))
                    self.response = session.get(RFHOST+self.URI, verify=False, auth = HTTPBasicAuth(RFUSER, RFPASSWD))
                self.response.raise_for_status()
                end_time=time.time()
                self.LastPolledAt = datetime.now()
            except (requests.exceptions.Timeout, requests.exceptions.InvalidURL, requests.exceptions.RequestException) as e:
                print(e)
                self.RESULT = "APIFAILED"
                return
            self.TIMETOOK=round(end_time-start_time, 6)


        # Parse out the field
        try:
            self.BODY = self.VALUE = self.response.json()

            value = self.VALUE
            for f in self.FIELD.split('/'):
                if f.isdigit():
                    f = int(f)
                value = value[f]

            self.VALUE = value
            self.RESULT = "PASS" if value != None and value != '' else "FIELDEMPTY"
        except (KeyError, IndexError, TypeError, json.decoder.JSONDecodeError) as e:
            self.VALUE = None
            self.RESULT = "FIELDMISSING"
            return

        return


class Catalog:
    def __init__(self, catalog_file, platform, rfhost, rfuser, rfpasswd):
        self.Platform = platform
        self.CatalogEntries = []
        self.TimeSeriesDataPoints = []
        self.SaveFilePrefix = ''

        global PLATFORM
        PLATFORM = platform

        global RFHOST, RFUSER, RFPASSWD
        RFHOST = rfhost
        RFUSER = rfuser
        RFPASSWD = rfpasswd

        # Populate test cases based on the sheet
        try:
            # logging.error(f'Parsing Catalog File: {catalog_file}')
            with open(catalog_file, newline='') as f:
                reader = csv.reader(f)

                global catalog_header
                catalog_header = next(reader)
                # print(catalog_header)
                for row in reader:
                    tc = CatalogEntry(row)
                    self.CatalogEntries.append(tc)

        except EnvironmentError:  # parent of IOError, OSError *and* WindowsError where available
            print("Could not open telemetry catalog file")


    def PrintCatalog(self): #Print the catalog
        # Sort the lists
        # self.CatalogEntries.sort(key=lambda tc: tc.URI)
        
        for tc in self.CatalogEntries:
            print(f'{tc.TGUID} {tc.URI} {tc.FIELD}')
            pass

    def Sanitize(self, filter_paramclass=None, filter_tguid=None): #Remove invalid/irrelevant entries, simplify formatting, sort

        # Remove empty URI entries
        self.CatalogEntries[:] = [tc for tc in self.CatalogEntries if tc.ValidateAndParse(filter_paramclass, filter_tguid)]

        # Sort the list
        self.CatalogEntries.sort(key=lambda tc: tc.URI)

        for tc in self.CatalogEntries:
            # Normalize Field so it can be extracted easily
            tc.FIELD = tc.FIELD.replace('{', '', 1)
            tc.FIELD = tc.FIELD.replace('{', '/')
            tc.FIELD = tc.FIELD.replace('}', '')
            tc.FIELD = tc.FIELD.replace('[', '/')
            tc.FIELD = tc.FIELD.replace(']', '')
        return
    
    def Expand(self):
        # Replace wildcard entries with extrapolated list
        ExpandedCatalogEntries = []
        for tc in self.CatalogEntries:
            ExpandTc(tc, ExpandedCatalogEntries)
        self.CatalogEntries = ExpandedCatalogEntries

        #Parse out component ID and param idx TODO: This would break someday but no better ideas at this time
        for tc in self.CatalogEntries:
            indices = re.findall(r'\[(\d+)\]', tc.TGUID)
            match len(indices):
                case 3:
                    tc.COMPID = indices[1]
                    tc.PARAMIDX = indices[2]
                case 2:
                    tc.COMPID = indices[1]
                case 1:
                    tc.COMPID = indices[0]
                case _:
                    pass
        return
       
    def FetchRedfishValues(self, verbose=False, ignore_poll_period=False):
        self.start = time.time()

        last_tc = CatalogEntry('')  # blank
        last_tc.URI = ''
        for tc in self.CatalogEntries:
        
            timeSinceLastPoll = datetime.now() - tc.LastPolledAt
            if not ignore_poll_period:
                #print(f"Time since last poll: {timeSinceLastPoll.total_seconds()} Poll period {tc.POLL_PERIOD}")
                if timeSinceLastPoll.total_seconds() < tc.POLL_PERIOD:
                    # print('Skipping')
                    continue  # not time to update this entry yet

            if verbose:
                print(f'{tc.TGUID :<50} ', end='', flush=True)
                # print(f'{tc.TGUID :<50} {tc.FIELD :<50}', end='', flush=True)

            if (tc.URI == last_tc.URI):
                tc.CallRedfishAPI(cached_tc=last_tc)
            else:
                tc.CallRedfishAPI()
                pass
            last_tc = tc

            if verbose:
                tc.PrintResult() 

        self.end = time.time()

    def AddMissingMrdEntries(self):
        added_entries = 0
        for k1,v1 in MRDValues.items():
            mrd_item_in_catalog = False
            for entry in self.CatalogEntries[:]:        
                if entry.URI+"#/"+entry.FIELD == k1 or (entry.URI in k1 and entry.FIELD == "Reading"):            
                    mrd_item_in_catalog = True
                    break
            if not mrd_item_in_catalog:        
                tc = CatalogEntry('')
                tc.LastPolledAt = datetime(1971, 1, 1, 0, 0, 0) #datetime.now()
                tc.MRD = 1
                tc.CACHED = 0
                tc.MRD_URI = v1['metric_mrd_uri']
                tc.URI = v1['metric_uri']
                tc.MRD_METRIC_URI = v1['metric_uri']
                tc.TGUID = "<MISSING IN CATALOG>"
                tc.TIMETOOK = 0
                tc.DRVRREQ = None               
                tc.SCHEMAVER = None
                tc.PARAMCLASS = "Unknown"
                tc.POLL_PERIOD = decide_poll_period(tc.PARAMCLASS)
                self.CatalogEntries.append(tc)
                added_entries += 1
        logging.info(f"Added #{added_entries} missing MRD entries")

    # Function used to remove entries that are not in MRD. Called whenh --get_mrd_only is used.    
    def RemoveNonMrdEntries(self):
        removed_entries = 0
        # Iterate over copy of the list
        for entry in self.CatalogEntries[:]:
            if not (entry.URI+"#/"+entry.FIELD in MRDValues or (entry.URI in MRDValues and entry.FIELD == "Reading") or entry.MRD):
                self.CatalogEntries.remove(entry)
                removed_entries += 1
        logging.info(f"Removed #{removed_entries} non MRD entries")                
    
    def ConvertToTimeSeriesDataPoints(self):
        @dataclass
        class tsdatapoint:
            table: str
            tguid: str
            field: str
            value: any
            timestamp: datetime
        
        self.TimeSeriesDataPoints.clear()

        for tc in self.CatalogEntries:

            tablename = 'VT_'+tc.PARAMCLASS+'_'+tc.COMPCLASS+'_'+tc.COMPID
            tablename = tablename.replace('.', '_')
            fieldname = tc.PARAMNAME if tc.PARAMIDX == '' else tc.PARAMNAME + '_' + tc.PARAMIDX

            try:
                if type(tc.VALUE) not in (list, dict) and tc.VALUE is not None: #Simple case of 1 to 1 mapping
                    self.TimeSeriesDataPoints.append(tsdatapoint(table=tablename, tguid = tc.TGUID, field=fieldname, value=tc.VALUE, timestamp=tc.TIMESTAMP))

                if type(tc.VALUE) in (list, dict):
                    
                    if 'XID-LOGS' in tc.TGUID: #Complex case #1 parse XID numbers from XID logs
                        xid_entries = tc.VALUE
                        for xid_entry in xid_entries:
                            xid_timestamp = xid_entry["Created"]
                            xid_messgae = xid_entry["Message"]
                            xid_parser = re.search('XID (\d+)', xid_messgae, re.IGNORECASE)
                            xid_number = xid_parser.group(1)
                            tc.VALUE = int(xid_number)
                            tc.TIMESTAMP = xid_timestamp
                            
                            self.TimeSeriesDataPoints.append(tsdatapoint(table=tablename, tguid = tc.TGUID, field=fieldname, value=tc.VALUE, timestamp=tc.TIMESTAMP))
                    
                    if 'BASEBOARD-REDFISH-EVENT-LOG' in tc.TGUID: #Complex case #2 parse all event entries
                        event_entries = tc.VALUE
                        for event_entry in event_entries:
                            event_timestamp = event_entry["Created"]
                            if "Resolution" in event_entry:
                                event_message = str(event_entry["Severity"]) + "~" + str(event_entry["Message"]) + "~" + str(event_entry["Resolution"]) + "~" + str(event_entry["Resolved"])
                            else:
                                event_message = str(event_entry["Severity"]) + "~" + str(event_entry["Message"]) + "~" + str("N/A") + "~" + str(event_entry["Resolved"])
                            tc.VALUE = event_message
                            tc.TIMESTAMP = event_timestamp
                            # print(f'{event_timestamp} : {event_message}')
                            
                            self.TimeSeriesDataPoints.append(tsdatapoint(table=tablename, tguid = tc.TGUID, field=fieldname, value=tc.VALUE, timestamp=tc.TIMESTAMP))
                    
            except TypeError:
                strangetype = type(tc.VALUE)
                print(f'Encountered Value of type {strangetype}')
                exit()
        return        

    def DumpTSPoints(self):
        for dp in self.TimeSeriesDataPoints:
            # print(f'{tc.TGUID :<50} {tc.FIELD :<50}', end='', flush=True)
            print(f'{dp.table :<30}\t{dp.field :<50}\t{dp.value  :<20}\t{dp.timestamp}')

    def GetPrometheusMetrics(self, registry, pushgateway, host):
        metadata = {}
        serial_number = 'telemetry_db_hgx_serial_num_' + self.BASEBOARD_SERIAL_NUM
        metadata["baseboard_serial_num"] = serial_number 
        metadata["bmc_ip"] = str(host)
        
        for dp in self.TimeSeriesDataPoints:
             
            pattern = r'[\[\]-]'
            metric_name = re.sub(pattern, '_', dp.tguid)
            metric_name = metric_name.rstrip('_')

            label_name = re.sub(pattern, '_', dp.field) 
            label_name = label_name.rstrip('_')
            
            if "XID_LOGS" in metric_name:
                xid_label = str(dp.value)
                
                if registry._names_to_collectors.get(metric_name) is not None: 
                    gauge = registry._names_to_collectors.get(metric_name)
                    gauge.labels(serial_number, xid_label, host).set(dp.value)

                elif registry._names_to_collectors.get(metric_name) is None:
                    gauge = Gauge(metric_name, dp.field, ['baseboard_serial_num', 'xid_value', 'bmc_ip'])
                    registry.register(gauge)
                    gauge.labels(serial_number, xid_label, host).set(dp.value)

            elif "BASEBOARD_REDFISH_EVENT_LOG" in metric_name:
                value = -2
                values = dp.value.split('~')
                severity = values[0]
                if severity == "OK":
                    value = 1
                elif severity == "Warning":
                    value = 0 
                elif severity == "Critical":
                    value = -1
                                    
                message = values[1]
                resolution = values[-2]
                resolved = values[-1]

                if registry._names_to_collectors.get(metric_name) is not None: 
                    gauge = registry._names_to_collectors.get(metric_name)
                    gauge.labels(serial_number, severity, message, resolution, resolved, host).set(value)

                elif registry._names_to_collectors.get(metric_name) is None:
                    gauge = Gauge(metric_name, dp.field, ['baseboard_serial_num', 'severity', 'message', 'resolution', 'resolved', 'bmc_ip'])
                    registry.register(gauge)
                    gauge.labels(serial_number, severity, message, resolution, resolved, host).set(value)

            if not isinstance(dp.value, str) and "XID_LOGS" not in metric_name:
                if registry._names_to_collectors.get(metric_name) is not None:
                    gauge = registry._names_to_collectors.get(metric_name)
                    gauge.labels(serial_number, host).set(dp.value)
                
                else:
                    gauge = Gauge(metric_name, dp.field, ['baseboard_serial_num', 'bmc_ip'])
                    registry.register(gauge)
                    gauge.labels(serial_number, host).set(dp.value)

            else:
                value = -2
                flag = False
                if dp.value == "OK" or dp.value == "Enabled" or dp.value == "LinkUp":
                    value = 1 
                    flag = True
                elif dp.value == "Warning":
                    value = 0
                    flag = True
                elif dp.value == "Critical" or dp.value == "Disabled" or dp.value == "LinkDown":
                    value = -1
                    flag = True
                elif "XID_LOGS" not in metric_name: 
                    metadata[metric_name] = dp.value
                    flag = False
                
                if registry._names_to_collectors.get(metric_name) is not None and flag == True: 
                    gauge = registry._names_to_collectors.get(metric_name)
                    gauge.labels(dp.value, serial_number, host).set(value)
                        
                elif registry._names_to_collectors.get(metric_name) is None and flag == True:
                    gauge = Gauge(metric_name, dp.field, [label_name, 'baseboard_serial_num', 'bmc_ip'])
                    registry.register(gauge)
                    gauge.labels(dp.value, serial_number, host).set(value)

        if registry._names_to_collectors.get("nvidia_telemetry_agent_meta_data") is not None:
            m = registry._names_to_collectors.get("nvidia_telemetry_agent_meta_data")
            m.info(metadata)
        else:
            m = Info("nvidia_telemetry_agent_meta_data", "metadata")
            registry.register(m)
            m.info(metadata)

        if pushgateway != "":
            push_to_gateway(gateway=pushgateway, job='test_job', registry=registry)

    def PushValuesToInflux(self, influxserver = '', influxorg = 'NVIDIA', influxtoken = ''):

        # Check if import of influx client module was successfull
        global influx_libs_available
        if influx_libs_available == False:
            print("import of influxdb_client failed")

        # Connect to InfluxDB and create a database if not exists
        self.database_name = 'telemetry_db_hgx_serial_num_' + self.BASEBOARD_SERIAL_NUM
        print("DB name:", self.database_name)
        client = InfluxDBClient(url=influxserver, token=influxtoken, org=influxorg)
        buckets_api = client.buckets_api()
        if buckets_api.find_bucket_by_name(self.database_name) is None:
            created_bucket = buckets_api.create_bucket(
                bucket_name=self.database_name, retention_rules=None, org=influxorg)

        write_api = client.write_api()

        for dp in self.TimeSeriesDataPoints:
            # print(f'{dp.table :<30}\t{dp.field :<50}\t{dp.value  :<20}\t{dp.timestamp}')
            p = influxdb_client.Point(dp.table).field(dp.field, dp.value).time(dp.timestamp)
            print("Adding record:", dp.field, dp.value, dp.timestamp)
            write_api.write(bucket=self.database_name, org=influxorg, record=p)

    def GetBasicSystemInfo(self):
        for tc in self.CatalogEntries:
            if 'BASEBOARD-BRD-SERIAL' in tc.TGUID:
                tc.CallRedfishAPI()
                self.BASEBOARD_SERIAL_NUM = tc.VALUE
                return

    def CollectPrintSummary(self, print_summary, mrd_time):
        rc = 0
        tc_total = 0
        tc_passed = 0
        tc_callfailed = 0
        tc_fieldmissing = 0
        tc_fieldempty = 0
        for tc in self.CatalogEntries:
            tc_total += 1
            if tc.RESULT == "PASS":
                tc_passed += 1
            if tc.RESULT == "APIFAILED":
                tc_callfailed += 1
            if tc.RESULT == "FIELDMISSING":
                tc_fieldmissing += 1
            if tc.RESULT == "FIELDEMPTY":
                tc_fieldempty += 1
        tc_pass_percent = round(100*tc_passed/tc_total, 2)
        time_taken_secs = round(self.end-self.start + mrd_time, 2)
        time_taken_mins = round(time_taken_secs/60, 2)

        if print_summary:
            print('=============================================================================================================')
            print(f'TOTAL={tc_total} PASS={tc_passed} FAILEDAPIs={tc_callfailed} FIELDSMISSING={tc_fieldmissing} FIELDSEMPTY={tc_fieldempty} PASSPERCENT={tc_pass_percent} TIMETAKEN={time_taken_secs}s(={time_taken_mins}m)')
            print('=============================================================================================================')

        if tc_pass_percent < 99: #HACK: not 100 to accomodate some known HMC bugs getting fixed in update 1
            rc = 2
        
        return rc

    def CreateOutputDir(self, outputdir):
        # Create output directory 
        if not os.path.exists(outputdir):
            try:
                os.makedirs(outputdir)
            except IOError:
                logging.error("Unable to open make output directory: %s", outputdir)
                sys.exit(1)
        now = datetime.now()
        self.SaveFilePrefix = os.path.join(outputdir, "testresults_" + now.strftime("%Y_%m_%d_%H_%M_%S"))

    def SaveResults(self, save_format, verbose_save = False):
        
        # this function only handles csv and json output.  For other formats return
        if "csv" not in save_format and "json" not in save_format:
           return
 
        # if saving verbose - then dump out additional fields - otherwise keep it at minimal for file size reasons
        if verbose_save:
            field_list = ["TGUID", "PARAMCLASS", "COMPCLASS", "SMBPBI", "DRVRREQ", "SCHEMAVER", "URI", "MRD_METRIC_URI", "BODY", "FIELD", "VALUE", "RESULT", "TIMESTAMP_UNIX", "TIMESTAMP", "TIMETOOK", "MRD", "CACHED", "MRD_URI"]
        else:         
            field_list = ["TGUID", "PARAMCLASS", "COMPCLASS", "VALUE", "RESULT", "MRD_METRIC_URI", "TIMESTAMP_UNIX", "TIMESTAMP", "TIMETOOK"]

        # Put data into dictionary for fields needed.
        catalog_list_dicts = []
        for tc in self.CatalogEntries:
            dict_entry = {}
            for field in field_list:
                dict_entry[field] = eval("tc." + field)
            catalog_list_dicts.append(dict_entry)

        # Handle CSV file saving        
        if "csv" in save_format:                   
            csv_file = self.SaveFilePrefix + ".csv"
            # check if file exists and non-zero size
            csv_file_exists = os.path.isfile(csv_file) and os.path.getsize(csv_file) > 0
            try: 
                with open(csv_file, 'a', newline='') as f:
                    csv_writer = csv.DictWriter(f, fieldnames=field_list)
                    if not csv_file_exists:
                        csv_writer.writeheader()
                    csv_writer.writerows(catalog_list_dicts)
            except IOError:
                logging.error("Unable to open csv for writing %s", csv_file)
                sys.exit(1)
            # create/update link to latest results
            os.symlink(csv_file, 'tmpLink')
            os.rename('tmpLink', 'latest_results.csv')
         
        #Handle JSON file saving
        if "json" in save_format:
            json_file = self.SaveFilePrefix + ".json"
            try:
                with open(json_file, 'a', newline='') as f:
                    json.dump(catalog_list_dicts, f)
            except IOError:
                logging.error("Unable to open json for writing %s", json_file)
                sys.exit(1)                            
            # create/update link to latest results
            os.symlink(json_file, 'tmpLink')
            os.rename('tmpLink', 'latest_results.json')

@dataclass
class URIExpansion:
    placeholder: str
    replacement: str

def ReadInURIExpansionLogic(uri_expansions_file, platform):
    global Expansions
    Expansions = []

    try:
        logging.info(f'Parsing URI Expansion File: {uri_expansions_file}')
        with open(uri_expansions_file, newline='') as f:
            reader = csv.reader(f)
            header_row = next(reader)
            plat_idx = find_column_index(header_row, platform)
            for row in reader:
                # print(row)
                exp = URIExpansion(row[0], row[plat_idx])
                Expansions.append(exp)

    except EnvironmentError:  # parent of IOError, OSError *and* WindowsError where available
        logging.error(f"Could not open URI Expansions file {uri_expansions_file}")
        
    # Normalize list by replacing known values in placeholders themeselves
    # print("\n\n\nBefore Processing")
    # print_expansion_logic()
    for i,x in enumerate(Expansions):
        if x.placeholder[0] == '/': #Skip exact paths which mean verbatim replacements using a list
            continue
        for exp in Expansions:
            if exp.placeholder != Expansions[i].placeholder:
                Expansions[i].placeholder = Expansions[i].placeholder.replace(exp.placeholder, exp.replacement)
    # print("\n\n\nAfter Processing")
    # print_expansion_logic()

def print_expansion_logic():
    for exp in Expansions:
        print(f'{exp.placeholder} --> {exp.replacement}')

def expand_range_to_list(input):
    expanded_list = []
    for member in input.split(','):
        if '-' not in member: #simple comma separated list
            expanded_list.append(member)
        else: #a range
            range_start = int(member.split('-')[0])
            range_end = int(member.split('-')[1])
            expanded_list.extend(range(range_start, range_end+1))
    return expanded_list

def ExpandTc(tc, extrapolated_tcs): #NOTE: This is a recursive function. Beware of unintended flows.
    logging.info(f'Input URI: {tc.URI}')

    if "{" not in tc.URI:  # Base condition: No wildcards in URI, so no (further) expansion is required
        # print(f'{uri}') # return the uri as is
        # print(f'Output URI: {tc.URI}') # return the uri as is
        extrapolated_tcs.append(tc)
        return

    for exp in Expansions:  #search expansion list to find an exact match first
        if exp.placeholder == tc.URI:
            possible_values = expand_range_to_list(exp.replacement)
            for idx, value in enumerate(possible_values):
                temp_tc = copy.copy(tc)
                temp_tc.URI = value.strip()
                temp_tc.TGUID = f'{tc.TGUID}[{idx}]'
                # print(f'Output URI: {temp_tc.URI}') # return the uri as is
                extrapolated_tcs.append(temp_tc)
            return # for exact match replacements, no further processing required

    matched = False
    for exp in Expansions:  #search expansion list to see if any placeholder patter matches this uri
        match = re.search(exp.placeholder, tc.URI)
        if match:
            matched=True
            if '{InstanceId}' not in exp.placeholder:
                logging.info(f'simple replacement: {exp.placeholder} --> {exp.replacement}')
                tc.URI = tc.URI.replace(exp.placeholder, exp.replacement)
                if "{" not in tc.URI: # If no more expansions possible, add the entry
                    # print(f'Output URI: {tc.URI}') # return the uri as is
                    extrapolated_tcs.append(tc)
            else:
                instanceid_range = expand_range_to_list(exp.replacement)
                logging.info(f'list replacement: {exp.placeholder} --> {instanceid_range}')
                for id in instanceid_range:
                    text2search = match.group()
                    text2replace = text2search.replace('{InstanceId}', f'{id}')
                    # logging.debug(f'list replacement: {text2search} --> {text2replace}')
                    temp_tc = copy.copy(tc)
                    temp_tc.URI = temp_tc.URI.replace(text2search, text2replace)
                    temp_tc.TGUID = f'{tc.TGUID}[{id}]'
                    ExpandTc(temp_tc, extrapolated_tcs) #Recurse to expand other wildcard parts of the URI, if any
                break
    if not matched:
        logging.error(f'No Match for: {tc.TGUID :<30} {tc.URI}')  
    
    # if "{" in tc.URI:
    #     print(f'Expansion incomplete for: {tc.TGUID :<30} {tc.URI}')  
       
   
def ClearLogServices():

    print("Clearing log services")

    service_log_list = []
    
    # get the list of service logs
    try:
        global RFHOST, RFUSER, RFPASSWD
        cmd_response = session.get(RFHOST+"/redfish/v1/Systems/System_0/LogServices", verify=False, auth = HTTPBasicAuth(RFUSER, RFPASSWD))
        cmd_response.raise_for_status()
        cmd_results = cmd_response.json()
    except (requests.exceptions.Timeout, 
            requests.exceptions.InvalidURL, 
            requests.exceptions.RequestException) as e:
        logging.warning("Unable to get list of Service logs !!!")

    for service_log_object in cmd_results['Members']:
        for k,v in service_log_object.items():
            if 'postcodes' not in v.lower():
                logging.debug(f"Clearing log:{v}")
                service_log_list.append(v)

    # Iterate through the list of service logs and clear the ones where clearlog is found
    for service_log in service_log_list:
        try:
            cmd_response = session.get(RFHOST + service_log, verify=False, auth = HTTPBasicAuth(RFUSER, RFPASSWD))
            cmd_response.raise_for_status()
            cmd_results = cmd_response.json()
        except (requests.exceptions.Timeout, 
                requests.exceptions.InvalidURL, 
                requests.exceptions.RequestException) as e:
            logging.warning(f"Unable to get access service {service_log}!!!")
        if 'Actions' in cmd_results:
            if '#LogService.ClearLog' in cmd_results['Actions']:
                if 'target' in cmd_results['Actions']['#LogService.ClearLog']:
                    clear_log_uri = cmd_results['Actions']['#LogService.ClearLog']['target']
                    try:
                        cmd_response = session.post(RFHOST + clear_log_uri, auth = HTTPBasicAuth(RFUSER, RFPASSWD))
                    except (requests.exceptions.Timeout, 
                            requests.exceptions.InvalidURL, 
                            requests.exceptions.RequestException) as e: 
                        logging.warning(f"Unable to clear log {clear_log_uri}!!!")
     

def GetMrdList(filter_mrd_list=None):

    mrd_list = []
    mrd_list_filtered = []


    # Get MRD list by reading the metricreports from target
    try:
        global RFHOST, RFUSER, RFPASSWD
        cmd_response = session.get(RFHOST+"/redfish/v1/TelemetryService/MetricReports", verify=False, auth = HTTPBasicAuth(RFUSER, RFPASSWD))
        cmd_response.raise_for_status()
        cmd_results = cmd_response.json()
    except (requests.exceptions.Timeout, 
            requests.exceptions.InvalidURL, 
            requests.exceptions.RequestException,
            requests.exceptions.HTTPError) as e:
        logging.error(f"Unable to get list of MRD tables - exception occurd {e} !!!")
        sys.exit(1)

    for mrd_object in cmd_results['Members']:
        for k,v in mrd_object.items():    
            mrd_list.append(v)
    logging.debug(f"Unfiltered list of MRDs is: {mrd_list}")

    
    # if filter mrd is specified then only includes those mrd
    if filter_mrd_list is None:
        return mrd_list
    else:
        exclude_mode = True if 'exclude' == filter_mrd_list[0].lower() else False
        if exclude_mode:
            mrd_list_filtered = copy.deepcopy(mrd_list)
        for filter_mrd_item in filter_mrd_list:
            mrd_matched = False
            if filter_mrd_item.lower() == 'exclude':
                continue
            for mrd_item in mrd_list:
                if filter_mrd_item.lower() in mrd_item.lower():
                    mrd_matched = True
                    if exclude_mode:
                        mrd_list_filtered.remove(mrd_item)
                    else:
                        mrd_list_filtered.append(mrd_item)
            if not mrd_matched:
                logging.warning(f"MRD Filter {filter_mrd_item} didn't match any existing MRD: {mrd_list}")                                 
        return mrd_list_filtered       
    
# MRD (Metric Report Definition) APIs are redfish endpoints which return 
# several usefull telemetry in a single call thus cutting down on the 
# number of API calls to make and thus overall time taken + load on target machine
def FetchMRDValues(mrd_list):
    
    # if MRD list is empty then print warning message and return
    if len(mrd_list) == 0:
        logging.warning(f"MRD List is empty, will not be using MRDs for any telemetry data")
        return 0
    
    logging.info(f"MRD regions to be read are {mrd_list}")
    
    mrd_start_time=time.time()
    
    # Call each MRD
    for mrd_uri in mrd_list:
        # print(f'processing MRD: {mrd_uri}')
        try:
            global RFHOST, RFUSER, RFPASSWD
            mrd_response = session.get(RFHOST+mrd_uri, verify=False, auth = HTTPBasicAuth(RFUSER, RFPASSWD))
            mrd_response.raise_for_status()
            mrd_results = mrd_response.json()
        except (requests.exceptions.Timeout, 
                requests.exceptions.InvalidURL, 
                requests.exceptions.RequestException) as e:
            print(f"MRD API {mrd_uri} call failed !!!")
            # exit()
            continue
        
        # print(f"Success")
        # print(RFHOST+mrd_uri)
        # print(mrd_results)
        global MRDValues
        try:
            for entry in mrd_results['MetricValues']:
                metric_uri = entry['MetricProperty']
                metric_val = entry['MetricValue']
                metric_ts = entry['Timestamp']
                
                # metric_uri = metric_uri.split('#', 1)[0]
                # print(f'{metric_uri} -> {metric_val}')
                entry_dict = {}
                if string_is_int(metric_val):
                    entry_dict['Value']=int(metric_val)
                elif string_is_float(metric_val):
                    entry_dict['Value']=float(metric_val)
                else:
                    entry_dict['Value']=metric_val
                entry_dict['Timestamp']=metric_ts
                entry_dict['metric_mrd_uri']=mrd_uri
                entry_dict['metric_uri']=metric_uri
                entry_dict['metric_in_catalog']=False
                MRDValues[metric_uri] = entry_dict
                    
        except (KeyError) as e:
            print(f"MRD API {mrd_uri} contents invalid. Probably not implemented yet? ")
            continue
    mrd_end_time=time.time()


    return round(mrd_end_time-mrd_start_time, 6)

def PrintMRDValues():
    global MRDValues
    for index, (key, value) in enumerate(MRDValues.items()):
        print(f'{key} -> {value}')

def string_is_int(t):
    try:
        int(t)
        return True
    except ValueError:
        return False

def string_is_float(t):
    try:
        float(t)
        return True
    except ValueError:
        return False
