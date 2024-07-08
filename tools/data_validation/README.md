# Validating the datatype in the fdr_ppf_vulcan.yaml
For each parameter in the PPF, we will run the corresponding busctl command and compare the busctl output with the datatype in the PPF. 

0. Get the copy of **nvidia-fdr/platforms/fdr_ppf_vulcan.yaml**
1. Generate busctl commands file
    - python3 create_busctl_cmds.py `<path to fdr_ppf_vulcan.yaml>` > `<output file>` 
    - python3 create_busctl_cmds.py ./fdr_ppf.yaml > busctl.cmds 

2. Copy the commands file (**busctl.cmds**) to HMC.
3. On HMC, run
    - `sh ./busctl.cmds &> busctl.results`
4. Copy this results file (**busctl.results**) back to your local system.
5. Compare the datatypes in **PPF** and **busctl.results**
    - python3 check_datatypes.py `<fdr_ppf_vulcan.yaml>` `busctl.results`  `busctl.cmds` > datatype_diff.out
    - python3 check_datatypes.py ./data/fdr_ppf.yaml ./data/busctl.results.3  busctl.cmds > datatype_diff.out

# Telemetry coverage of FDR dump
Check if the entries in the telemetry catalog are present in the FDR dump.

Options:
- t: path to telemetry catalog file
- u: path to URI expansion ranges file
- e: path to exempt list yaml file
- f: path to FDR dump directory
- p: platform [Optional] [Default = "Vulcan"]
    - Supported platforms: Vulcan, Viking, hgxb(use the other name), Multi-Socket CG1, GH200 NVL, GB200 NVL, Miranda, Ranger, SMC-XYZ, GB200 NVL BMC
```
python3 check_fdr_telemetry_coverage.py \
-t "/.../nvidia-telemetry-agent/Telemetry Catalog (WIP Copy) - Consolidated Customer View (Read Only).csv" \
-u "/.../nvidia-telemetry-agent/Telemetry Catalog (WIP Copy) - URI Expansion Ranges.csv" \
-e "./platform/exempt_telemetry_list_vulcan.yaml" \
-f "/.../nvidia-fdr/fdrtool/fdr_logs/fdr" \
-p "Vulcan" > fdr_telemetry_coverage_output.txt
```
# Validating the FDR dump
## Setup
1. Create a ssh-key
    - ssh-keygen
2. Copy the ssh-key to Host
    -  ssh-copy-id -i <ssh public key> user@IP
3. Add ssh-id to authentication agent
    - eval \`ssh-agent -s\`; ssh-add 
4. Enable SSH port-forwarding to BMC (Required for fdrtool and telemetry-agent)
    - ssh -fNT -L 18888:192.168.31.1:80 user@BMC_IP -p 22
5. Clone nvidia-fdr repository to get the fdrtool.
    - git clone ssh://git@gitlab-master.nvidia.com:12051/dgx/nvidia-fdr.git
6. Clone nvidia-telemetry-agent repository
    - git clone ssh://git@gitlab-master.nvidia.com:12051/dgx/nvidia-telemetry-agent.git

## Validate sensor data
1. bash fdr_validation.sh \
-h user@host_ip \
-n <path/to/capture_sensor_reading.sh; should be in pwd by default> \
-w <path/to/workload.py; should be in pwd by default> \
-o <path/to/output/directory>
2. collect fdr dump
    - Use fdrtool to collect and decode the dump
    - FDR dump should be decoded with -kn option.
3. python3 compare_nvidia-smi_fdr_dump.py \
-n <path/to/nvidia-smi.output; should be in ./tmp by default> \
-f <path/to/fdr/dump>
-o <path/to/output/directory>

## Validate static data
1. collect the fdr dump (not required if already downloaded in above steps)
    - Use fdrtool to collect and decode the dump
    - FDR dump should be decoded with -kn option.
2. collect the RedFish data using Nvidia Telemetry Agent
3. python3 compare_telemetry-agent_fdr_dump.py \
-t <path/to/latest_results.csv; This is the output of the Telemetry Agent> \
-f <path/to/fdr_logs/fdr; path to latest decoded dump>

### Output of the compare_telemetry-agent_fdr_dump script
- Missing ID from FDR logs: If the parameter is present in the Telemetry-Agent's output (Telemetry catalog) but not in FDR's others.dat file.
- Match at: The value of the parameter in the Telemetry-Agent's output matches with the parameter value in FDR's others.dat file.  
- Mismatch at: The value of the parameter in the Telemetry-Agent's output do not match with the parameter value in FDR's others.dat file.  

### Note
- The script will only consider the entries in the Telemetry-Agent's output that do not have "Sensor." in it.
- The script will only consider the entries in the FDR's others.dat file.
- The script changes True and False to 1 and 0 respectively.
- The script only compares the integer part of floating point numbers.
