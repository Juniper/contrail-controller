#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Contains common logger initialization to be used in job mgr internals."""

import logging

DEFAULT_JOB_MGR_LOG_PATH = '/var/log/contrail/contrail-fabric-ansible.log'
DATE_FORMAT = "%m/%d/%Y %I:%M:%S %p"
LOGGING_FORMAT = '%(asctime)s [%(name)s] [%(levelname)s]:  %(message)s'


def job_mgr_logger(name, ctx=None):
    name = name
    ctx = ctx

    logger = logging.getLogger(name)
    handler = logging.FileHandler(DEFAULT_JOB_MGR_LOG_PATH)
    formatter = logging.Formatter(fmt=LOGGING_FORMAT, datefmt=DATE_FORMAT)
    handler.setFormatter(formatter)
    logger.addHandler(handler)

    return logger
