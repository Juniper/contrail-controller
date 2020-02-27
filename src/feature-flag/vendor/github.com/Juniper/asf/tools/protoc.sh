#!/bin/bash

# Wrapper script for protoc. If the binary is missing it runs the insallation script.

BASEDIR=$(dirname "$0")

PROTOC_BIN=$BASEDIR/../bin/protoc
[[ -f "$PROTOC_BIN" ]] || $BASEDIR/install_proto.sh

GOGOPROTO_DIR=$(go list -f '{{ .Dir }}' -m github.com/gogo/protobuf)

PROTOC_ARGS="-I $GOGOPROTO_DIR -I $GOGOPROTO_DIR/protobuf -I. --gogofaster_out=plugins=grpc:."


exec $PROTOC_BIN $PROTOC_ARGS "$@"
