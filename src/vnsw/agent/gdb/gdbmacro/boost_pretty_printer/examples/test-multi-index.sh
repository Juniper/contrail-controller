#!/bin/bash

if [ -x test-multi-index ] && [ test-multi-index -nt test-multi-index.cpp ]; then
    echo "test-multi-index: exists"
else
    echo "test-multi-index: compiling"
    g++ -g -std=c++0x -Wall -Wextra -o test-multi-index test-multi-index.cpp ||
    {
        echo "compilation failed"
        exit 1
    }
fi
echo "test-multi-index: invoking gdb"
gdb test-multi-index -x test-multi-index.gdb
