# D-Bus Tree Dumper

A comprehensive bash script to dump D-Bus tree information from Linux systems into YAML format.

## Features

- **Service Discovery**: Automatically discovers all available D-Bus services or processes a specific service
- **Object Path Enumeration**: Finds all object paths for each service
- **Introspection**: Retrieves detailed introspection data for each object path
- **YAML Output**: Organizes data in a structured YAML format with proper hierarchy:
  - Service as parent node
  - Object path as second parent
  - Introspection data as child
- **Multiple D-Bus Tools**: Supports `busctl`, `gdbus`, and `dbus-send` for maximum compatibility
- **Bus Type Support**: Works with both system and session buses

## Usage

```bash
# Make script executable
chmod +x dbus_dump.sh

# Dump all services to default file (dbus_dump.yaml)
./dbus_dump.sh

# Dump specific service
./dbus_dump.sh org.freedesktop.NetworkManager

# Dump to custom output file
./dbus_dump.sh -o my_dump.yaml

# Dump session bus instead of system bus
./dbus_dump.sh --session

# Dump specific service with custom output
./dbus_dump.sh org.freedesktop.NetworkManager custom_output.yaml

# Show help
./dbus_dump.sh --help
```

## Command Line Options

- `-h, --help`: Show help message
- `-s, --system`: Use system bus (default)
- `-u, --session`: Use session bus
- `-o, --output FILE`: Specify output file (default: dbus_dump.yaml)

## Output Format

The script generates YAML with the following structure:

```yaml
# D-Bus Tree Dump
# Generated on: [timestamp]
# Bus type: system
# Format: service -> tree_structure + object_paths -> introspection_data

dbus_dump:
  "service.name":
    tree_structure: |
      `- /xyz
        `- /xyz/openbmc_project
          |- /xyz/openbmc_project/FruDevice
          | |- /xyz/openbmc_project/FruDevice/10
          | `- /xyz/openbmc_project/FruDevice/12
    
    object_paths:
      "/xyz/openbmc_project":
        introspection: |
          NAME                                TYPE      SIGNATURE RESULT/VALUE FLAGS
          org.freedesktop.DBus.Introspectable interface -         -            -
          .Introspect                         method    -         s            -
          
      "/xyz/openbmc_project/FruDevice/10":
        introspection: |
          NAME                                TYPE      SIGNATURE RESULT/VALUE                           FLAGS
          org.freedesktop.DBus.Introspectable interface -         -                                      -
          .Introspect                         method    -         s                                      -
          xyz.openbmc_project.FruDevice       interface -         -                                      -
          .DEVICE_TYPE                        property  y         4                                      emits-change
```

## Requirements

The script requires at least one of the following D-Bus tools:
- `busctl` (from systemd) - Preferred
- `gdbus` (from GLib) - Good for introspection
- `dbus-send` (from dbus) - Fallback option

## Error Handling

- Script checks for required dependencies on startup
- Gracefully handles services that can't be introspected
- Logs warnings and errors to stderr while keeping data output clean
- Continues processing other services if one fails

## Examples

### Dump with service name
```bash
./dbus_dump.sh xyz.openbmc_project.EntityManager
```

```bash
./dbus_dump.sh xyz.openbmc_project.NSM
```


## Use Cases and Benefits

With this script, you can generate a dump of the entire D-Bus tree and its subtrees. You can then search the YAML for your desired D-Bus property (which is listed alongside the Redfish API in curly braces {} at the end of the URI), and retrieve the corresponding object path and interface.

If multiple matching properties are found, try to match the object path with the Redfish API as defined in the Telemetry Catalog.



This makes it easy to:
- **Discover available properties** across all D-Bus services
- **Find object paths** for specific interfaces or properties
- **Map telemetry data** by correlating with RF identifiers
- **Debug D-Bus issues** by having complete service visibility
- **Document system interfaces** for development and integration

## Troubleshooting

If you see malformed YAML output, ensure you're using the latest version of the script where log messages are properly directed to stderr.

The script automatically tries different methods to enumerate object paths and falls back to common patterns if automated discovery fails. 