#!/usr/bin/env bash

set -o errexit

go install github.com/gogo/protobuf/protoc-gen-gogofaster

BASEDIR=$(dirname "$0")
cd $BASEDIR/..

if [[ $(./bin/protoc --version) == "libprotoc 3.5.1" ]]; then
    echo "Protoc already installed - skipping"
    exit 0
fi

if [ "$(uname)" == 'Darwin' ]; then
    wget https://github.com/google/protobuf/releases/download/v3.5.1/protoc-3.5.1-osx-x86_64.zip
    unzip -o protoc-3.5.1-osx-x86_64.zip "bin/protoc"
    rm protoc-3.5.1-osx-x86_64.zip
elif [ "$(expr substr $(uname -s) 1 5)" == 'Linux' ]; then
    wget https://github.com/google/protobuf/releases/download/v3.5.1/protoc-3.5.1-linux-x86_64.zip
    unzip -o protoc-3.5.1-linux-x86_64.zip "bin/protoc"
    rm protoc-3.5.1-linux-x86_64.zip
else
    echo "Your platform ($(uname -a)) is not supported."
    echo "Please manually install protoc"
fi
