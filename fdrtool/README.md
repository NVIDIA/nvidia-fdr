# Dev environment setup on build machine without Docker.

These steps were verified on Ubuntu 22.04.

**Python / Ubuntu packages to install before building the image if you are not using DOCKER**

```bash
sudo apt install python3 sqlite3

# If there is any new packages needed, please add it to the requirements.txt file.
pip3 install -r requirements.txt
```
## Protocol file and location
The script uses the same protocol format "fdr_logs_schema.proto" as the nvidia-fdr application. We'll need to specify the path to the protocol file in the Makefile. Open the Makefile and make sure PROTO_DIR variable has the correct path to the protocol file.
```bash
# Directory where fdr_logs_schema exists
PROTO_DIR := ..
```

## Build
The following command builds an "executable" (a shell script actually).
```bash
make fdrtool
```
**Note:** 'make clean' will delete the executable, the protobuf compiled file and the fdr_logs directory.

# Docker image Build
#### How to build new image
Docker file is in root directory of nvidia-fdr make sure you are in the same directory.
```bash
nvidia-fdr$ sudo docker build -t image-name .
```
#### How to run the container

```bash
nvidia-fdr$ sudo docker run -it --name give-a-name-here-to-container image-name
root@ab947ab7d16d:/app/fdrtool# 
```
#### IMP: How to start virtual Environment
```bash
root@ab947ab7d16d:/app/fdrtool# source env_fdr/bin/activate
```

#### Build image or package
fdrtool executable is already build by Dockerfile
##### for package
```bash
$: cd /fdrtool && make install
```
you can find executable in /dist directory.


# Usage
## Arguments
The tool's configuration options can be provided through commad line arguments and/or configuration file.
The confuration file template is provided as *[config.yaml](https://gitlab-master.nvidia.com/dgx/nvidia-fdr/-/blob/fdrtool/fdrtool/config.yaml)*.

| Arguments | Type | Description |
| ------ | ------ | ------ |
| --use_local, -ul | Boolean | default=False. Set to True if a local tarball should be used instead of collecting FDR dump from HMC using Redfish API. <br /><br /> *Note: This is used for dev/test only. This option will be removed in the future before release.*|
| --local_file, -l | String | Path of the tar archive of FDR logs. <br /><br /> *Note: This is used for dev/test only. This option will be removed in the future before release.*|
| --ip, -i | String | IP address of host |
| --username, -u | String | Username for Authentication |
| --password, -p | String | Password for Authentication |
| --json, --influx, --sqlite | Boolean | Set one of these to True for specifying decoded files destination. |
| --key_name, -kn | Boolean | default=False. Set to True if we want to replace the ParamIDs with ParamName in the decoded logs. |
| --influx_url | String | InfluxDB Host URL |
| --influx_org | String | InfluxDB Host Orginization |
| --influx_token | String | Token for authenticating to InfluxDB Host |
| --append, -a | Boolean | default=False. Set to True if we want to append the logs to existing database. If this flag is not provided, any existing database with same name will be deleted first. |
| --length_delimited, -ld | Boolean | default=False. Set to True if we want to decode length-delimited binary. If this flag is not provided, the decoding will be performed for zero-delimited COBS-R binary. <br /><br /> *Note: This is used for dev/test only. This option will be removed in the future before release.*|
| --environment, -e | String | defaule=UNK. Location of the machine. Field(FIE), Factory(FAC), Unknown(UNK)|

## Run
Assuming the fdr application runs locally in HMC, the logs are also saved in HMC. 

Ideally, we would like to retrieve the logs using Redfish API. However, the fdrtool can be run in any machine while 
the API calls to HMC can be made only from BMC. To resolve this issue, we can use port forwarding to redirect the HMC port
to the local machine. To use Redfish API to retrieve the logs, from the local machine, open port 18888 using following command on your local machine.
Note that 192.168.31.1 is HMC's default IP address. Please make sure that port forwarding is enabled in BMC.
```bash
ssh -fNT -L 18888:192.168.31.1:80 <HOSTBMC_USERNAME>@<HOSTBMC_IP> -p 22
```

If the Redfish API isn't available to retrieve the FDR dump from HMC, we can copy the tarball manually from the HMC to the
local machine and give the file path as a cmdline arg.

### Run using config file
```bash
./fdrtool -c config.yaml
```
**Note:** If an arg is specified in more than one place, then commandline values override environment variables which override config file
values which override defaults.

#### Option 1: Retrieve FDR dump using Redfish API
We'll need to provide the host ip address, username and password while running fdrtool. For example, if we're using port-forwarding
as mentioned above, we'll be providing HMC's IP address and authentication information as following. The dump will be stored in the `./tmp/` directory. File format will be `HMC_UNK_SN<serial_number>_MMDDYYYY_HHMMSS.tar.xz`
```bash
./fdrtool --ip http://127.0.0.1:18888 -u root -p 0penBmc --json -ld
```
#### Option 2: Manually copy the FDR dump from HMC
After copying over the FDR dump from HMC to local machine, we'll need to provide the tarball's path while running fdrtool.
For example,
```bash
./fdrtool --local_file '../../binary-logs.tar.gz' --json -ld
```
**Note:** The tool can take either local_file or {ip, username, password} as cmdline arg. That means only one of the above
options can be used at a time.
### To store JSON logs in local drive

```bash
./fdrtool --ip http://127.0.0.1:18888 -u root -p 0penBmc --json -ld
```
**Note:** If the decoding is successful, the json logs will be located under *./fdr_logs* directory. Otherwise, the binary logs
may be found under the same directory given that the API calls or the unzipping of the tar archive is successful.

Add the "-kn" or "--key_name" flag as cmdline arg, or set the value to True in config.yaml to to replace the ParamIDs with ParamName in the decoded logs.
### To write logs to InfluxDB

```bash
./fdrtool --ip http://127.0.0.1:18888 -u root -p 0penBmc --influx -ld
```
**Note:** The binary logs can be found under the *./fdr_logs* directory given that the API calls or the unzipping of the tar archive is successful.
The bucket name will be 'telemetry_db_hgx_serial_num_' + BRD-SERIAL from Baseboard/Inventory.log.
### To write logs to SQLite
The fdrtool creates a db under *./fdr_logs_db/* directory if the *"--append"* flag isn't provided as a cmdline arg. Please be aware that
it'll delete any existing DB with same name under that directory. If we want to keep the existing database and just append the new data
to it, we need to use the *"--append"* flag.
#### To create a new database
```bash
./fdrtool --ip http://127.0.0.1:18888 -u root -p 0penBmc --sqlite -ld
```
#### To append to existing db

```bash
./fdrtool --ip http://127.0.0.1:18888 -u root -p 0penBmc --sqlite --append -ld
```
**Note:** The binary logs can be found under the *./fdr_logs* directory given that the API calls or the unzipping of the tar archive is successful.
The database name will be 'telemetry_db_hgx_serial_num_' + BRD-SERIAL from Baseboard/Inventory.log.
## Browse fdrtool decoded data
### JSON
All the JSON logs will be located under *./fdr_logs* directory in the exact same tree structure sent by FDR.
### InfluxDB
Currently, we're running the InfluxDB instance in a VM as a systemd service. Later, we'll use the cloud.
Go to "http://afsanac-dev-01.nvidia.com:8086/" in any browser. 

#### To use the shell:
In the system where the InfluxDB instance is running, run the following commands.
```bash
# To start the shell
influx v1 shell

# To show all available databases/buckets
SHOW DATADASES

# To select any sprecific database/bucket
USE database_name

# To view all tables/measurements
SHOW MEASUREMENTS

# To view any specific table/measurement
SELECT * FROM tablename

# To exit shell
quit
```
### sqlite
```bash
sudo apt install sqlite3

cd fdr_logs_db

# Suppose, the db name is telemetry_db_hgx_serial_num_001.db
sqlite3 telemetry_db_hgx_serial_num_001.db

# To see all the created tables
.tables

# To see all schemas
.schemas

# To see any specific table's schema
.schema tablename

# To see data in a table
SELECT * FROM tablename;

# To see all data  from PDT (Parameters Description Table)
SELECT * FROM PDT;

# To see all data in CDV (Combined Data View)
SELECT * FROM CDV;

# To see all data in CSV (Combined Stats View)
SELECT * FROM CSV;
```
