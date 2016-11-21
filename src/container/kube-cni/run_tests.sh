#!/bin/bash
set -eu

build_top=${build_top:-$(pwd)/../../../../build/debug}
tools_path=${tools_path:-$(pwd)/../../../debug/config/common/tests/}
root_path=${root_path:-$(pwd)}

source ${tools_path}/tools/run_tests.sh
