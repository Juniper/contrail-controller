#!/usr/bin/env bash

set -o errexit
set -x
echo "PATH : => " $PATH
echo "GOPATH : => " $GOPATH
echo "GOROOT : => " $GOROOT
echo "PWD : => " $PWD
SRC_PATH=${SRC_PATH:-""}
GOLINT_INSTALL_PATH=${GOLINT_INSTALL_PATH:-$(go env GOPATH)}
go install ./${SRC_PATH}/vendor/github.com/golang/dep/cmd/dep

curl -sfL https://raw.githubusercontent.com/golangci/golangci-lint/master/install.sh | \
        bash -s -- -b ${GOLINT_INSTALL_PATH}/bin v1.10.2
