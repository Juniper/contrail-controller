#!/bin/bash

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# stand alone shell script for generting device apis and place xsd, test files for reference
# in the target build folder

TARGET=build/debug/api-lib/device_api/gen
JUNIPER_DEVICE_XSD=juniper_common.xsd
JUNIPER_DEVICE_GEN_LIB=juniper_common

mkdir -p ${TARGET}
tools/generateds/generateDS.py -f -o ${TARGET}/${JUNIPER_DEVICE_GEN_LIB} -g device-api controller/src/schema/device_schema/juniper/${JUNIPER_DEVICE_XSD}
cp ./controller/src/config/common/generatedssuper.py ${TARGET}
cp ./controller/src/schema/device_schema/juniper/* ${TARGET}
