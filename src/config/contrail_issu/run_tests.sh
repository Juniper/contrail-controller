#!/bin/bash
set -eu

build_top=${build_top:-$(pwd)/../../}
tools_path=${tools_path:-$(pwd)/../common/tests/}
tools_path=${tools_path:-$(pwd)/../schema-transformer/test/}
root_path=${root_path:-$(pwd)}

source ${tools_path}/tools/run_tests.sh
