"""Contrail vrouter provisioning package"""

from builtins import str
import os
import logging
import logging.handlers
from subprocess import check_output, CalledProcessError, STDOUT


LOG_DIR = os.getenv('CONTAINER_LOG_DIR')
if os.path.exists(LOG_DIR):
    LOG_FILENAME = os.path.join(LOG_DIR, 'contrail_vrouter_provisioning.log')
else:
    LOG_FILENAME = 'contrail_vrouter_provisioning.log'


def setup_logger():
    """Root logger for the contrail vrouter provisioning
    """
    log = logging.getLogger('contrail_vrouter_provisioning')
    log.setLevel(logging.DEBUG)
    # create rotating file handler which logs even debug messages
    fh = logging.handlers.RotatingFileHandler(LOG_FILENAME,
                                              maxBytes=64*1024,
                                              backupCount=2)
    fh.setLevel(logging.DEBUG)
    # create console handler with a higher log level
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    # create formatter and add it to the handlers
    formatter = logging.Formatter(
        '[%(asctime)s %(name)s(%(lineno)s) %(levelname)s]: %(message)s',
        datefmt='%a %b %d %H:%M:%S %Y')
    fh.setFormatter(formatter)
    ch.setFormatter(formatter)
    # add the handlers to the logger
    log.addHandler(fh)
    log.addHandler(ch)

    return log


# Initialize root logger
log = setup_logger()


class ExtList(list):
    def findex(self, fun):
        for i, x in enumerate(self):
            if fun(x):
                return i
        raise LookupError('No matching element in list')


class AttributeString(str):
    """
    Simple string subclass to allow arbitrary attribute access.
    """
    @property
    def stdout(self):
        return str(self)


def local(cmd, capture=False, warn_only=False, executable='/bin/sh'):
    """
    Wrapper to execute local command and collect its stdout/status.
    """
    log.info("Executing: %s", cmd)
    output, succeeded, failed = (AttributeString(''), True, False)
    try:
        output = AttributeString(check_output(
            cmd, stderr=STDOUT, shell=True, executable=executable))
        if capture:
            log.info(output)
    except CalledProcessError as err:
        succeeded, failed = (False, True)
        if warn_only:
            log.warning(err.output)
        else:
            log.error(err.output)
            raise

    output.succeeded = succeeded
    output.failed = failed

    return output
