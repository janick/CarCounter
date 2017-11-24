#! /bin/bash

ROOT='/home/CarCounter'

#
# Kill any running instance
#
if [ ! -f "$ROOT/CarCounter.pid" ]; then
    kill -9 `cat $ROOT/CarCounter.pid`
    rm $ROOT/CarCounter.pid
fi

#
# Start a new instance of the CarCounter, logging into the daily file
#
MONTH=`date +%Y-%m`
DAY=`date +%Y-%m-%d`

if [ ! -d "$ROOT/logs/$MONTH" ]; then
    mkdir -p "$ROOT/logs/$MONTH"
fi

$ROOT/bin/CarCounter >> $ROOT/logs/$MONTH/$DAY &
