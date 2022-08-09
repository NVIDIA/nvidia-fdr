#include <iostream>
#include <sqlite3.h>
#include <chrono>
#include <ctime>
#include <fstream>
#include "stdlib.h"
#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "sys/times.h"

#include "sample.pb.h"

struct sample {
    int a;
    std::string b;
    std::string c;
    int d;
};


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// https://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process


static clock_t lastCPU, lastSysCPU, lastUserCPU;
static int numProcessors;

void init(){
    FILE* file;
    struct tms timeSample;
    char line[128];

    lastCPU = times(&timeSample);
    lastSysCPU = timeSample.tms_stime;
    lastUserCPU = timeSample.tms_utime;

    file = fopen("/proc/cpuinfo", "r");
    numProcessors = 0;
    while(fgets(line, 128, file) != NULL){
        if (strncmp(line, "processor", 9) == 0) numProcessors++;
    }
    fclose(file);
}

double getCurrentValue(){
    struct tms timeSample;
    clock_t now;
    double percent;

    now = times(&timeSample);
    if (now <= lastCPU || timeSample.tms_stime < lastSysCPU ||
        timeSample.tms_utime < lastUserCPU){
        //Overflow detection. Just skip this value.
        percent = -1.0;
    }
    else{
        percent = (timeSample.tms_stime - lastSysCPU) +
            (timeSample.tms_utime - lastUserCPU);
        percent /= (now - lastCPU);
        percent /= numProcessors;
        percent *= 100;
    }
    lastCPU = now;
    lastSysCPU = timeSample.tms_stime;
    lastUserCPU = timeSample.tms_utime;

    return percent;
}

void dispTime(std::chrono::duration<double> elapsed, std::string context){
    std::cout << context << " : " << elapsed.count() << "s" << std::endl;
}

/*void execTrans(sqlite3* DB, int index){
    sqlite3_exec(DB, "BEGIN TRANSACTION", NULL, NULL, NULL);

    std::string sql = "CREATE TABLE TESTTAB" + std::to_string(index) + " (ID INT PRIMARY KEY NOT NULL,NAME TEXT NOT NULL,EXTRA TEXT);";
    sqlite3_exec(DB,sql.c_str(),NULL,0,NULL);

    int numIns = 10;
    for(int i = 0 ; i < numIns ; i++){
        sql = "INSERT INTO TESTTAB" + std::to_string(index) + " VALUES (" + std::to_string(i) + ",'n1','ab')";
        sqlite3_exec(DB,sql.c_str(),NULL,0,NULL);
    }

    sqlite3_exec(DB, "END TRANSACTION", NULL, NULL, NULL);
}

void execTrans2(sqlite3* DB, int index){
    sqlite3_exec(DB, "BEGIN TRANSACTION", NULL, NULL, NULL);

    int numIns = 50;
    std::string sql = "INSERT INTO TEMPTAB1 VALUES ";
    for(int i = 0 ; i < numIns ; i++){
        //sql += "(" + std::to_string(i+index*numIns) + ",'n1','" + LONG_STRING + "')"  + (i == numIns - 1 ? "" : ", ");
    }

    sqlite3_exec(DB,sql.c_str(),NULL,0,NULL);

    sqlite3_exec(DB, "END TRANSACTION", NULL, NULL, NULL);
}*/


/* 
    numTrans delay measureUsage textSize insPerTrans Type[0-SQLite,1-Binary,2-Protobuf]
*/

int main(int argc, char **argv){

    //sqlite3* DB;
	int errCode = 0;
    init();

    int numTransac = 200;
    int transDelay = 10;
    bool measureUsage = false;
    int textSize = 100;
    int insPerTrans = 20;
    int type = 0;

    if (argc > 1){
        numTransac = std::strtol(argv[1], NULL, 10);
    }
    if (argc > 2){
        transDelay = std::strtol(argv[2], NULL, 10);
    }
    if (argc > 3){
        measureUsage = std::strtol(argv[3], NULL, 10);
    }
    if (argc > 4){
        textSize = std::strtol(argv[4], NULL, 10);
    }
    if (argc > 5){
        insPerTrans = std::strtol(argv[5], NULL, 10);
    }
    if (argc > 6){
        type = std::strtol(argv[6], NULL, 10);
    }

    std::cout << numTransac << " " << transDelay << " " << measureUsage << " " << textSize << " " << insPerTrans << std::endl;

    // Opening/Creaing a DB

    auto start = std::chrono::system_clock::now();

	errCode = sqlite3_open("test.db", &DB);

    auto end = std::chrono::system_clock::now();

    //dispTime(end - start, "Creation/Open time");


    //Creating tables

    char *zErrMsg = 0;
    int numTab = 2;
    const std::string sqlEnd = " (ID INT PRIMARY KEY NOT NULL,NAME TEXT NOT NULL, EXTRA TEXT, EX INT);";

    start = std::chrono::system_clock::now();

    sqlite3_exec(DB, "BEGIN TRANSACTION", NULL, NULL, NULL);

    for(int i = 0 ; i < numTab ; i++){
        std::string sql = "CREATE TABLE TEMPTAB" + std::to_string(i) + sqlEnd;
        errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);
    }

    sqlite3_exec(DB, "COMMIT", NULL, NULL, NULL);

    end = std::chrono::system_clock::now();

    std::cout<< std::endl << "Total time for " << numTab << " table creations : " << std::chrono::duration<double>(end - start).count() <<"s"<< std::endl;

    dispTime(std::chrono::duration<double>(end - start) / numTab, "Average Table Creation time");


    //Insert statements

    /*
    const std::string insertStart = "INSERT INTO TEMPTAB0 VALUES (";
    const std::string insertEnd = ",'n1','ab')";
    int numQueries = 1000;

    start = std::chrono::system_clock::now();

    sqlite3_exec(DB, "BEGIN TRANSACTION", NULL, NULL, NULL);

    for(int i = 0 ; i < numQueries ; i++){
        std::string sql = insertStart + std::to_string(i) + insertEnd;
        errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);
    }

    sqlite3_exec(DB, "COMMIT", NULL, NULL, NULL);

    end = std::chrono::system_clock::now();

    //std::cout << std::endl << "CPU Usage : " << getCurrentValue() << "%" << std::endl;

    std::cout<< std::endl << "Total time for " << numQueries << " inserts : " << std::chrono::duration<double>(end - start).count() <<"s" << std::endl;

    dispTime(std::chrono::duration<double>(end - start) / numQueries, "Insert time");


    //Individual Deletes

    const std::string del1 = "DELETE FROM TEMPTAB0 WHERE ID = ";
    int numDels = 500;

    start = std::chrono::system_clock::now();

    sqlite3_exec(DB, "BEGIN TRANSACTION", NULL, NULL, NULL);

    for(int i = 0 ; i < numDels ; i++){
        std::string sql = del1 + std::to_string(i);
        errCode = sqlite3_exec(DB,sql.c_str(),NULL,0,&zErrMsg);
    }

    sqlite3_exec(DB, "COMMIT", NULL, NULL, NULL);

    end = std::chrono::system_clock::now();

    //std::cout << std::endl << "CPU Usage : " << getCurrentValue() << "%" << std::endl;

    std::cout<< std::endl << "Total time for " << numDels << " deletes : " << std::chrono::duration<double>(end - start).count() <<"s"<< std::endl;

    dispTime(std::chrono::duration<double>(end - start) / numQueries, "Individual delete time");


    //Group delete

    const std::string del2 = "DELETE FROM TEMPTAB0";

    start = std::chrono::system_clock::now();

    errCode = sqlite3_exec(DB,del2.c_str(),NULL,0,&zErrMsg);

    end = std::chrono::system_clock::now();

    //std::cout << std::endl << "CPU Usage : " << getCurrentValue() << "%" << std::endl;

    dispTime(std::chrono::duration<double>(end - start) / numQueries, "Group delete time");


    int numIns2 = 1000;
    const std::string ins2 = "INSERT INTO TEMPTAB0 VALUES ";
    std::string query = ins2;
    for(int i = 0 ; i < numIns2 ; i++){
        query += "(" + std::to_string(i) + ",'n2','abcd')" + (i == numIns2 - 1 ? "" : ", ");
    }

    start = std::chrono::system_clock::now();

    sqlite3_exec(DB, "BEGIN TRANSACTION", NULL, NULL, NULL);

    errCode = sqlite3_exec(DB,query.c_str(),NULL,0,&zErrMsg);

    sqlite3_exec(DB, "COMMIT", NULL, NULL, NULL);

    end = std::chrono::system_clock::now();

    dispTime(std::chrono::duration<double>(end - start), "Group Insert time");

    std::cout << std::endl << "CPU Usage : " << getCurrentValue() << "%" << std::endl;
    */

    init();

    int numTrans = numTransac;
    int delayus = transDelay;
    double maxUsage = 0;
    double avgUsage = 0;
    int n = 0;
    int numIns = insPerTrans;

    std::string midString = ",'n1','";
    for(int i = 0 ; i < textSize ; i++){
        midString += 'a';
    }
    midString += "',1234)";

    start = std::chrono::system_clock::now();
    
    if (type == 0){
        std::cout << "SQL TEST" << std::endl;
        for(int i = 0 ; i < numTrans ; i++){
            sqlite3_exec(DB, "BEGIN TRANSACTION", NULL, NULL, NULL);
            std::string sql = "INSERT INTO TEMPTAB1 VALUES ";
            for(int j = 0 ; j < numIns ; j++){
                sql += "(" + std::to_string(j+i*numIns) + midString + (j == numIns - 1 ? "" : ", ");
            }
            sqlite3_exec(DB,sql.c_str(),NULL,0,NULL);
            //std::cout<< sql.c_str() <<std::endl;
            sqlite3_exec(DB, "COMMIT", NULL, NULL, NULL);

            if (measureUsage){
                double currUsage = getCurrentValue();
                if (currUsage >= 0){
                    n++;
                    avgUsage += currUsage;
                    maxUsage = maxUsage < currUsage ? currUsage : maxUsage;
                }
            }
            usleep(delayus);
        }
    }
    else if(type == 1){
        std::cout << "BINARY FILE(NO PROTOBUF) TEST" << std::endl;
        std::ofstream op("test", std::ios_base::out /*| std::ios_base::binary*/);
        struct sample temp;
        temp.b = "n1";
        temp.c = midString;
        temp.d = 328237;
        for(int i = 0 ; i < numTrans ; i++){
            temp.a = i;
            op << temp.a << " " << temp.b << " " << temp.c << " " << temp.d << std::endl;
            //op.write((char *) &temp, sizeof(sample));
            //std::cout << "OP:" << temp.a << " " << temp.b << " " << temp.c << " " << temp.d << std::endl;
            usleep(delayus);
        }
        op.close();
    }
    else if(type == 2){
        std::cout << "PROTOBUF TEST" << std::endl;
        test::sample2 temp1;
        temp1.set_text("n1");
        temp1.set_extra(midString);
        temp1.set_ex(328237);
        std::ofstream op("testProto.bin", std::ios_base::out | std::ios_base::binary);
        for(int i = 0 ; i < numTrans ; i++){
            temp1.set_id(i);
            //op << temp.a << " " << temp.b << " " << temp.c << " " << temp.d << std::endl;
            temp1.SerializeToOstream(&op);
            //std::cout << "OP:" << temp1.id() << " " << temp1.text() << " " << temp1.extra() << " " << temp1.ex() << std::endl;
            usleep(delayus);
        }
        op.close();
    }

    end = std::chrono::system_clock::now();

    //std::cout << "OP" << std::endl;

    /*std::ifstream op("testProto.bin", std::ios_base::in | std::ios_base::binary);
    test::sample2 temp2;
    for(int i = 0 ; i < numTrans ; i++){
        //op >> temp.a >> temp.b >> temp.c >> temp.d;
        if(!temp2.ParseFromIstream(&op))
            std::cout <<"Error reading" << std::endl;
        std::cout << "OP:" << temp2.id() << " " << temp2.text() << " " << temp2.extra() << " " << temp2.ex() << std::endl;
    }
    op.close();*/

    std::cout << std::endl << "Total time : " << std::chrono::duration<double>(end - start).count() << "s" << std::endl;
    std::cout << "Number of transactions : " << numTrans << std::endl;
    std::cout << "Transactions per second : " << numTrans / std::chrono::duration<double>(end - start).count() << std::endl;
    if (measureUsage){
        std::cout << "CPU Average Usage : " << avgUsage/n << "%" << std::endl;
        std::cout << "CPU Max Usage : " << maxUsage << "%" << std::endl;
        std::cout << "Number of samples : " << n << std::endl;
    }
    std::cout << "Size of text : " << textSize << std::endl;

    sqlite3_close(DB);

}