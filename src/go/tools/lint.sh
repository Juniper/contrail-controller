#!/usr/bin/env bash

set -x
set -o pipefail


echo "PATH : => " $PATH
echo "GOPATH : => " $GOPATH
echo "GOROOT : => " $GOROOT
echo "PWD : => " $PWD
SRC_PATH=${SRC_PATH:-""}

echo "DIR 0: $(dirname $0): => " $PWD
TOP=$(cd "$(dirname "$0")" && cd ../ && pwd)
LINT_LOG_DIR=${LINT_LOG_DIR:-${TOP}}
echo "TOP : => " $TOP
echo "PWD : => " $PWD

function run_go_tool_fix() {
	local issues
	issues=$(go tool fix --diff ./${SRC_PATH}/cmd/ ./${SRC_PATH}/pkg/)

	[[ -z "$issues" ]] || (echo "Go tool fix found issues: $issues" && return 1)
}

run_go_tool_fix || exit 1

cd ./${SRC_PATH} 
golangci-lint --config .golangci.yml --verbose run ./... 2>&1 | tee -a "${LINT_LOG_DIR}/linter.log" || exit 1
