/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <string>
#include <sstream>
#include <spdlog/spdlog.h>
#include "fdr_store.hpp"

#include "fdr_logs_schema.pb.h"
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <google/protobuf/io/zero_copy_stream.h>

// This global variable is just for debugging to track the number of FDRStore objects
// created for the whole FDR instance
uint64_t numFdsObjs=0;
FDRStore::FDRStore(std::string filename, std::string fileformat, int isStoreReaderWriter) : binaryinzerocopystream(nullptr)    //Protobuf/json version
{
    storagefilepath = filename;
    encodingtouse = fileformat;

    numFdsObjs++;
    // std::string readerOrWriterStr = (isStoreReaderWriter == STORE_READER) ? std::string("STORE_READER") : std::string("STORE_WRITER");
    // std::cout << "FDRStore[construtor]: numFdsObjs: " << numFdsObjs
    //           << "; storagefilepath: " << storagefilepath
    //           << "; " << readerOrWriterStr
    //           << std::endl;

    // open the instream only for the reader requester
    if (isStoreReaderWriter == STORE_READER) {
        instream.open(storagefilepath);
        if (encodingtouse == "BINARY") {
            binaryinzerocopystream = new google::protobuf::io::IstreamInputStream(&instream);
        }
    }
}

FDRStore::FDRStore(std::string file, std::string fileformat, std::string cClass, std::string ID, std::string pClass) :
        encodingtouse(fileformat), dbLoc(file), paramClass(pClass), compClass(cClass), compID(ID)
{
    DB = NULL;

    int errCode = sqlite3_open(dbLoc.c_str(), &DB);

    if(errCode != SQLITE_OK){	//TODO: Error Handling
		spdlog::warn("Error opening DB");
	}

    //Generate the table name
    std::string tableName = "PVT_" + paramClass + "_" + compClass + "_" + compID;
    char *zErrMsg;

    //Create the table if it does not exist
    std::string sql = "CREATE TABLE if not exists " + tableName + " (TimeStamp INTEGER, ParamID INT, ParamValue TEXT, BootCounter INT);";
    errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);
    if (errCode) {
		spdlog::warn("Error creating table {}: {}", tableName, std::string(sqlite3_errmsg(DB)));
		sqlite3_close(DB);
    }

    //Create the SQL statement used to read values from the DB
    std::string sqlStmt = "SELECT vt.timestamp, vt.paramvalue, vt.paramid, p.datatype from " + tableName + " as vt inner join PVT_Param_Description_Table as p where p.paramId = vt.paramId and p.compclass = '" + compClass + "' AND p.paramClass = '" + paramClass + "';";

    errCode = sqlite3_prepare_v2(DB, sqlStmt.c_str(), -1, &sampleFetchStmt, NULL);

    if(errCode){
		spdlog::warn("Error during execution: {}\nStatement could not be prepared: {}",
                    std::string(sqlite3_errmsg(DB)), sqlStmt); 
		sqlite3_close(DB);
	}

}

FDRStore::~FDRStore()
{
    numFdsObjs--;
    // std::cout << "FDRStore[destrutor]: numFdsObjs: " << numFdsObjs
    //           << "; storagefilepath: " << storagefilepath
    //           << std::endl;
    if (DB && encodingtouse == ENCODING_CHOICE_DB) {
        sqlite3_finalize(sampleFetchStmt);
        sqlite3_close(DB);
    } else {
        if (binaryinzerocopystream) {
            delete binaryinzerocopystream;
        }
        instream.close();

        // std::cout << "2. FDRStore Destructor called" << std::endl;
    }
    // std::cout << "3. FDRStore Destructor called" << std::endl;
}

void FDRStore::append(const google::protobuf::Message &data)
{
    if (encodingtouse == ENCODING_CHOICE_JSON)
    {
        outstream.open(storagefilepath, std::ios_base::app);
        if (outstream.is_open())
        {
            std::string jsonstr;
            google::protobuf::util::MessageToJsonString(data, &jsonstr);
            outstream << jsonstr << std::endl;
            outstream.close();
        }
        else
        {
            // TODO: Error handling
        }
    }
    else if(encodingtouse == ENCODING_CHOICE_BINARY)
    {
        outstream.open(storagefilepath, std::ios::binary | std::ios::app);
        google::protobuf::util::SerializeDelimitedToOstream(data, &outstream);
        outstream.close();
    }
    else
    {
    	// Use the other function for SQLite
    }
}

void FDRStore::append(const fdr_sample_sql &data)
{
    if (encodingtouse == ENCODING_CHOICE_DB){
        int errCode = 0;
    	char *zErrMsg;

        std::string tableName = "PVT_" + paramClass + "_" + compClass + "_" + compID;

        /*
            INSERT INTO <Table> SELECT <TimeStamp>, <ParamID>, <Value>, BootCounter FROM PDT as p where p.CompClass = <compClass> AND p.ParamClass = <paramClass> AND p.ParamName = <paramName>.

            The subquery is used to fetch the param ID corresponding to the compClass, paramClass and paramName.
        */
        //std::string sql = "INSERT INTO " + tableName + " SELECT " + std::to_string(data.timestamp) + ", p.ParamID, \"" + (data.paramType == "Uint64" ? std::to_string(data.paramValueInt64) : data.paramValueString) + "\"," + "0" /* TODO: Boot counter */ + " FROM PDT as p where p.CompClass = '" + compClass + "' AND p.ParamClass = '" + paramClass + "' AND p.ParamName = '" + data.paramName + "'";

        std::string sql = "INSERT INTO " + tableName + " (TimeStamp, ParamID, ParamValue)" + " VALUES (" + std::to_string(data.timestamp) + ", " + std::to_string(data.paramID) + ", \"" + (data.paramType == "Uint64" ? std::to_string(data.paramValueInt64) : data.paramValueString) + "\"" + ")";
        
        errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);

		if(errCode){
			spdlog::warn("Error during execution : {}\nInsert failed : {}",
                         zErrMsg, sql);
			sqlite3_close(DB);
			return;
		}
		else{
			//std::cout << "Insert completed" << std::endl;
		}
    }
}

int FDRStore::readnext(google::protobuf::Message *datap){
    // std::cout << "readnext:encodingtouse: " << encodingtouse << "; storagefilepath: " << storagefilepath << std::endl;
    if (encodingtouse == ENCODING_CHOICE_JSON)
    {
        if (instream.is_open())
        {
            std::string line;
            if (std::getline(instream, line))
            {
                google::protobuf::util::JsonStringToMessage(line, datap);
                return 1;
            }
        }
        // std::cout << "readnext: ENCODING_CHOICE_JSON: instream not open: storagefilepath: " << storagefilepath << std::endl;
        return 0;
    }
    else if(encodingtouse == ENCODING_CHOICE_BINARY)
    {
        if (instream.is_open())
        {
            bool clean_eof = true;
            auto ret = google::protobuf::util::ParseDelimitedFromZeroCopyStream(datap, binaryinzerocopystream, &clean_eof);

            if (ret == false)
            {
                if (clean_eof)
                {
                    return 0; // clean end of file
                }
                else
                {
                    spdlog::warn("Binary file seems corrupted");
                    return 0; // Unexpected end of file
                }
            }
            return 1; //Successfull read of a record and more left to read
        }
    }
    else{
        // Use the other function for SQLite
    }
    // std::cout << "readnext: default: failure: return 0: encodingtouse: " << encodingtouse
    //           << "; storagefilepath: " << storagefilepath 
    //           << std::endl;
    return 0;
}

// returns 0 if EOF reached else 1
int FDRStore::readnext(fdr_sample_sql *datap)
{
    int errCode = sqlite3_step(sampleFetchStmt);

    /*
        Error Code SQLITE_ROW means a row was returned succesfully.
        The returned columns are Timestamp, ParamValue, ParamName, DataType
    */

    if (errCode == SQLITE_ROW){
        // vt.timestamp, vt.paramvalue, p.paramname, p.datatype
        datap->timestamp = sqlite3_column_int64(sampleFetchStmt,0);

        datap->paramID = sqlite3_column_int(sampleFetchStmt,2);
        std::string datType =  std::string(reinterpret_cast<const char*>(sqlite3_column_text(sampleFetchStmt,3)));

        datap->paramType = datType == "Integer" ? "Uint64" : "String";
        
        std::string value = std::string(reinterpret_cast<const char*>(sqlite3_column_text(sampleFetchStmt,1)));
        if (datap->paramType == "Uint64"){
            std::istringstream strToNum(value);
            strToNum >> datap->paramValueInt64;
        }
        else{
            datap->paramValueString = value;
        }
        return 1;   //Successfully read a record and not reached end of file yet
    }
    else{
        return 0;   //Reached EOF
    }
}

void FDRStore::deleteRecords(){
    std::string tableName = "PVT_" + paramClass + "_" + compClass + "_" + compID;
    std::string sql = "DELETE FROM " + tableName + ";";

    char *zErrMsg;
    int errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);

    if(errCode){
        spdlog::warn("Error during execution : {}\nDelete failed : {}", zErrMsg, sql);
        sqlite3_close(DB);
    }
    else{
        //std::cout << "Delete completed" << std::endl;
    }
}

/*int FDRStore::getLatest(fdr_sample *datap, std::string infoID){

    std::string tableName = "PVT_" + paramClass + "_" + compClass + "_" + compID;
    std::string sqlStmt = "SELECT TOP 1 vt.timestamp, vt.paramvalue, p.paramname, p.datatype from " + tableName + " as vt inner join PDT as p where p.paramId = vt.paramId and p.compclass = '" + compClass + "' AND p.paramClass = '" + paramClass + "' AND p.paramname = '" + infoID +"' ORDER BY vt.timestamp DESC;";

    sqlite3_stmt *tempStmt;
    int errCode = sqlite3_prepare_v2(DB, sqlStmt.c_str(), -1, &tempStmt, NULL);
    errCode = sqlite3_step(tempStmt);

    if (errCode == SQLITE_ROW){
        // vt.timestamp, vt.paramvalue, p.paramname, p.datatype
        datap->timestamp = sqlite3_column_int64(tempStmt,0);

        datap->paramName = std::string(reinterpret_cast<const char*>(sqlite3_column_text(tempStmt,2)));
        std::string datType =  std::string(reinterpret_cast<const char*>(sqlite3_column_text(tempStmt,3)));

        datap->paramType = datType == "Integer" ? "Uint64" : "String";
        
        std::string value = std::string(reinterpret_cast<const char*>(sqlite3_column_text(tempStmt,1)));
        if (datap->paramType == "Uint64"){
            std::istringstream strToNum(value);
            strToNum >> datap->paramValueInt64;
        }
        else{
            datap->paramValueString = value;
        }
        return 1;   //Successfully read a record
    }
    else{
        return 0;
    }   
}*/

void FDRStore::createStatesTable(){
    std::string tableName = "Stats_" + paramClass + "_" + compClass + "_" + compID;
    char *zErrMsg;
    //Create the table if it does not exist
    std::string sql = "CREATE TABLE if not exists " + tableName + " (ParamID INT, FromTimeStamp INTEGER, ToTimeStamp INTEGER, Num INTEGER, Min INTEGER, Max INTEGER, Average INTEGER);";
    int errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);

    if(errCode != SQLITE_OK){	//TODO: Error Handling
        spdlog::warn("Error creating states table");
    }
}

void FDRStore::appendStat(const fdr_stat_sql &data){
    int errCode = 0;
    char *zErrMsg;

    std::string tableName = "Stats_" + paramClass + "_" + compClass + "_" + compID;

    /*
        INSERT INTO <Table> SELECT p.paramID, <FromTimeStamp>, <ToTimeStamp>, <Num>, <Min>, <Max>, <Average>
        FROM PDT as p where p.CompClass = <compClass> AND p.ParamClass = <paramClass> AND
        p.ParamName = <paramName>.

        The subquery is used to fetch the param ID corresponding to the compClass, paramClass and paramName.
    */
    //std::string sql = "INSERT INTO " + tableName + " SELECT p.ParamID, " + std::to_string(data.fromtime) + ", " + std::to_string(data.totime) + ", " + std::to_string(data.numsamples) + ", " + std::to_string(data.min) + ", " + std::to_string(data.max) + ", " + std::to_string(data.avg) + " FROM PDT as p where p.CompClass = '" + compClass + "' AND p.ParamClass = '" + paramClass + "' AND p.ParamName = '" + data.paramName + "'";

    std::string sql = "INSERT INTO " + tableName + " (ParamID, FromTimeStamp, ToTimeStamp, Num, Min, Max, Average)" + " VALUES (" + std::to_string(data.paramID) + ", " + std::to_string(data.fromtime) + ", " + std::to_string(data.totime) + ", " + std::to_string(data.numsamples) + ", " + std::to_string(data.min) + ", " + std::to_string(data.max) + ", " + std::to_string(data.avg) + ");";
    
    errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);

    if(errCode){
        spdlog::warn("Error during execution : {}\nInsert failed : {}", zErrMsg, sql);
        sqlite3_close(DB);
        return;
    }
    else{
        //std::cout << "Insert completed" << std::endl;
    }
}

std::string FDRStore::getStoreFilePath()
{
    return storagefilepath;
}