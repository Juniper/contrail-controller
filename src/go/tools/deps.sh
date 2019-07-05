#!/usr/bin/env bash

set -o errexit

go install ./vendor/github.com/golang/dep/cmd/dep

curl -sfL https://raw.githubusercontent.com/golangci/golangci-lint/master/install.sh | \
        bash -s -- -b $(go env GOPATH)/bin v1.10.2
