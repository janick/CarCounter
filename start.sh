#! /bin/bash

ROOT='/home/CarCounter'
cd $ROOT

#
# Kill any running instance
#
if [ -f CarCount.pid ]; then
    kill -9 `cat CarCount.pid`
    rm -f CarCount.pid
fi

#
# Start a new instance of the CarCounter, logging into the daily file
#
MONTH=`date +%Y-%m`
DAY=`date +%Y-%m-%d`

if [ ! -d "logs/$MONTH" ]; then
    mkdir -p "logs/$MONTH"
fi

$ROOT/bin/CarCounter >> logs/$MONTH/$DAY &
