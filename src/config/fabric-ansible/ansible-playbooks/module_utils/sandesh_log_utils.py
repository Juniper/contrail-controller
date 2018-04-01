#!/usr/bin/python


# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains utility functions for sending prouter and job objectlogs
via sandesh
"""

import uuid
import time
import json

from job_manager.job_log_utils import JobLogUtils


def send_prouter_object_log(prouter_fqname,
                            config_args,
                            job_execution_id,
                            job_template_fqname,
                            job_input,
                            os_version,
                            serial_num,
                            onboarding_state):
    results = {}
    results['failed'] = False

    try:
        job_log_util = JobLogUtils(sandesh_instance_id=str(uuid.uuid4()),
                                   config_args=json.dumps(config_args))
        job_log_util.send_prouter_object_log(
            prouter_fqname,
            job_execution_id,
            json.dumps(job_input),
            job_template_fqname,
            onboarding_state,
            os_version,
            serial_num)
        time.sleep(10)
    except Exception as ex:
        results['msg']=str(ex)
        results['failed']=True

    return results

def send_job_object_log(config_args,
                        job_execution_id,
                        job_template_fqname,
                        message,
                        status,
                        result):
    results={}
    results['failed']=False

    try:
        job_log_util=JobLogUtils(sandesh_instance_id = str(uuid.uuid4()),
                                   config_args = json.dumps(config_args))
        job_log_util.send_job_log(
            job_template_fqname,
            job_execution_id,
            message,
            status,
            result)
        time.sleep(10)
    except Exception as ex:
        results['msg']=str(ex)
        results['failed']=True

    return results
