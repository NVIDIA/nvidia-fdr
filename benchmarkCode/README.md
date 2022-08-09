./a.out numTrans delay measureUsage textSize insPerTrans Type

numTrans : For SQL, number of transactions. For others, number of inserts.
delay: Delay between transactions in microseconds.
measureUsage: 1/0 to measure usage or not. Only for SQL. 
textSize: Approximate size of each record to be inserted.
insPerTrans: Only for SQL. Number of inserts per transaction.
Type: 0-SQLite, 1-Binary, 2-Protobuf