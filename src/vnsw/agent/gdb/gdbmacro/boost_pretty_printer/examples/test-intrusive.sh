#!/bin/bash

if [ -x test-intrusive ] && [ test-intrusive -nt test-intrusive.cpp ]; then
    echo "test-intrusive: exists"
else
    echo "test-intrusive: compiling"
    g++ -ggdb -std=c++0x -Wall -Wextra -I. -o test-intrusive test-intrusive.cpp ||
    {
        echo "compilation failed"
        exit 1
    }
fi
echo "test-intrusive: invoking gdb"
gdb test-intrusive -x test-intrusive.gdb | egrep -e '^\$[0-9]+ =' | sed "s/\(.*\[0\] =\).*/\1 .../g"
