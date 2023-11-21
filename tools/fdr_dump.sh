#!/bin/bash

TMP_DIR="/tmp"
EPOCHTIME=$(date +"%s")
F_NAME_TEMPLATE=""

ARG_DUMP_ID="00000000"
ARG_DUMP_ACTION="collect"
ARG_DUMP_PATH=""

FDR_LOG_PATH="/var/emmc/fdr/"

function help()
{
    echo "Usage: fdr_dump [-h] -p <file_path> -i <dump_id> -a <action>"
    echo ""
    echo "Options:"
    echo "          -h  shows this help"
    echo "          -p  (required) path to put compressed dump to"
    echo "          -i  file dump id, default $ARG_DUMP_ID"
    echo "          -a  action, default collect"
}


function on_clean_action()
{
    if [ -d "$FDR_LOG_PATH" ]; then
        find $FDR_LOG_PATH \
            -mindepth 1 \
            -maxdepth 1 \
            -type d \
            ! -name 'Bookkeeper' \
            -exec rm -rf {} + && \
        find $FDR_LOG_PATH \
            -type f \
            ! -name 'BirthCertificate.tar' \
            -exec rm -rf {} +
    fi
}


function initialize()
{
    F_NAME_TEMPLATE=$"obmcdump_"$ARG_DUMP_ID"_$EPOCHTIME"

    mkdir -p $ARG_DUMP_PATH
    if [ $? -ne 0 ]; then
        echo "Failed to create destination directory $ARG_DUMP_PATH"
        exit 1
    fi
    echo "Created dest dir $ARG_DUMP_PATH"

}

function cleanup()
{
    local res_ret=0

    return $res_ret
}

function main()
{
    local TEMP_DUMP_FILE="/tmp/.$F_NAME_TEMPLATE.tar.xz"
    local DEST_DUMP_FILE="$ARG_DUMP_PATH/$F_NAME_TEMPLATE.tar.xz"

    # compress fdr dir to destination dir directly to save memory
    tar -Jcf $TEMP_DUMP_FILE -C $(dirname "$FDR_LOG_PATH") \
        $(basename "$FDR_LOG_PATH")

    if [ $? -ne 0 ]; then
        echo "Compression $FDR_LOG_PATH failed"

        # remove the temp file if error occured
        rm -rf $TEMP_DUMP_FILE

        return 1
    fi

    # rename compressed archive
    mv $TEMP_DUMP_FILE $DEST_DUMP_FILE
    if [ $? -ne 0 ]; then
        echo "Failed to move $TEMP_DUMP_FILE to $DEST_DUMP_FILE"

        # remove both files if error occured
        rm -rf $TEMP_DUMP_FILE
        rm -rf $DEST_DUMP_FILE

        return 1
    fi

    return 0
}

while getopts ":hDp:i:a:" option; do
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
