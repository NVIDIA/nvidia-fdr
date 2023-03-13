#!/bin/sh

# warning: This shell script should have no other echo statement other than
# the echo at the end just before exit.


if [ $# != 0 ]; then
  DATAFILEDIR=$1
else
  # if the file path is not given then create one
  DATAFILEDIR='/tmp/fdrBootCount'
fi

DATAFILE="$DATAFILEDIR"/BootCount.txt

update_data_file() {
  if [ -w "$DATAFILE" ]; then
    while IFS='=' read key value; do
      case "$key" in
        'count') count="$value" ;;
      esac
    done < "$DATAFILE"
  else if [ ! -d "$DATAFILEDIR" ]; then
    mkdir -p "$DATAFILEDIR"
    count=0
  fi fi

  count=$(($count + 1))
  echo "count=$count" > "$DATAFILE"
}

update_data_file

echo "$count"

exit 0
