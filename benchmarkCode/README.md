# Dev Setup

sudo apt install protobuf-compiler

sudo apt install libsqlite3-dev

sudo apt install libsystemd-dev

# Build Instructions

cmake .

make

# Running the code

./benchTest numTrans delay measureUsage textSize insPerTrans Type

numTrans : For SQL, number of transactions. For others, number of inserts.

delay: Delay between transactions in microseconds.

measureUsage: 1/0 to measure CPU usage or not. Only for SQL. 

textSize: Approximate size of each record to be inserted.

insPerTrans: Only for SQL. Number of inserts per transaction.

Type: 0-SQLite, 1-Binary, 2-Protobuf

# About the code

For SQL, two tables are created, TEMPTAB0 and TEMPTAB1. Only TEMPTAB1 is currently used for the test. A transaction is performed by running "BEGIN TRANSACTION", followed by *insPerTrans* number of inserts, followed by a "COMMIT". Then, there is a small delay as specified by the *delay* param. This reduces the transactions per second, which in turn reduces the CPU usage. This is then repeated for a total of *numTrans* times.

For Protobuf and Binary file, each "transaction" is a single insert into the file.

Each insert statement inserts a record with 4 values, a timestamp, the param type which is "String", the param name which is "Param Test" and the value, which is a string of size *textSize*.

At the end, the program displays the total time taken, total number of transactions and the transactions per second. You can get the CPU usage by running the top command simultaneously and checking the average CPU usage.

The *measureUsage* param only works with SQL code, but it is not very accurate, so use top for CPU usage instead.