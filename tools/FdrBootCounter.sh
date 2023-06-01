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
    while read value; do
      count="$value" ;
    done < "$DATAFILE"
  else if [ ! -d "$DATAFILEDIR" ]; then
    mkdir -p "$DATAFILEDIR"
    count=0
  fi fi

  count=$(($count + 1))
  echo "$count" > "$DATAFILE" # this echo will update the counter inside the file.
}

# need to determine on what condition this script got called,
# whether its during HMC reboot or during FDR process/service restart?
#   if "/tmp/.fdrHmcAlive" not exist, then its HMC reboot, else its FDR restart
# update the boot counter only during HMC reboot or if BootCount.txt doesn't exist.
if [ ! -f "/tmp/.fdrHmcAlive" ] || [ ! -f $DATAFILE ]; then
  update_data_file
else
  count="$(cat $DATAFILE)"
fi

echo "$count" # this echo will be the return value of this script.

exit 0
