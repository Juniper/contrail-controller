#!/bin/bash

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

TARGET=build/debug/api-lib/java/src/net/juniper/contrail/api/types

mkdir -p ${TARGET}
tools/generateds/generateDS.py -f -o ${TARGET} -g java-api src/contrail-api-client/schema/vnc_cfg.xsd
tools/generateds/generateDS.py -f -o ${TARGET} -g golang-api src/contrail-api-client/schema/vnc_cfg.xsd
