#pragma once

#include <string>
#include <fstream>
#include "fdr_logs_schema.pb.h"
using namespace fdr;

const std::string ENCODING_CHOICE_JSON = "JSON";
const std::string ENCODING_CHOICE_BINARY = "BINARY";

class FDRStore
{
private:
    /* data */
    std::string storagefilepath;
    std::string encodingtouse;
    std::ifstream instream;
    std::ofstream outstream;
public:
    FDRStore(std::string filename, std::string fileformat);
    ~FDRStore();

    void append(const google::protobuf::Message &data); // append data to file

    int readnext(google::protobuf::Message *datap); // read data at current pointer in file and advance pointer to next

    void rewind(); // reset pointer in file to begining of file
};
