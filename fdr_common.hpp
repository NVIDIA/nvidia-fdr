#pragma once

#include <ostream>
#include <string>
#include <stdlib.h>

#define ONE_MB              1024UL*1024UL

enum {
    FDR_SUCCESS             = 0,
    FDR_ERR_GENFAILURE

};

typedef struct CommandResult {
    std::string cmdOutput;
    int cmdExitstatus;
} CommandResult_t;

CommandResult_t exec(const char *cmd);
