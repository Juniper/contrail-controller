from enum import Enum
from time import gmtime, strftime
import traceback

from sandesh.job.ttypes import *
from job_exception import JobException

class JobStatus(Enum):
    STARTING = 0
    IN_PROGRESS = 1
    SUCCESS = 2
    FAILURE = 3
    TIMEOUT = 4

def send_job_log(job_template_id, job_execution_id, logger, message, status, result=None, timestamp=None):
    try:
        if timestamp is None:
            timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
        job_log_entry = JobLogEntry(job_id=job_template_id, execution_id=job_execution_id,
                                    timestamp=timestamp,  message=message, status=status, result=result)
        job_log = JobLog(log_entry=job_log_entry)
        job_log.send(sandesh=logger._sandesh)
        logger.debug("Created job log for job template: %s, execution id: %s,  status: %s, result: %s, message: %s" %
                     (job_template_id, job_execution_id, status, result, message))
    except Exception as e:
        msg = "Error while creating the job log for job template %s and execution id %s : %s" % \
              (job_template_id, job_execution_id, repr(e))
        logger.error(msg)
        logger.error(traceback.print_stack())
        raise JobException(msg)


def send_job_execution_uve(job_template_id, job_execution_id, logger, timestamp=None,
                           percentage_completed=None):
    try:
        if timestamp is None:
            timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
        job_exe_data = JobExecution(name=job_template_id, execution_id=job_execution_id,
                                    job_start_ts=timestamp, percentage_completed=percentage_completed)
        job_uve = UveJobExecution(data=job_exe_data)
        job_uve.send(sandesh=logger._sandesh)
    except Exception as e:
        msg = "Error while sending the job execution UVE for job template %s and execution id %s : %s" % \
              (job_template_id, job_execution_id, repr(e))
        logger.error(msg)
        logger.error(traceback.print_stack())
        raise JobException(msg)


def read_job_template(vnc_api, job_template_id, logger):
    try:
        job_template = vnc_api.job_template_read(id=job_template_id)
        logger.debug("Read job template %s from database" % job_template_id)
    except Exception as e:
        msg = "Error while reading the job template %s from database" % job_template_id
        logger.error(msg)
        logger.error(traceback.print_stack())
        raise JobException(msg)
    return job_template


def get_device_family(vnc_api, device_id, logger):
    try:
        pr = vnc_api.physical_router_read(id=device_id)
        logger.debug("Read device family as %s for device %s" % (pr.get_physical_router_device_family(), device_id))
    except Exception as e:
        msg = "Error while reading the device family from DB for device %s " % device_id
        logger.error(msg)
        logger.error(traceback.print_stack())
        raise JobException(msg)
    return pr.get_physical_router_device_family()


