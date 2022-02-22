
#include <string>
#include "fdr_logs_schema.pb.h"
#include "fdr_store.hpp"

#include "fdr_logs_schema.pb.h"
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <google/protobuf/io/zero_copy_stream.h>

FDRStore::FDRStore(std::string filename, std::string fileformat)
{
    storagefilepath = filename;
    encodingtouse = fileformat;

    instream.open(storagefilepath);
}

FDRStore::~FDRStore()
{
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
    else
    {
        outstream.open(storagefilepath, std::ios::binary | std::ios::app);
        google::protobuf::util::SerializeDelimitedToOstream(data, &outstream);
        outstream.close();
    }
}

// returns 0 if EOF reached else 1
int FDRStore::readnext(google::protobuf::Message *datap)
{
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
    else
    {
        if (instream.is_open())
        {
            google::protobuf::io::ZeroCopyInputStream *binaryinzerocopystream = new google::protobuf::io::IstreamInputStream(&instream);

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
                    std::cout << "Binary file seems corrupted" << std::endl;
                    return 0; // Unexpected end of file
                }
            }
            return 1; //Successfull read of a record and more left to read
        }
    }
    return 0;
}
