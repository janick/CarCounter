#! /bin/bash

#
# Uses https://github.com/andreafabrizi/Dropbox-Uploader
#

ROOT='/home/CarCounter'

DROPBOX="$ROOT/dropbox_uploader.sh -f $ROOT/dropbox_uploader.conf"

#
# List the log files, in reverse chronological order, not including today's
#
files=`cd $ROOT/logs; find . -type f -mtime +1 -printf '%Ts\t%p\n' | sort -nr | cut -f2`

$toUpload=""
for log in $files; do
    # If the log file has already been uploaded, we only need to upload the newer ones
    $DROPBOX download $log /dev/null
    if [ $? ]; then
	break
    fi
    # If it wasn't on Dropbox already, add it to the list of files to upload.
    # The list is in chronological order so we'll upload the older ones first
    # that way we can recover should we be interrupted
    toUpload="$log $toUpload"
done

# Upload the files that were not on Dropbox
for log in $tpUpload; do
    $DROPBOX upload $ROOT/logs/$log $log
done

# Erase the log files that are older than 14 days
find $ROOT/logs -type f -mtime +14 -exec rm {} \;
# Clean the empty directories
find $ROOT/logs -type d -empty -exec rm -f {} \; -prune

exit 0



