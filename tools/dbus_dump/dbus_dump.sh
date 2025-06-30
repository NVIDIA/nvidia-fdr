#!/bin/bash

# D-Bus Tree Dumper Script
# Usage: ./dbus_dump.sh [service_name] [output_file]
# If no service_name provided, dumps all services
# If no output_file provided, uses dbus_dump.yaml

set -e

# Default values
OUTPUT_FILE="dbus_dump.yaml"
SERVICE_NAME=""
BUS_TYPE="system"  # Can be 'system' or 'session'

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" >&2
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

log_debug() {
    echo -e "${BLUE}[DEBUG]${NC} $1" >&2
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS] [service_name] [output_file]"
    echo ""
    echo "Options:"
    echo "  -h, --help        Show this help message"
    echo "  -s, --system      Use system bus (default)"
    echo "  -u, --session     Use session bus"
    echo "  -o, --output FILE Specify output file (default: dbus_dump.yaml)"
    echo ""
    echo "Arguments:"
    echo "  service_name      Specific D-Bus service to dump (optional)"
    echo "  output_file       Output YAML file name (optional)"
    echo ""
    echo "Examples:"
    echo "  $0                              # Dump all services to dbus_dump.yaml"
    echo "  $0 org.freedesktop.NetworkManager # Dump specific service"
    echo "  $0 -o my_dump.yaml              # Dump all to custom file"
    echo "  $0 --session                    # Dump session bus instead of system"
}

# Function to check if required tools are available
check_dependencies() {
    local missing_deps=()
    
    if ! command -v busctl &> /dev/null && ! command -v dbus-send &> /dev/null; then
        missing_deps+=("busctl or dbus-send")
    fi
    
    if ! command -v gdbus &> /dev/null; then
        log_warn "gdbus not found, will use dbus-send/busctl (may have limited functionality)"
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        log_error "Please install systemd (for busctl) or dbus (for dbus-send)"
        exit 1
    fi
}

# Function to get all D-Bus services
get_all_services() {
    local bus_option=""
    if [ "$BUS_TYPE" = "session" ]; then
        bus_option="--user"
    fi
    
    log_info "Discovering all D-Bus services on $BUS_TYPE bus..."
    
    if command -v busctl &> /dev/null; then
        busctl $bus_option list --no-legend | awk '{print $1}' | grep -v "^:" | sort
    elif command -v dbus-send &> /dev/null; then
        local bus_address="--system"
        if [ "$BUS_TYPE" = "session" ]; then
            bus_address="--session"
        fi
        
        dbus-send $bus_address --dest=org.freedesktop.DBus --type=method_call \
            --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames | \
            grep -o '"[^"]*"' | sed 's/"//g' | grep -v "^:" | sort
    else
        log_error "No suitable D-Bus tool found"
        exit 1
    fi
}

# Function to get object paths from busctl tree output
get_object_paths() {
    local service="$1"
    local bus_option=""
    
    if [ "$BUS_TYPE" = "session" ]; then
        bus_option="--user"
    fi
    
    log_debug "Getting object paths for service: $service"
    
    # Method 1: Try busctl tree (preferred method)
    if command -v busctl &> /dev/null; then
        local tree_output
        if tree_output=$(busctl $bus_option tree "$service" 2>/dev/null); then
            # Extract paths from tree output - look for actual paths (starting with /)
            echo "$tree_output" | grep -o '/[^[:space:]]*' | sort -u
            return 0
        fi
    fi
    
    # Method 2: Try gdbus (fallback)
    if command -v gdbus &> /dev/null; then
        local gdbus_bus="--system"
        if [ "$BUS_TYPE" = "session" ]; then
            gdbus_bus="--session"
        fi
        
        # Start with root path and recursively discover
        discover_paths_recursive "$service" "/" "$gdbus_bus"
        return 0
    fi
    
    # Method 3: Fallback to common paths if other methods fail
    log_warn "Could not enumerate paths for $service, trying common root paths"
    echo "/"
    
    # Try some common path patterns
    local service_path="/$(echo "$service" | tr '.' '/')"
    echo "$service_path"
}

# Function to recursively discover object paths
discover_paths_recursive() {
    local service="$1"
    local path="$2"
    local bus_arg="$3"
    
    # Introspect the current path
    local introspect_data
    if introspect_data=$(gdbus introspect $bus_arg --dest "$service" --object-path "$path" 2>/dev/null); then
        echo "$path"
        
        # Look for child nodes in the introspection data
        echo "$introspect_data" | grep "^  node " | sed 's/^  node //' | while read -r child_node; do
            if [ -n "$child_node" ]; then
                local child_path
                if [ "$path" = "/" ]; then
                    child_path="/$child_node"
                else
                    child_path="$path/$child_node"
                fi
                discover_paths_recursive "$service" "$child_path" "$bus_arg"
            fi
        done
    fi
}

# Function to introspect an object path
introspect_object() {
    local service="$1"
    local object_path="$2"
    local bus_option=""
    
    if [ "$BUS_TYPE" = "session" ]; then
        bus_option="--user"
    fi
    
    log_debug "Introspecting $service at $object_path"
    
    # Try busctl introspect first (preferred - gives formatted output)
    if command -v busctl &> /dev/null; then
        local introspect_output
        if introspect_output=$(busctl $bus_option introspect "$service" "$object_path" 2>/dev/null); then
            echo "$introspect_output"
            return 0
        fi
    fi
    
    # Fallback to gdbus
    if command -v gdbus &> /dev/null; then
        local gdbus_bus="--system"
        if [ "$BUS_TYPE" = "session" ]; then
            gdbus_bus="--session"
        fi
        
        if gdbus introspect $gdbus_bus --dest "$service" --object-path "$object_path" 2>/dev/null; then
            return 0
        fi
    fi
    
    # Fallback to dbus-send
    if command -v dbus-send &> /dev/null; then
        local dbus_bus="--system"
        if [ "$BUS_TYPE" = "session" ]; then
            dbus_bus="--session"
        fi
        
        if dbus-send $dbus_bus --dest="$service" --print-reply "$object_path" \
            org.freedesktop.DBus.Introspectable.Introspect 2>/dev/null | \
            sed -n '/string "/,/"/p' | sed 's/^.*string "//; s/".*$//'; then
            return 0
        fi
    fi
    
    log_warn "Failed to introspect $service at $object_path"
    echo "# Introspection failed"
}

# Function to escape YAML special characters
yaml_escape() {
    # Escape quotes and backslashes, handle multiline
    sed 's/\\/\\\\/g; s/"/\\"/g' | \
    awk '{
        if (NR == 1 && NF > 0) {
            if ($0 ~ /^[[:space:]]*$/) {
                print "\"\"";
            } else if ($0 ~ /[:|>@`]/ || $0 ~ /^[[:space:]]/ || $0 ~ /[[:space:]]$/ || $0 ~ /^[0-9]/ || $0 ~ /^-/ || $0 ~ /^[!&*]/) {
                gsub(/"/, "\\\"");
                print "\"" $0 "\"";
            } else {
                print $0;
            }
        } else if (NR == 1) {
            print "\"\"";
        } else {
            gsub(/"/, "\\\"");
            print "  \"" $0 "\"";
        }
    }'
}

# Function to get tree structure for a service
get_tree_structure() {
    local service="$1"
    local bus_option=""
    
    if [ "$BUS_TYPE" = "session" ]; then
        bus_option="--user"
    fi
    
    if command -v busctl &> /dev/null; then
        busctl $bus_option tree "$service" 2>/dev/null || echo "# Tree structure not available"
    else
        echo "# Tree structure not available (busctl not found)"
    fi
}

# Function to create YAML output
create_yaml_output() {
    local services=("$@")
    
    log_info "Creating YAML output in $OUTPUT_FILE"
    
    # Initialize YAML file
    echo "# D-Bus Tree Dump" > "$OUTPUT_FILE"
    echo "# Generated on: $(date)" >> "$OUTPUT_FILE"
    echo "# Bus type: $BUS_TYPE" >> "$OUTPUT_FILE"
    echo "# Format: service -> tree_structure + object_paths -> introspection_data" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"
    echo "dbus_dump:" >> "$OUTPUT_FILE"
    
    for service in "${services[@]}"; do
        if [ -z "$service" ] || [[ "$service" =~ ^: ]]; then
            continue  # Skip empty or connection-specific names
        fi
        
        log_info "Processing service: $service"
        echo "  \"$service\":" >> "$OUTPUT_FILE"
        
        # Add tree structure
        echo "    tree_structure: |" >> "$OUTPUT_FILE"
        local tree_data
        tree_data=$(get_tree_structure "$service")
        if [ -n "$tree_data" ]; then
            echo "$tree_data" | sed 's/^/      /' >> "$OUTPUT_FILE"
        else
            echo "      # Tree structure not available" >> "$OUTPUT_FILE"
        fi
        echo "" >> "$OUTPUT_FILE"
        
        # Add object paths and introspection data
        echo "    object_paths:" >> "$OUTPUT_FILE"
        
        # Get object paths for this service
        local paths
        mapfile -t paths < <(get_object_paths "$service" | sort -u)
        
        if [ ${#paths[@]} -eq 0 ]; then
            log_warn "No object paths found for $service"
            echo "      # No accessible object paths found" >> "$OUTPUT_FILE"
            echo "" >> "$OUTPUT_FILE"
            continue
        fi
        
        for path in "${paths[@]}"; do
            if [ -z "$path" ]; then
                continue
            fi
            
            log_debug "Processing path: $path"
            echo "      \"$path\":" >> "$OUTPUT_FILE"
            echo "        introspection: |" >> "$OUTPUT_FILE"
            
            # Get introspection data
            local introspect_data
            introspect_data=$(introspect_object "$service" "$path")
            
            if [ -n "$introspect_data" ] && [ "$introspect_data" != "# Introspection failed" ]; then
                # Process introspection data and add proper indentation
                echo "$introspect_data" | sed 's/^/          /' >> "$OUTPUT_FILE"
            else
                echo "          # Introspection data not available" >> "$OUTPUT_FILE"
            fi
            
            echo "" >> "$OUTPUT_FILE"
        done
        
        echo "" >> "$OUTPUT_FILE"
    done
    
    log_info "YAML dump completed: $OUTPUT_FILE"
}

# Parse command line arguments
while [ $# -gt 0 ]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -s|--system)
            BUS_TYPE="system"
            shift
            ;;
        -u|--session)
            BUS_TYPE="session"
            shift
            ;;
        -o|--output)
            if [ -n "$2" ]; then
                OUTPUT_FILE="$2"
                shift 2
            else
                log_error "Option --output requires an argument"
                exit 1
            fi
            ;;
        -*)
            log_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
        *)
            if [ -z "$SERVICE_NAME" ]; then
                SERVICE_NAME="$1"
            elif [ "$OUTPUT_FILE" = "dbus_dump.yaml" ]; then
                OUTPUT_FILE="$1"
            fi
            shift
            ;;
    esac
done

# Main execution
main() {
    log_info "D-Bus Tree Dumper starting..."
    log_info "Bus type: $BUS_TYPE"
    log_info "Output file: $OUTPUT_FILE"
    
    # Check dependencies
    check_dependencies
    
    # Determine services to process
    local services_to_process=()
    
    if [ -n "$SERVICE_NAME" ]; then
        log_info "Processing specific service: $SERVICE_NAME"
        services_to_process=("$SERVICE_NAME")
    else
        log_info "Processing all available services..."
        mapfile -t services_to_process < <(get_all_services)
        log_info "Found ${#services_to_process[@]} services"
    fi
    
    if [ ${#services_to_process[@]} -eq 0 ]; then
        log_error "No services found to process"
        exit 1
    fi
    
    # Create YAML output
    create_yaml_output "${services_to_process[@]}"
    
    log_info "D-Bus tree dump completed successfully!"
    log_info "Output saved to: $OUTPUT_FILE"
}

# Run main function
main "$@"
