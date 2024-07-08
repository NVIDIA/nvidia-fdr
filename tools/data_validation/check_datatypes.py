import yaml
import sys

datatype_map = {
    "s": "string",
    "t": "Uint64",
    "d": "Double",
    "u": "Uint64",
    "x": "Uint64",
    "q": "Uint64",
    "b": "Uint64",
    "(bu)": "Uint64",
}

def parse_yaml(ppf_file_name):
    with open(ppf_file_name, "r") as f:
        ppf = yaml.safe_load(f)
    return ppf

def compare_datatype(ppf, results_file_name, cmd_file_name):
    with open(results_file_name, "r") as f:
        results = f.readlines()
    with open(cmd_file_name, "r") as f:
        cmds = f.readlines()
    idx = 0
    for section in ppf["Sections"]:
        for component in section["Components"]:
            for ig in component["InfoGroups"]:
                for il in ig["InfoList"]:   
                    result_parts = results[idx].split(" ")              
                    cmd_id_r = result_parts[0]
                    datatype = result_parts[1]
                    try:
                        if il["DataType"] != datatype_map[datatype]:
                            print("Mismatch:", idx+1, il["ParamID"], section["ID"], component["ID"], ig["ID"], il["ID"], "Datatype:", il["DataType"],datatype_map[datatype])
                            print(cmds[idx], "\n\n")
                    except Exception as e:
                        # idx+1 == busctl command Line number in the busctl commands file
                        print("Exception:", idx+1, il["ParamID"], section["ID"], component["ID"], ig["ID"], il["ID"], "Datatype:", il["DataType"], datatype)
                        print(cmds[idx], "\n\n")
                    idx += 1
 


if __name__ == "__main__":
    ppf = parse_yaml(sys.argv[1])
    compare_datatype(ppf, sys.argv[2], sys.argv[3])