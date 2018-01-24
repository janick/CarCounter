#! /bin/bash

#
# Uses https://github.com/andreafabrizi/Dropbox-Uploader
#

ROOT='/home/CarCounter'

DROPBOX="$ROOT/dropbox_uploader.sh -f $ROOT/dropbox_uploader.conf"

#
# List the 7 log files, in reverse chronological order, not including today's
#
files=`cd $ROOT/logs; find . -type f -mtime +0 -mtime -7 -printf '%Ts\t%p\n' | sort -nr | cut -f2`

# Upload the files
for log in $files; do
    echo "Uploading $log..."
    $DROPBOX upload $ROOT/logs/$log $log.txt
done

# Erase the log files that are older than 14 days
#find $ROOT/logs -type f -mtime +14 -exec rm {} \;
# Clean the empty directories
#find $ROOT/logs -type d -empty -exec rm -f {} \; -prune

exit 0



