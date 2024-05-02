import yaml
import sys

def parse_yaml(ppf_file_name):
    with open(ppf_file_name, "r") as f:
        ppf = yaml.safe_load(f)
    return ppf

def create_busctl_cmds(ppf):
    idx = 0
    for section in ppf["Sections"]:
        for component in section["Components"]:
            for ig in component["InfoGroups"]:
                for il in ig["InfoList"]:                 
                    Service = il["DbusParams"]["Service"]
                    ObjectPath = il["DbusParams"]["ObjectPath"]
                    Interface = il["DbusParams"]["Interface"]
                    Property = il["DbusParams"]["Property"]
                    for param in component["Params"]:
                        ObjectPath = ObjectPath.replace(f'${param["name"]}', str(param["value"]))
                    print(f"echo -n '{idx} ' ; ", f"busctl get-property {Service} {ObjectPath} {Interface} {Property} ; ", "#", section["ID"], component["ID"], ig["ID"], il["ID"], il["ParamID"])
                    idx += 1

if __name__ == "__main__":
    ppf = parse_yaml(sys.argv[1])
    create_busctl_cmds(ppf)