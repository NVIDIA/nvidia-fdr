
#include <string>
#include <sstream>
#include "fdr_store.hpp"

#include "fdr_logs_schema.pb.h"
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <google/protobuf/io/zero_copy_stream.h>


FDRStore::FDRStore(std::string filename, std::string fileformat)    //Protobuf/json version
{
    storagefilepath = filename;
    encodingtouse = fileformat;

    instream.open(storagefilepath);
}

FDRStore::FDRStore(std::string file, std::string fileformat, std::string pClass, std::string cClass, std::string ID) :
        encodingtouse(fileformat), dbLoc(file), paramClass(pClass), compClass(cClass), compID(ID)
{
    DB = NULL;

    int errCode = sqlite3_open(dbLoc.c_str(), &DB);

    if(errCode != SQLITE_OK){	//TODO: Error Handling
		std::cout << "Error opening DB" << std::endl;
	}

    //Generate the table name
    std::string tableName = "PVT_" + paramClass + "_" + compClass + "_" + compID;
    char *zErrMsg;

    //Create the table if it does not exist
    std::string sql = "CREATE TABLE if not exists " + tableName + " (TimeStamp INTEGER, ParamID INT, ParamValue TEXT, BootCounter INT);";
    errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);

    //Create the SQL statement used to read values from the DB
    std::string sqlStmt = "SELECT vt.timestamp, vt.paramvalue, p.paramname, p.datatype from " + tableName + " as vt inner join PDT as p where p.paramId = vt.paramId and p.compclass = '" + compClass + "' AND p.paramClass = '" + paramClass + "';";

    errCode = sqlite3_prepare_v2(DB, sqlStmt.c_str(), -1, &sampleFetchStmt, NULL);

    if(errCode){
		std::cout << "Error during execution : " << std::string(sqlite3_errmsg(DB)) << std::endl << "Statement could not be prepared : " << sqlStmt << std::endl;
		sqlite3_close(DB);
	}

}

FDRStore::~FDRStore()
{
    if (DB && encodingtouse == ENCODING_CHOICE_DB){
        sqlite3_finalize(sampleFetchStmt);
        sqlite3_close(DB);
    }
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
            INSERT INTO <Table> SELECT <TimeStamp>, p.paramID, <Value>, BootCounter FROM PDT as p where p.CompClass = <compClass> AND p.ParamClass = <paramClass> AND p.ParamName = <paramName>.

            The subquery is used to fetch the param ID corresponding to the compClass, paramClass and paramName.
        */

        std::string sql = "INSERT INTO " + tableName + " SELECT " + std::to_string(data.timestamp) + ", p.ParamID, \"" + (data.paramType == "Uint64" ? std::to_string(data.paramValueInt64) : data.paramValueString) + "\"," + "0" /* TODO: Boot counter */ + " FROM PDT as p where p.CompClass = '" + compClass + "' AND p.ParamClass = '" + paramClass + "' AND p.ParamName = '" + data.paramName + "'";

        errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);

		if(errCode){
			std::cout << "Error during execution : " << zErrMsg << std::endl << "Insert failed : " << sql << std::endl;
			sqlite3_close(DB);
			return;
		}
		else{
			//std::cout << "Insert completed" << std::endl;
		}
    }
}

int FDRStore::readnext(google::protobuf::Message *datap){
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
        return 0;
    }
    else if(encodingtouse == ENCODING_CHOICE_BINARY)
    {
        if (instream.is_open())
        {
            google::protobuf::io::ZeroCopyInputStream *binaryinzerocopystream = new google::protobuf::io::IstreamInputStream(&instream);

            bool clean_eof = true;
            auto ret = google::protobuf::util::ParseDelimitedFromZeroCopyStream(datap, binaryinzerocopystream, &clean_eof);

            delete binaryinzerocopystream;

            if (ret == false)
            {
                if (clean_eof)
                {
                    return 0; // clean end of file
                }
                else
                {
                    std::cout << "Binary file seems corrupted" << std::endl;
                    return 0; // Unexpected end of file
                }
            }
            return 1; //Successfull read of a record and more left to read
        }
    }
    else{
        // Use the other function for SQLite
    }
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

        datap->paramName = std::string(reinterpret_cast<const char*>(sqlite3_column_text(sampleFetchStmt,2)));
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
        std::cout << "Error during execution : " << zErrMsg << std::endl << "Delete failed : " << sql << std::endl;
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
        std::cout << "Error creating states table" << std::endl;
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

    std::string sql = "INSERT INTO " + tableName + " SELECT p.ParamID, " + std::to_string(data.fromtime) + ", " + std::to_string(data.totime) + ", " + std::to_string(data.numsamples) + ", " + std::to_string(data.min) + ", " + std::to_string(data.max) + ", " + std::to_string(data.avg) + " FROM PDT as p where p.CompClass = '" + compClass + "' AND p.ParamClass = '" + paramClass + "' AND p.ParamName = '" + data.paramName + "'";

    errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);

    if(errCode){
        std::cout << "Error during execution : " << zErrMsg << std::endl << "Insert failed : " << sql << std::endl;
        sqlite3_close(DB);
        return;
    }
    else{
        //std::cout << "Insert completed" << std::endl;
    }
}
