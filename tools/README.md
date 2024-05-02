## Validate FDR data

### Validating the datatype in the fdr_ppf_vulcan.yaml
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

### Validating the FDR logs data