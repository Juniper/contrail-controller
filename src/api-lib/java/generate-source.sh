#!/bin/bash

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

TARGET=build/debug/api-lib/java/src/net/juniper/contrail/api/types

mkdir -p ${TARGET}
tools/generateds/generateDS.py -f -o ${TARGET} -g java-api controller/src/schema/vnc_cfg.xsd
