#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains common logger initialization to be used in job_manager internals
that are to be captured from playbook_helper.py sub-process
"""

import logging
from logging.handlers import RotatingFileHandler

DEFAULT_JOB_MGR_LOG_PATH = '/var/log/contrail/contrail-fabric-ansible.log'
DATE_FORMAT = "%m/%d/%Y %I:%M:%S %p"
LOGGING_FORMAT = '%(asctime)s [%(name)s] [%(levelname)s]:  %(message)s'


def job_mgr_logger(name, ctx=None):
    name = name
    ctx = ctx

    logger = logging.getLogger(name)
    handler = RotatingFileHandler(DEFAULT_JOB_MGR_LOG_PATH,
                                          maxBytes=6291454)
    formatter = logging.Formatter(fmt=LOGGING_FORMAT, datefmt=DATE_FORMAT)
    handler.setFormatter(formatter)
    logger.addHandler(handler)

    return logger
