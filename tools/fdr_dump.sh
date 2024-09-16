#!/bin/bash
set -o pipefail

TMP_DIR="/tmp"
EPOCHTIME=$(date +"%s")
F_NAME_TEMPLATE=""
FDR_LOG_PATH="/var/emmc/fdr/"
TAR_BLOCK_SZ="512"
LIVE_DATA_BUFFER="10240"
# ~90% of 44 mins. Rest of the time will be used in creating the tar
FILE_ADDITION_TIME_LIMIT=2340
script_start_time=$(date +%s)
STOP_FILE_ADDITION=0

# Script arguments
ARG_DUMP_ID="00000000"
ARG_DUMP_ACTION="collect"
ARG_DUMP_PATH=""
ARG_DUMP_START="0"
ARG_DUMP_END=$EPOCHTIME
ARG_DUMP_MAX_SIZE="1000000000"
ARG_EXTENDED_SOURCE=""

function help()
{
    echo "Usage: fdr_dump [-h] -p <file_path> -i <dump_id> -a <action> -s <start date ISO8601> -e <end date ISO8601> -m <max dump size allowed> -S <extended source information>"
    echo ""
    echo "Options:"
    echo "          -h  shows this help"
    echo "          -p  (required) path to put compressed dump to"
    echo "          -i  file dump id, default $ARG_DUMP_ID"
    echo "          -a  action, default collect"
    echo "          -s  start time in ISO8601 YYYY-MM-DD HH:mm:ss TZ"      # Default = 0
    echo "          -e  end time in ISO8601 Eg: 2023-04-24 14:40:36 05:30"  # Default = Current time
    echo "          -m  max dump size in bytes"      # Default = 1GB
    echo "          -S  extended source"             
}


function on_clean_action()
{
    if [ -d "$FDR_LOG_PATH" ]; then
        find $FDR_LOG_PATH \
            -mindepth 1 \
            -maxdepth 1 \
            -type d \
            ! -name 'Bookkeeper' \
            ! -name 'dumps' \
            -exec rm -rf {} + && \
        find $FDR_LOG_PATH/Bookkeeper \
            -type f \
            ! -name 'BirthCertificate.tar' \
            -exec rm -rf {} +
    fi

    echo "[INFO] Cleaned up $ARG_DUMP_PATH"
}

function initialize()
{
    F_NAME_TEMPLATE="obmcdump_${ARG_DUMP_ID}_${EPOCHTIME}"
    DEST_DUMP_FILE="$ARG_DUMP_PATH/$F_NAME_TEMPLATE.tar"
    
    mkdir -p $ARG_DUMP_PATH
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to create destination directory $ARG_DUMP_PATH"
        exit 1
    fi
    echo "[INFO] Created destination directory $ARG_DUMP_PATH"
}

function cleanup()
{
    local res_ret=0
     # Removing this temporary output file
    rm ${MANIFEST_FILE}
    rm ${FDR_LOG_DIR_BASE_NAME}/${JOURNALCTL_OUTPUT_FILENAME}
    rm ${FDR_LOG_DIR_BASE_NAME}/$(basename "$FDR_PPF_FILE_PATH")
    return $res_ret
}

# upscale => value + ( -value & (block_size-1) )
# moves value to next multiple of block_size
function up_scale()
{
    local VALUE=$1
    local FACTOR=$2
    local result=$(( $(( VALUE + FACTOR - 1 )) / FACTOR * FACTOR ))
    echo $result
}

function check_size_and_add()
{
    local FILE_NAMES=$1
    # echo "[INFO] check_size_and_add ${FILE_NAMES}"
    
    # If total size exceeds the limit, do not add any of files.
    # local total_file_size=$(du -cb $FILE_NAMES | tail -n 1 | cut -d$'\t' -f 1)
    # if (( current_dump_size + total_file_size > ARG_DUMP_MAX_SIZE )); then
    #     echo "[INFO] Size exceeded the limit!! Cannot add: $FILE_NAMES"
    #     return 1
    # fi

    if (( STOP_FILE_ADDITION == 1 )); then
        echo "Skipping file addition due to time constraints."
        return
    fi

    for file in $FILE_NAMES; do
        # echo "[INFO] check_size_and_add ${file}"
        local file_size=$(du -cb $file | tail -n 1 | cut -d$'\t' -f 1)
        file_size=$(up_scale $file_size $TAR_BLOCK_SZ)
        record_size=$(( $file_size + $(( 1 * $TAR_BLOCK_SZ)) ))
        if (( current_dump_size + record_size > ARG_DUMP_MAX_SIZE )); then
            echo "[INFO] Size exceeded the limit!! Cannot add: $file"
            return 1
        else
            current_dump_size=$(( current_dump_size + record_size ))
            final_files+=($file)
            count_of_files=$(( count_of_files + 1 ))
            echo "[INFO] Added $file, Current dump size: $current_dump_size"
        fi
        script_current_time=$(date +%s)
        script_elapsed_time=$((script_current_time - script_start_time))
        if ((script_elapsed_time >= FILE_ADDITION_TIME_LIMIT)); then
            echo "Time Limit for file addition reached its limit:$script_elapsed_time seconds Script will not add any more data for tar"
            STOP_FILE_ADDITION=1
            return
        fi
    done
    return 0
}

function create_manifest_file()
{
    MANIFEST_FILE="${FDR_LOG_DIR_BASE_NAME}/fdr_manifest.txt"
    echo -e "Log type: FDR" > $MANIFEST_FILE
    
    # Dump file name
    echo -e "Dump file name: $DEST_DUMP_FILE" >> $MANIFEST_FILE

    # HMC-BRD-SERIAL
    command="busctl get-property xyz.openbmc_project.GpuMgr /xyz/openbmc_project/inventory/system/chassis/HGX_Chassis_0/Assembly0 xyz.openbmc_project.Inventory.Decorator.Asset SerialNumber"
    echo -e "HMC-BRD-SERIAL: $( $command )" >> $MANIFEST_FILE

    # HMC-FW-VER
    command="busctl get-property xyz.openbmc_project.Software.BMC.Inventory /xyz/openbmc_project/software/HGX_FW_BMC_0 xyz.openbmc_project.Software.Version Version"
    echo -e "HMC-FW-VER: $( $command )" >> $MANIFEST_FILE

    # GPU SXM SN; TODO: Number of GPUs should not be hard coded.
    for gpuid in {1..8}; do
        command="busctl get-property xyz.openbmc_project.GpuMgr /xyz/openbmc_project/inventory/system/processors/GPU_SXM_$gpuid xyz.openbmc_project.Inventory.Decorator.Asset SerialNumber"
        echo -e "GPU SXM $gpuid SerialNumber: $( $command )" >> $MANIFEST_FILE
    done

    # Extended source information
    echo -e "Extended source information: $ARG_EXTENDED_SOURCE" >> $MANIFEST_FILE
}

function arguments_validation()
{
    # Check for validity of the arguments
    if [[ "$ARG_DUMP_START" -gt "$ARG_DUMP_END" ]]; then
        echo "[FATAL] ARG_DUMP_START is greater than ARG_DUMP_END"
        exit 1
    fi

    if [[ "$ARG_DUMP_START" -lt 0 ]] || [[ "$ARG_DUMP_END" -lt 0 ]] || [[ "$ARG_DUMP_MAX_SIZE" -lt 0 ]]; then
        echo "[FATAL] ARG_DUMP_START, ARG_DUMP_END or ARG_DUMP_MAX_SIZE cannot be negative"
        exit 1
    fi
}

function main()
{
    FDR_LOG_DIR=$(dirname "$FDR_LOG_PATH")
    FDR_LOG_DIR_BASE_NAME=$(basename "$FDR_LOG_PATH")

    pushd $FDR_LOG_DIR
    trap "popd" EXIT

    arguments_validation
    
    # Map each timestamp to a BootCount directory
    declare -A timestamp_file_map
    
    # Map the bootcount number to the oldest directory(that contains others.dat)
    declare -A bootcount_oldestTimestamp_map
    
    # Map of others.dat status. 
    # REQUIRED => others.dat should be in the dump, not yet added
    # ADDED => others.dat added to the dump
    declare -A bootcount_addStatus_map
    
    # Will contain all the directories that are within the time range
    required_files=()
    
    # final_files are required files(directories) that are within the size limit, and other files like
    #   bookkeeper, manifest, journalctl, ppf.
    final_files=()
    
    # Total count of files to be added. This will be used to calculate the tar's metadata size
    count_of_files="0"

    # NOTE: From what I have read online. Busybox TAR, for each file will:  
    # add 1 block for metadata (512 bytes)
    # add the file data
    # pad the file data in multiples of 512 bytes. Eg 500 bytes of file will become 512 bytes.
    # At the end of the archive, it will add 2 blocks of size 512 bytes.

    # NOTE: Added extra TAR_BLOCK_SIZE, not sure why it is more than expected.
    # When collecting the latest BootCount directory, the size of the TAR
    # will be more than the calculated size due to new data being added.
    # Therefore adding some buffer to accomodate this extra data.
    dump_size_buffer=$(( 2 * $TAR_BLOCK_SZ  + $TAR_BLOCK_SZ + $LIVE_DATA_BUFFER )) 
    current_dump_size=${dump_size_buffer}

    # Add Bookkeper to the dump
    check_size_and_add $FDR_LOG_DIR_BASE_NAME/Bookkeeper

    # Add manifest file
    create_manifest_file
    check_size_and_add $MANIFEST_FILE

    # Add journalctl -u 'nvidia-fdr' output to the dump
    JOURNALCTL_OUTPUT_FILENAME="journalctl_u_nvidia-fdr.output"
    # Can also use: --since "-2 day"
    journalctl --since yesterday -u 'nvidia-fdr' &> ${FDR_LOG_DIR_BASE_NAME}/${JOURNALCTL_OUTPUT_FILENAME}
    check_size_and_add "${FDR_LOG_DIR_BASE_NAME}/${JOURNALCTL_OUTPUT_FILENAME}"


    # Add the PPF yaml file
    FDR_PPF_FILE_PATH="/etc/nvidia-fdr/platforms/fdr_ppf_*"
    cp $FDR_PPF_FILE_PATH $FDR_LOG_DIR_BASE_NAME/
    check_size_and_add "$FDR_LOG_DIR_BASE_NAME/$(basename "$FDR_PPF_FILE_PATH")"

    # Collecting all the required directory names, that are within the time range
    files=$(ls -1 $FDR_LOG_PATH)
    for file_name in $files; do
        # Filter BootCount directories
        if [[ "$file_name" =~ ^BootCount ]]; then
            echo "[INFO] Checking $file_name"
            timestamp=$(echo "$file_name" | cut -d '_' -f 4)
            bootcount=$(echo "$file_name" | cut -d '_' -f 2)
            timestamp_file_map[$timestamp]=$file_name
            # Set the oldest BootCount directory for the given bootcount
            if [[ ${bootcount_oldestTimestamp_map[$bootcount]} -ne "" ]]; then
                bootcount_oldestTimestamp_map[$bootcount]=$((bootcount_oldestTimestamp_map[$bootcount] < $timestamp ? bootcount_oldestTimestamp_map[$bootcount] : $timestamp))
            else 
                bootcount_oldestTimestamp_map[$bootcount]=$timestamp
            fi
            # echo "[INFO] bootcount_oldestTimestamp_map ${bootcount_oldestTimestamp_map[$bootcount]}"
            # Check if the time stamp lies within the given range
            if [[ "$ARG_DUMP_START" -le "$timestamp" ]] && [[ "$timestamp" -le "$ARG_DUMP_END" ]]; then
                required_files+=($timestamp)
                bootcount_addStatus_map[$bootcount]="REQUIRED"
            fi
        fi
    done

    # Sort the required directory names. From recent to old.
    required_files_tmp=$(echo "${required_files[@]}" | tr " " "\n" | sort -r)
    required_files=()
    for file_name in $required_files_tmp ; do
        required_files+=($file_name)
    done

    # Add BootCount directories data
    # final_files will contain all the BootCount directories after checking the dump size limit.
    # final_files=() # Declared on the top
    for timestamp in ${required_files[@]} ; do
        file_name=${timestamp_file_map[$timestamp]}
        echo "[INFO] Processing $file_name"
        bootcount=$(echo $file_name | cut -d '_' -f 2)
        count_files="0"
        # Add others.dat files of this bootcount if not already added.
        if [ ${bootcount_addStatus_map[$bootcount]} = "REQUIRED" ]; then
            echo "[INFO] Adding other.dat files."
            bootcount_addStatus_map[$bootcount]="ADDED"
            timestamp_other=${bootcount_oldestTimestamp_map[$bootcount]}
            file_name_other=${timestamp_file_map[$timestamp_other]}
            echo "[INFO] Checking $file_name_other for other.dat files." 

            count_files=$(ls -l $FDR_LOG_DIR_BASE_NAME/$file_name_other/*others.dat | wc -l)
            if (( count_files == "0" )); then
                echo "[WARNING] No others.dat files found in the directory $file_name_other."
            else
                check_size_and_add "$FDR_LOG_DIR_BASE_NAME/$file_name_other/*others.dat"
                if [ $? -ne 0 ]; then
                    break
                fi
            fi
        fi

        # Add the BootCount directory. Ignore the others.dat files; added above.
        check_size_and_add "$(find $FDR_LOG_DIR_BASE_NAME/${file_name} -type f ! -name "*.others.dat")"
        if [ $? -ne 0 ]; then
            break
        fi
        echo
    done
    
    echo "[INFO] Estimated tar size:" $(( current_dump_size ))
    echo 
    
    # Create tar
    tar -o -C $FDR_LOG_DIR --exclude=$(dirname "$DEST_DUMP_FILE") -cf $DEST_DUMP_FILE  ${final_files[@]}
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to tar the dir: $FDR_LOG_PATH"
        exit 1
    fi
    
    dump_tar_size=$(du -cb $DEST_DUMP_FILE | tail -n 1  | cut -d$'\t' -f 1)
    echo "[INFO] Size of tar: $dump_tar_size"
    # echo "[INFO] Calculated size of files: $(($current_dump_size))"
    echo "[INFO] actual tar size - expected tar size: $(($dump_tar_size - $current_dump_size))"
}


while getopts ":hDp:i:a:s:e:m:c:S:" option; do
   case $option in
      h) # display help
         help
         exit;;

      p) # output file path
         ARG_DUMP_PATH=$OPTARG
         ;;

      i) # output file path
         ARG_DUMP_ID=$OPTARG
         ;;

      a) # action
         ARG_DUMP_ACTION=$OPTARG
         ;;
      
      s) # Start time
         ARG_DUMP_START=$(date -d "$OPTARG" +"%s")
         if [ $? -ne 0 ]; then
            echo "Failed to parse start date: $OPTARG"
            exit 1
         fi
         ;;
      
      e) # End time
         ARG_DUMP_END=$(date -d "$OPTARG" +"%s")
         if [ $? -ne 0 ]; then
            echo "Failed to parse end date: $OPTARG"
            exit 1
         fi
         ;;
      
      m) # Max dump size
         ARG_DUMP_MAX_SIZE=$OPTARG
         ;;

      S) # Extended source information
         ARG_EXTENDED_SOURCE=$OPTARG
         ;;
     \?) # Invalid option
         echo "Invalid option: -$OPTARG" >&2
         help
         exit 1
         ;;

      :) echo "Missing option argument for -$OPTARG" >&2
         exit 1
         ;;

      *) echo "Unimplemented option: -$OPTARG" >&2
         exit 1
         ;;
   esac
done

if [ $OPTIND -eq 1 ]; then
    echo "No options were passed"
    WRONG_OPT=1
fi

if [ ! "$ARG_DUMP_PATH" ]; then
    echo "argument -p is required"
    WRONG_OPT=1
fi

if [ $WRONG_OPT ]; then
    help
    exit 1
fi

#
# Check ARG_DUMP_ACTION
#
if [ $ARG_DUMP_ACTION == "clean" ]; then
    on_clean_action
    if [ $? -ne 0 ]; then
        echo "Failed to clean $FDR_LOG_PATH"
        exit 1
    fi

    exit 0
fi

# For action collect
initialize
if [ $? -ne 0 ]; then
    echo "Init failed"
    exit 1
fi

main
if [ $? -ne 0 ]; then
    echo "Dump failed"
    cleanup
    exit 1
fi

cleanup
if [ $? -ne 0 ]; then
    echo "Cleanup failed"
    exit 1
fi

exit 0
