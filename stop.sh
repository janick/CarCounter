#! /bin/bash

ROOT='/home/CarCounter'

#
# Kill any running instance
#
kill -9 `cat $ROOT/CarCount.pid`
