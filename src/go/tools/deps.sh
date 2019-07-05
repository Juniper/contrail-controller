#!/usr/bin/env bash

set -o errexit

echo "PATH : => " $PATH
echo "GOPATH : => " $GOPATH
echo "GOROOT : => " $GOROOT
echo "PWD : => " $PWD
SRC_PATH=${SRC_PATH:-""}
go install ./${SRC_PATH}/vendor/github.com/golang/dep/cmd/dep

curl -sfL https://raw.githubusercontent.com/golangci/golangci-lint/master/install.sh | \
        bash -s -- -b $(go env GOPATH)/bin v1.10.2
