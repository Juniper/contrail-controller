# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
CNI logger
"""

import logging

class Logger(object):
    def __init__(self, args=None):
        self._args = args

    def error(self, *msg):
        print(msg)

    def warning(self, *msg):
        print(msg)

    def debug(self, *msg):
        print(msg)

    def log(self, *msg):
        print(msg)
