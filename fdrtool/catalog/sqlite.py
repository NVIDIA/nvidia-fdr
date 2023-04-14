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
  APV_cmd_list = [] # SQL cmd to see all PVT together. Strings will be appended as we read logs.
  ASV_cmd_list = [] # SQL cmd to see all Stats together. Strings will be appended as we read logs.
  def __init__(self, filepath):
    super().__init__(filepath)

    self.tablename = self.FindTablename()
    if self.tablename.startswith(tuple(FDR_TABLE_SCHEMA)):
      self.tabletype = [key for key in FDR_TABLE_TYPE if self.tablename.startswith(key.name)][0] # List should have only one item
    else:
      raise sqlite3.Error("Provided tablename is {}, but tabletype doesn't exist in the Sqlite schema.".format(self.tablename))

  @staticmethod
  def AppendToCombinedList(tabletype, fetch_cmd):
    if tabletype is FDR_TABLE_TYPE.PVT:
      SQLiteDBCatalogEntry.APV_cmd_list.append(fetch_cmd)
    elif tabletype is FDR_TABLE_TYPE.STATS:
      SQLiteDBCatalogEntry.ASV_cmd_list.append(fetch_cmd)
      
  def AddMessage(self, proto_msg):
    table_columns = FDR_TABLE_SCHEMA[self.tabletype.name]
    values = CatalogEntry.get_message_values(proto_msg, list(table_columns.keys()))
    self.messages.append(values)
    
  def UpdateParamID(self, sqliteClient):
    for message in self.messages:
      sql_cmd = "SELECT ParamID from PDT WHERE CompClass = '" +  str(self.compClass) + "' AND ParamClass = '" +\
                str(self.paramClass) + "' AND ParamName = '" + str(message["ParamName"]) + "'"
      res = sqliteClient.ExecuteCmd(sqliteClient.connection.execute, sql_cmd)
      #print('Updating PAramID for {}'.format(self.tablename))
      message["ParamID"] = res[0][0] if res else None
        
  def GetParamValue(self, message, paramName):
    paramValue = None
    if message.get('ParamName') == paramName:
      for key in message:
        if 'ParamValue' in key:
          paramValue = message.get(key)
          break
    return paramValue
  
  def WriteEntry(self, **kwargs):
    sqliteClient = kwargs.get('sqliteClient')
    if sqliteClient:
      if self.tabletype is FDR_TABLE_TYPE.PVT or self.tabletype is FDR_TABLE_TYPE.STATS:
        try:
          self.UpdateParamID(sqliteClient)
          sqliteClient.WriteToTable(self.tablename, self.tabletype, self.messages)
          fetch_cmd = "SELECT *, '" + str(self.paramClass) + "' as ParamClass, '" + str(self.compClass) +\
                      "' as CompClass, '" + str(self.compID) + "' as CompID"
          fetch_cmd += " FROM " + self.tablename
          SQLiteDBCatalogEntry.AppendToCombinedList(self.tabletype, fetch_cmd)
        except sqlite3.Error as e:
          logging.error(e)
          traceback.print_exc()
      elif self.tabletype is FDR_TABLE_TYPE.PDT:
        try:
          sqliteClient.WriteToTable(self.tablename, self.tabletype, self.messages)
        except sqlite3.Error as e:
          logging.error(e)
          traceback.print_exc()
    else:
      print("Missing sqliteClient")

  def __repr__(self): 
    return "Logs for {}:\n{}\n".format(self.tablename, self.messages)
    #return "Logs for {}".format(self.tablename)

def CreateOneCombinedView(view_name, create_view_as, sqliteClient):
  # Drop any existing view
  drop_view_cmd = "DROP VIEW IF EXISTS " + view_name
  sqliteClient.ExecuteCmd(sqliteClient.connection.execute, drop_view_cmd)
  # Now create new view
  create_view_cmd = "CREATE VIEW " +  view_name + " AS " + create_view_as
  sqliteClient.ExecuteCmd(sqliteClient.connection.execute, create_view_cmd)

def CreateCombinedViews(sqliteClient):
  try:
    all_PDTs_view = "APV" # APV (All Parameters View)
    create_view_as = " UNION ALL ".join(SQLiteDBCatalogEntry.APV_cmd_list)
    CreateOneCombinedView(all_PDTs_view, create_view_as, sqliteClient)
    
    combined_data_view = "CDV" # CDV (Combined Data View)
    create_view_as = "SELECT datetime(" + all_PDTs_view + ".TimeStamp, 'unixepoch') as Time, \
                    " + all_PDTs_view + ".ParamValue, " + all_PDTs_view + ".CompID, PDT.* FROM PDT \
                    INNER JOIN " + all_PDTs_view + " ON " + all_PDTs_view + ".ParamID=PDT.ParamID AND "\
                    + all_PDTs_view + ".CompClass=PDT.CompClass AND  " + all_PDTs_view + ".ParamClass=PDT.ParamClass"
    CreateOneCombinedView(combined_data_view, create_view_as, sqliteClient)
    
    # Verify if the view was created successfully
    sqliteClient.GetTable("CDV")
  except sqlite3.Error as e:
    logging.error("Couldn't create combined views for PVT. {}".format(e)) 
  
  try:
    all_Stats_view = "ASV" # ASV (All Stats View)
    create_view_as = " UNION ALL ".join(SQLiteDBCatalogEntry.ASV_cmd_list)
    CreateOneCombinedView(all_Stats_view, create_view_as, sqliteClient)
    
    combined_stats_view = "CSV" # CSV (Combined Stats View)
    create_view_as = "SELECT datetime(" + all_Stats_view + ".FromTime, 'unixepoch') as FromTime, \
                    datetime(" + all_Stats_view + ".ToTime, 'unixepoch') as ToTime, \
                    " + all_Stats_view + ".Min, " + all_Stats_view + ".Max, " + all_Stats_view + ".Avg, "\
                    + all_Stats_view + ".CompID, PDT.* FROM PDT \
                    INNER JOIN " + all_Stats_view + " ON " + all_Stats_view + ".ParamID=PDT.ParamID AND "\
                    + all_Stats_view + ".CompClass=PDT.CompClass AND  " + all_Stats_view + ".ParamClass=PDT.ParamClass"
    CreateOneCombinedView(combined_stats_view, create_view_as, sqliteClient)
    
    # Verify if the view was created successfully
    sqliteClient.GetTable("CSV")
  except sqlite3.Error as e:
    logging.error("Couldn't create combined views for Stats. {}".format(e)) 