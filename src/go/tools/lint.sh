#!/usr/bin/env bash

set -o pipefail

TOP=$(cd "$(dirname "$0")" && cd ../ && pwd)

function run_go_tool_fix() {
	local issues
	issues=$(go tool fix --diff ./cmd/ ./pkg/)

	[[ -z "$issues" ]] || (echo "Go tool fix found issues: $issues" && return 1)
}

run_go_tool_fix || exit 1

golangci-lint --config .golangci.yml --verbose run ./... 2>&1 | tee -a "$TOP/linter.log" || exit 1
