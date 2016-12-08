# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Logger for Opencontrail CNI
"""

import logging


class Logger(object):

    def __init__(self, module, log_file, log_level):
        logging.basicConfig(filename=log_file, level=log_level.upper())
        self.logger = logging.getLogger(module)
        return

    def debug(self, msg):
        self.logger.debug(msg)

    def info(self, msg):
        self.logger.info(msg)

    def warning(self, msg):
        self.logger.warning(msg)

    def error(self, msg):
        self.logger.error(msg)

    def critical(self, msg):
        self.logger.criticial(msg)
