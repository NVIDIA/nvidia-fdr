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
We can have 2 versions of Build now:
1. Customer's Build: This version can be release to customers

```bash
make fdrtool
```
2. Developer's Build: This version can generate coverage report & data_validation from the same commandline argument while decoding the dump into JSON.

```bash
make fdrtool CUSTOMER=0
```
The above command(s) builds an "executable" (a shell script actually).

**Note:** 'make clean' will delete the executable, the protobuf compiled file and the fdr_logs directory.

# Python package build (Default / Recommended Way).

Using following command we can package the fdrtool for distribution

```bash
make install
```
Install python package.
```bash
cd dist && pip3 install fdrtool-<fdrtool version>.tar.gz
```

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

#### Copy to/from docker container
##### Step 1 : find you containerID 
```bash
sudo docker ps -a
```
#### Step 2 : docker copy

From local to container
```bash
docker cp /path/to/local/file CONTAINER_ID:/path/in/container
```
From container to local
```bash
docker cp CONTAINER_ID:/path/in/container /path/on/local/machine
```
#### Access Docker container
```bash
docker exec -it CONTAINER_ID /bin/bash
```

# Usage
## Arguments
The tool's configuration options can be provided through commad line arguments and/or configuration file.
The confuration file template is provided as *[config.yaml](https://gitlab-master.nvidia.com/dgx/nvidia-fdr/-/blob/fdrtool/fdrtool/config.yaml)*.

| Arguments | Type | Description |
| ------ | ------ | ------ |
| --use_local, -ul | Boolean | default=False. Set to True if a local tarball should be used instead of collecting FDR dump from HMC using Redfish API. <br /><br /> *Note: This is used for dev/test only. This option will be removed in the future before release.*|
| --local_file, -l | String | Path of the tar archive of FDR logs. <br /><br /> *Note: This is used for dev/test only. This option will be removed in the future before release.*|
| --log_root_dir, -lr | String | default="./fdr_logs/", Decoded dump will be extracted at this path. |
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
| --environment, -e | String | default=UNK. Location of the machine. Field(FIE), Factory(FAC), Unknown(UNK)|
| --generate_cvg_report, -gcr | Boolean | (Only available in Dev Build, and for JSON Decode option only), default=False, Set to True, If we want to generate coverage report along with decode of fdr dump |
| --telemetry_catalog, -tc | String | (Only available in Dev Build, required if --generate_cvg_report is set to True) Provide Path to catalog csv file |
| --telemetry_uri_exp, -tu | String | (Only available in Dev Build, required if --generate_cvg_report is set to True) Provide Path to uri_expansion csv file |
| --platform, -pl | String | (Only available in Dev Build, required if --generate_cvg_report is set to True) Provide Platform name from uri_expansion csv. |
| --exempt_list, -el | String | (Only available in Dev Build), Provide path to exempt_list.yaml file. See ex: nvidia-fdr/platforms/fdr_ppf_vulcan.yaml |
| --fdr_output_dir, -fd | String | (Only available in Dev Build), Provide Path where you want to save the report. Default Path: ./fdr_logs/fdr/coverage_report |
| --value_validation_report, -vvr | Boolean | (Only available in Dev Build, and for JSON Decode option only), default=False, Set to True, If we want to generate value validation report along with decode of fdr dump |
| --telemetry_agent_output, -ltc | String | (Only available in Dev Build, required if --value_validation_report is set to True) Provide Path to telemetry agent's csv file |

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
## Coverage report
Coverage report is a part of fdrtool which verifies the coverage of the decoded logs. Only accessible in dev build. It uses the catalog and uri_expansion csv files to report, how much %age of telemetry data from the platform is covered in the decoded logs, It also reports the missing parameters. It outputs the coverage report in HTML & log format. 

In order to generate coverage report, we need to provide the following arguments:
- --gcr: Generate coverage report.
- --tc: Path to the catalog CSV file.
- --tu: Path to the URI expansion CSV file.
- --el: Path to the exempt list YAML file. 
- --pl: Platform name.
- --fd: Path to the decoded logs directory.

Or simply, update the config.yaml file with the required values. 

Following command will generate the coverage report and store it in the `./fdr_logs/fdr/coverage_report` directory if we already have the decoded logs.

```bash
$./fdrtool -ul --local_file <path/to/dump.tar.xz> --json -ld -gcr -tc "<path/to/catalog.csv>" -tu "<path/to/uri_expansion.csv>" -el "<path/to/exempt_list.yaml>" -pl "<platform_name>" -fd "<path/to/decoded_logs_dir>"
```
## Value validation report
Value validation report is a part of fdrtool which verifies the correctness of the decoded logs. Only accessible in dev build. It does following.
- It evaluates whether values from Redfish and FDR logs match. (Uses Telemetry agent's csv file & decoded logs)
- Handles different data formats and types:
  - Case-insensitive string comparison
  - Boolean values (true/false converted to 1/0)
  - Numeric comparisons
  - Special handling for OpenBMC project values
- Comparison results are categorized as:
  - "Match": Values are identical or equivalent
  - "Partial-Match": Values are numerically close but not identical
  - "Mismatch": Values differ significantly
- Value validation report is generated in HTML & log format.
- We have to consider following points in value validation report:
  - Match Percentage: It is the percentage of parameters which are present in both the telemetry agent's csv file and the decoded logs and values are matching.
  - Coverage Percentage: It is the percentage of parameters which are present in the FDR logs vs telemetry agent's csv file.

To generate the report, we need to provide the following arguments:
  - --vvr: Generate value validation report.
  - --ltc: Path to the telemetry agent's csv file.

```bash
$ ./fdrtool -ul --local_file ./tmp/HMC_UNK_SN1332124050115_10242024_041533.tar.xz --json -ld -vvr -ltc "<path/to/telemetry_agents_output.csv>"
```

#### Exempt list YAML file
Exempt list YAML file contains the list of parameters (TGUID/ParamClass) that are exempted from being considered in coverage and value validation report. It is a YAML file with the following format:
```yaml
# List of all the TGUID/ParamClass in the catalog to be excluded from the coverage check.
# Platform: hgxb_hmc

TGUID:
  - GPU-XID-LOGS
  - NVSWITCH-PORTS-CONNECTED
  - GPU-SYMBOL-ERROR
  - PCIERETIMER-DOWNLINK-HEALTH
  - PCIERETIMER-UPLINK-HEALTH
  - GPU-HEALTH-ROLLUP
  - FPGA-HEALTH-ROLLUP
  - HMC-HEALTH-ROLLUP
  - BASEBOARD-HEALTH-ROLLUP
  - PCIERETIMER-HEALTH
  - PCIERETIMER-HEALTH-ROLLUP
  - PCIERETIMER-LOGS
 
ParamClass:
  - Specs

```
Note: The exempt list YAML file is platform specific.

#### Telemetry agent's csv file
This file contains the telemetry details of the platform based on the telemetry catalog & uri_expansion csv files. This can be generated by running the telemetry agent. Please refer to the telemetry agent's *[README](https://gitlab-master.nvidia.com/dgx/nvidia-telemetry-agent)* for more details.


