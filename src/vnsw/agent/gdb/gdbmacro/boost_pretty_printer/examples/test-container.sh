#!/bin/bash

if [ -x test-container ] && [ test-container -nt test-container.cpp ]; then
    echo "test-container: exists"
else
    echo "test-container: compiling"
    g++ -ggdb -std=c++0x -I. -o test-container test-container.cpp ||
    {
        echo "compilation failed"
        exit 1
    }
fi
echo "test-container: invoking gdb"
gdb test-container -x test-container.gdb | egrep -e '^\$[0-9]+ ='
