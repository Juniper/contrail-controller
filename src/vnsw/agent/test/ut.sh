#!/bin/bash

# A simple script to run multiple iterations of a UT in parallel
# By default, runs the test 1000 times with 8 tests running in parallel

if [ $# -ge 4 -o $# -lt 1 ]; then
    echo "Usage : " $0 " <cmd> [count [parallels]]"
    exit 1
fi

FILE=$1
if [ ! -f $FILE ]; then
    echo "File " $FILE " not found"
    exit 2
fi

COUNT=1000
if [ $# -ge 2 ]; then
    COUNT=$2
fi

STEPS=8
if [ $# -ge 3 ]; then
    STEPS=$3
fi

if [ $STEPS -gt 8 ]; then
    echo "Max parallel run supported is 8"
    exit 3
fi

echo "Running $FILE iterations $COUNT parallel-runs $STEPS"

#run_once #file #iter #parallels #step
function run_once {
    if [ $4 -lt $3 ]; then
        iter=$((($2*$3) + $4))
        echo "Iteration " $iter
        LOGFILE=`basename $1`.$iter.out
        $1 --gtest_break_on_failure > ut/$LOGFILE 2>&1 &
    fi
}

#wait_one #pid #iter #parallels #step
function wait_one {
    if [ $4 -lt $3 ]; then
        iter=$((($2*$3) + $4))
        wait $1
        if [ $? -ne 0 ]; then
             echo "Failed iteration $iter pid $1. Stopping"
             exit 3
        fi
    fi
}

# run #file #iter-count #parallel
function run {
    run_once $1 $2 $3 0
    run_once $1 $2 $3 1
    run_once $1 $2 $3 2
    run_once $1 $2 $3 3
    run_once $1 $2 $3 4
    run_once $1 $2 $3 5
    run_once $1 $2 $3 6
    run_once $1 $2 $3 7

    wait_one %1 $2 $3 0
    wait_one %2 $2 $3 1
    wait_one %3 $2 $3 2
    wait_one %4 $2 $3 3
    wait_one %5 $2 $3 4
    wait_one %6 $2 $3 5
    wait_one %7 $2 $3 6
    wait_one %8 $2 $3 7
}

#Set environment variables for UT
TOP=$PWD
export LD_BIND_NOW=1
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$TOP/build/lib
export HEAPCHECK=normal
export PPROF_PATH=$TOP/build/bin/pprof

mkdir -p ut
ulimit -c unlimited

COUNT=$(($COUNT/$STEPS))
for (( i=0; i < $COUNT; i++))
do
    run $FILE $i $STEPS
done
