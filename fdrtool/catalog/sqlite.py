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
import os
import sqlite3
from enum import Enum
import traceback
# Import third-party library modules
import logging
logging.basicConfig(level=logging.INFO)

# Import locally developed modules
import fdr_logs_schema_pb2 as fdr_schema
from catalog.catalog import CatalogEntry, FDR_TABLE_SCHEMA, FDR_TABLE_TYPE

DB_DIRECTORY = './fdr_logs_db'

'''
Class responsible for SQLite connection
'''
class SQLiteConnection:
  def __init__(self, catalog_name, append_to_existing=False):
    # Connect to SQLite and create a database if not exists
    database_name = 'telemetry_db_hgx_serial_num_' + catalog_name + '.db' 
    print('sqlite database name: {}'.format(database_name))
    db = os.path.join(DB_DIRECTORY, database_name)
    if not append_to_existing and os.path.exists(db): # if we DON'T want to append to existing DB and there is DB present already
      os.remove(db)
    if not os.path.exists(DB_DIRECTORY):
      os.mkdir(DB_DIRECTORY)
    self.connection = sqlite3.connect(db)
    # Note: if there is any error here, it should propagate to the main, because there is no point of further execution.

  def GetTable(self, tablename):
    cmd = "SELECT * FROM " + tablename
    try:
      rows = self.connection.execute(cmd)
      return rows
    except sqlite3.Error as e:
      logging.error("Couldn't get table {}. {}".format(tablename, e))
  
  def PrintTable(self, tablename):
    rows = self.GetTable(tablename)
    if rows:
      print("\nResults for {}: {}\n".format(tablename, rows.fetchall()))

  def CreateTableQuery(self, tablename, tabletype, primary_key=None):
    """_summary_

    Args:
        tablename (_type_): _description_
        tabletype (_type_): _description_
        primary_key (tuple, optional): _description_. Defaults to None.

    Returns:
        _type_: _description_
    """
    table_columns = FDR_TABLE_SCHEMA[tabletype.name]
    table_str = '('
    all_columns = []
    for column in table_columns:
      column_str = str(column) + ' ' + str(table_columns[column])
      all_columns.append(column_str)
    all_column_str = ', '.join(all_columns)
    table_str += all_column_str
    if primary_key:
      table_str += ', CONSTRAINT ' + tablename + '_PK PRIMARY KEY (' + (',').join(primary_key) + ')'
    table_str += ')'
    sql_cmd = "CREATE TABLE if not exists " + tablename + " " + table_str + ";"
    return sql_cmd

  def InsertRowQuery(self, tablename, tabletype):
    table_columns = FDR_TABLE_SCHEMA[tabletype.name]
    all_columns = []
    for column in table_columns:
      column_str = ":" + str(column)
      all_columns.append(column_str)
    row_str = ', '.join(all_columns)
    sql_cmd = "INSERT INTO " + tablename + " VALUES(" + row_str + ")"
    return sql_cmd

  def ExecuteCmd(self, sql_execute_func, *argv):
    """Method to execute SQL command

    Args:
        sql_execute_func (function pointer): what type of "execute" needs to be run

    Returns:
        list: Result of the SQL command execution
    """
    # Using context manager as that's considered best practice according to python docs
    # Successful, con.commit() is called automatically afterwards
    # con.rollback() is called after the with block finishes with an exception, the
    # exception is still raised and must be caught
    result = None
    try:
      with self.connection:
        query_res = sql_execute_func(*argv)
        result = query_res.fetchall()
    except sqlite3.IntegrityError as e:
        raise sqlite3.Error("{} failed! {}".format(argv[0], e))
    return result

  def ExecuteOneCmd(self, sql_cmd) :
    # Using context manager as that's considered best practice according to python docs
    # Successful, con.commit() is called automatically afterwards
    # con.rollback() is called after the with block finishes with an exception, the
    # exception is still raised and must be caught
    try:
      with self.connection:
        self.connection.execute(sql_cmd)
    except sqlite3.IntegrityError:
        raise sqlite3.Error("{} failed!".format(sql_cmd))

  def ExecuteMultipleCmd(self, sql_cmd, values):
    # Using context manager as that's considered best practice according to python docs
    # Successful, con.commit() is called automatically afterwards
    # con.rollback() is called after the with block finishes with an exception, the
    # exception is still raised and must be caught
    try:
      with self.connection:
        self.connection.executemany(sql_cmd, values)
    except sqlite3.IntegrityError:
        raise sqlite3.Error("{} failed!".format(sql_cmd))

  def WriteToTable(self, tablename, tabletype, rows):
    primary_key = None
    # To-do: Add any required PRIMARY KEY here
    # if tabletype is FDR_TABLE_TYPE.PVT:
    #   primary_key = ('TimeStamp', 'ParamID')
    sql_cmd_create_table = self.CreateTableQuery(tablename, tabletype, primary_key)
    #print("Table create command : %s " % sql_cmd_create_table)
    #self.ExecuteOneCmd(sql_cmd_create_table)
    self.ExecuteCmd(self.connection.execute, sql_cmd_create_table)

    sql_cmd_insert = self.InsertRowQuery(tablename, tabletype)
    #self.ExecuteMultipleCmd(sql_cmd_insert, rows)
    self.ExecuteCmd(self.connection.executemany, sql_cmd_insert, rows)

    #self.PrintTable(tablename) #testing to verify if the data was actually inserted

  def Close(self):
    if self.connection:
      # Check if all tables are written
      #print("ALL THE TABLES: {}".format(self.connection.execute("SELECT name FROM sqlite_master").fetchall()))
      
      self.connection.close()

'''
Child class for holding a single data entry/message for SQLite
'''
class SQLiteDBCatalogEntry(CatalogEntry):
  logging.debug('SQLiteDBCatalogEntry: Entry')
  APV_cmd_list = [] # SQL cmd to see all PVT together. Strings will be appended as we read logs.
  ASV_cmd_list = [] # SQL cmd to see all Stats together. Strings will be appended as we read logs.
  
  def __init__(self, filepath, ParamIDClassDict = None, ParamIDNameDict= None):
    super().__init__(filepath, ParamIDClassDict, ParamIDNameDict)
    self.ParamIDClassDict = ParamIDClassDict
    self.tablename = self.FindTablename()

    if self.tablename.startswith(tuple(FDR_TABLE_SCHEMA)):
      self.tabletype = [key for key in FDR_TABLE_TYPE if self.tablename.startswith(key.name)][0] # List should have only one item
    else:
      logging.error("Provided tablename is {}, but tabletype doesn't exist in the Sqlite schema.".format(self.tablename))

  @staticmethod
  def AppendToCombinedList(tabletype, fetch_cmd):
    logging.debug('AppendToCombinedList: Entry')

    if tabletype is FDR_TABLE_TYPE.PVT:
      if fetch_cmd not in SQLiteDBCatalogEntry.APV_cmd_list:
        SQLiteDBCatalogEntry.APV_cmd_list.append(fetch_cmd)
    elif tabletype is FDR_TABLE_TYPE.PST:
      if fetch_cmd not in SQLiteDBCatalogEntry.ASV_cmd_list:
        SQLiteDBCatalogEntry.ASV_cmd_list.append(fetch_cmd)
  
  def UpdateTableName(self, param_class):
    #print('param class %s and tablename %s' % (param_class, self.tablename))
    if "_" in self.tablename:
      field_list = self.tablename.split('_')
      self.tablename = '_'.join([field_list[0], param_class, field_list[-1]])

  def AddMessage(self, proto_msg, is_event_type= False):
    table_columns = FDR_TABLE_SCHEMA[self.tabletype.name]
    values = CatalogEntry.get_message_values(proto_msg, list(table_columns.keys()))

    values['BootId'] = self.bootid
    if (not is_event_type) and self.tabletype is not FDR_TABLE_TYPE.PDT and \
      self.tabletype is not FDR_TABLE_TYPE.BootEvent and \
      self.tabletype is not FDR_TABLE_TYPE.BookKeeper and \
      self.tabletype is not FDR_TABLE_TYPE.EventDetails:
      values['ParamClass'] = self.ParamIDClassDict[values['ParamID']]
      self.UpdateTableName(values['ParamClass'])
    self.messages.append(values)
  
  def GetMessageDict(self, message):
    return message
    
  def UpdateParamID(self, sqliteClient):
    for message in self.messages:
      if message.get("ParamName") and message.get("ParamID") is None:
        message_ParamClass = message_dict.get("ParamClass").replace('.', '_')
        sql_cmd = "SELECT ParamID from PDT WHERE CompClass = '" +  str(self.compClass) + "' AND ParamClass = '" +\
                  str(message_ParamClass) + "' AND ParamName = '" + str(message["ParamName"]) + "'"
        res = sqliteClient.ExecuteCmd(sqliteClient.connection.execute, sql_cmd)
        print('Updating ParamID for {}'.format(self.tablename))
        message["ParamID"] = res[0][0] if res else None
        
  def GetParamValue(self, message, paramId=None, paramName=None):
    paramValue = None
    if (paramId is not None and message.get('ParamID') == paramId) or (paramName is not None and message.get('ParamName') == paramName):
      for key in message:
        if 'ParamValue' in key:
          paramValue = message.get(key)
          break
    return paramValue
  
  def WriteEntry(self, **kwargs):
    sqliteClient = kwargs.get('sqliteClient')
    if sqliteClient:
      if self.tabletype is FDR_TABLE_TYPE.PVT or self.tabletype is FDR_TABLE_TYPE.PST:
        self.UpdateParamID(sqliteClient)
        sqliteClient.WriteToTable(self.tablename, self.tabletype, self.messages)
        
        # Add the table to Sqlite View creating command as well
        fetch_cmd = "SELECT *, '" + str(self.paramClass) + "' as ParamClass, '" + str(self.compClass) +\
                          "' as CompClass, '" + str(self.compID) + "' as CompID"
        fetch_cmd += " FROM " + self.tablename
        SQLiteDBCatalogEntry.AppendToCombinedList(self.tabletype, fetch_cmd)  
      else: # FDR_TABLE_TYPE.PDT, BookKeeper, BookOfError
        sqliteClient.WriteToTable(self.tablename, self.tabletype, self.messages)
    
    else:
      print("Missing sqliteClient")

  def __repr__(self): 
    return "Logs for {}:\n{}\n".format(self.tablename, self.messages)
    #return "Logs for {}".format(self.tablename)

def CreateSqliteView(view_name, create_view_as, sqliteClient):
  # Drop any existing view
  drop_view_cmd = "DROP VIEW IF EXISTS " + view_name
  sqliteClient.ExecuteCmd(sqliteClient.connection.execute, drop_view_cmd)
  # Now create new view
  create_view_cmd = "CREATE VIEW " +  view_name + " AS " + create_view_as
  sqliteClient.ExecuteCmd(sqliteClient.connection.execute, create_view_cmd)

def CreateOneCombinedView(sqliteClient, all_tables_view_name, all_tables_cmd_list, combined_view_name, combined_view_create_cmd):
  if all_tables_cmd_list: # list is not empty - so there is at least one table
    try:
      create_view_as = " UNION ALL ".join(all_tables_cmd_list)
      CreateSqliteView(all_tables_view_name, create_view_as, sqliteClient)
      CreateSqliteView(combined_view_name, combined_view_create_cmd, sqliteClient)
      
      # Verify if the view was created successfully
      sqliteClient.GetTable(combined_view_name)
      print(f"Created Sqlite View: {combined_view_name}")
    except sqlite3.Error as e:
      logging.error(f"Couldn't create Sqlite view {combined_view_name}. {e}") 
  else:
    print(f"WARNING: Sqlite view {combined_view_name} is not created as there are no tables to be added to the view.")

def CreateCombinedViews(sqliteClient):
  all_tables_view_name = "APV" # APV (All Parameters View)
  combined_view_name = "CDV" # CDV (Combined Data View)

  combined_view_create_cmd = "SELECT datetime(" + all_tables_view_name + ".TimeStamp, 'unixepoch') as Time, \
                      " + all_tables_view_name + ".ParamValue, " + all_tables_view_name + ".CompID, PDT.* FROM PDT \
                      INNER JOIN " + all_tables_view_name + " ON " + all_tables_view_name + ".ParamID=PDT.ParamID AND "\
                      + all_tables_view_name + ".CompClass=PDT.CompClass"

  CreateOneCombinedView(sqliteClient, all_tables_view_name, SQLiteDBCatalogEntry.APV_cmd_list, combined_view_name, combined_view_create_cmd)
  
  all_tables_view_name = "ASV" # ASV (All Stats View)
  combined_view_name = "CSV" # CSV (Combined Stats View)
  combined_view_create_cmd = "SELECT datetime(" + all_tables_view_name + ".FromTime, 'unixepoch') as FromTime, \
                      datetime(" + all_tables_view_name + ".ToTime, 'unixepoch') as ToTime, \
                      " + all_tables_view_name + ".Min, " + all_tables_view_name + ".Max, " + all_tables_view_name + ".Avg, "\
                      + all_tables_view_name + ".CompID, PDT.* FROM PDT \
                      INNER JOIN " + all_tables_view_name + " ON " + all_tables_view_name + ".ParamID=PDT.ParamID AND "\
                      + all_tables_view_name + ".CompClass=PDT.CompClass "

  CreateOneCombinedView(sqliteClient, all_tables_view_name, SQLiteDBCatalogEntry.ASV_cmd_list, combined_view_name, combined_view_create_cmd)