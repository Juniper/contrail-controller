#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains common logger initialization to be used in ansible internals
as well as ansible modules
"""

import os
import logging
from logging.handlers import RotatingFileHandler
from ansible import constants as CONST

DEFAULT_ANSIBLE_LOG_PATH = '/var/log/contrail/contrail-fabric-ansible-playbooks.log'
LOGGING_FORMAT = '%(asctime)s.%(msecs)03d %(name)s [%(levelname)s]:  %(message)s'
DATE_FORMAT = "%m/%d/%Y %H:%M:%S"

# Context attribute along with it's abbeviation for logging
# Include attributes in this list if you want them to appear in log
# message header
extra_list = {
    'job_execution_id': 'eid',
    # add more entries here...
}


def fabric_ansible_logger(name, ctx=None):
    name = name
    ctx = ctx
    debug = CONST.DEFAULT_DEBUG
    verbosity = CONST.DEFAULT_VERBOSITY
    logfile = CONST.DEFAULT_LOG_PATH or DEFAULT_ANSIBLE_LOG_PATH
    # Log more if ANSIBLE_DEBUG or -v[v] is set.
    if debug is True:
        level = logging.DEBUG
    elif verbosity == 1:
        level = logging.INFO
    elif verbosity > 1:
        level = logging.DEBUG
    else:
        level = logging.WARNING
    # If log file is writable, init normal logger, otherwise use null logger
    # to avoid logging errors
    if (logfile is not None and os.path.exists(logfile) and os.access(
            logfile, os.W_OK)) or os.access(os.path.dirname(logfile), os.W_OK):
        logging.basicConfig(
            filename=logfile,
            level=level,
            format=LOGGING_FORMAT,
            datefmt=DATE_FORMAT)
        name_hdr = "[{}]".format(name)
        name_hdr += " pid={}".format(str(os.getpid()))
        # Include any context attributes
        if ctx:
            for k, v in extra_list.iteritems():
                if k in ctx:
                    name_hdr += " {}={}".format(v, ctx[k])
        logger = logging.getLogger(name_hdr)
        handler = RotatingFileHandler(logfile, maxBytes=6291454)
        logger.setLevel(level)
        logger.addHandler(handler)
    else:
        raise Exception("Cannot write to log file at {}".format(logfile))

    return logger

