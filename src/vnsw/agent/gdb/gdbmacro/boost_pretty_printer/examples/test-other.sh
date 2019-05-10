#!/bin/bash

if [ -x test-other ] && [ test-other -nt test-other.cpp ]; then
    echo "test-other: exists"
else
    echo "test-other: compiling"
    g++ -g -std=c++0x -Wall -Wextra -o test-other test-other.cpp ||
    {
        echo "compilation failed"
        exit 1
    }
fi
echo "test-other: invoking gdb"
gdb test-other -x test-other.gdb
