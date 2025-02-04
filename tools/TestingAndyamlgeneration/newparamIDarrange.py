import sys
import re

def process_file(filename):
    try:
        with open(filename, 'r') as file:
            lines = file.readlines()

        # This will store the new lines for the updated file content 
        new_lines = []
        paramid_count = 0
        node_pattern = re.compile(r'^[^\s]+:')  # Matches new nodes (lines starting without spaces followed by :)
        comment_pattern = re.compile(r'^\s*#')  # Matches lines that are comments (starting with # optionally preceded by spaces)

        for line in lines:
            # Ignore comment lines
            if comment_pattern.match(line):
                new_lines.append(line)
                continue

            # If a new node starts, reset the paramid_count
            if node_pattern.match(line):
                paramid_count = 0

            if 'ParamID:' in line:
                new_lines.append(f'      ParamID: {paramid_count}\n')  # 6 spaces before ParamID
                paramid_count += 1
            else:
                new_lines.append(line)

        # Write the updated content back to the file
        with open(filename, 'w') as file:
            file.writelines(new_lines)

        print(f"File '{filename}' has been processed successfully.")
    
    except FileNotFoundError:
        print(f"Error: File '{filename}' not found.")
    except IOError as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <filename>")
    else:
        process_file(sys.argv[1])
