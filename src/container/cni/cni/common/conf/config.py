#!/usr/bin/python
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import argparse
import json
import os
import sys

def read_config_file(conf_file=None):
    '''
    Read config file from STDIN or optionally from a file
    '''
    if conf_file:
        with open(conf_file, 'r') as f:
            input_str = f.read()
    else:
        input_str = sys.stdin.read()
    obj = json.loads(input_str)
    return obj


def parse_args():
    '''
    Define command-line arguemnts.
    Note: CNI does not use any command-line arguments. Arguments are used only
    when run as standalone application (ex: for UT)
    Command-line argument overrides environment variables
    '''
    parser = argparse.ArgumentParser(description='CNI Arguments')
    parser.add_argument('-c', '--command',
                        help='CNI command add/del/version/get/poll')
    parser.add_argument('-v', '--version', action='version', version='0.1')
    parser.add_argument('-f', '--file', help='Contrail CNI config file')
    parser.add_argument('-u', '--uuid', help='Container UUID')
    args = parser.parse_args()

    # Override CNI_COMMAND environment
    if args.command != None:
        os.environ['CNI_COMMAND'] = args.command
    return args


