#! /usr/bin/env /usr/bin/python

# 
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
# 

# this is called from c++ code and is expected to be passed
# the following variables
# paramlist - a list of strings considered as parameters

import sys

actionresult = "echoaction.py"
i = 0
while i < len(paramlist):
    actionresult = actionresult + " " + paramlist[i]
    i = i + 1
