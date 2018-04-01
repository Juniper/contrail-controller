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


def validate_job_ctx(job_ctx, results):
    if job_ctx.get('config_args') is None:
        results['msg'] = "Sandesh args not present in job_ctx"
        results['failed'] = True
    elif job_ctx.get('job_template_fqname') is None:
        results['msg'] = "Job template fqname not present in job_ctx"
        results['failed'] = True
    elif job_ctx.get('job_execution_id') is None:
        results['msg'] = "Job execution id not present in job_ctx"
        results['failed'] = True
    elif job_ctx.get('job_input') is None:
        results['msg'] = "Job input not present in job_ctx"
        results['failed'] = True

    return results


def send_prouter_object_log(prouter_fqname,
                            job_ctx,
                            os_version,
                            serial_num,
                            onboarding_state):
    results = {}
    results['failed'] = False

    results = validate_job_ctx(job_ctx, results)
    if results['failed']:
        return results

    try:
        job_log_util = JobLogUtils(
            sandesh_instance_id=str(
                uuid.uuid4()), config_args=json.dumps(
                job_ctx['config_args']))
        job_log_util.send_prouter_object_log(
            prouter_fqname,
            job_ctx['job_execution_id'],
            json.dumps(job_ctx['job_input']),
            job_ctx['job_template_fqname'],
            onboarding_state,
            os_version,
            serial_num)
        time.sleep(10)
    except Exception as ex:
        results['msg'] = str(ex)
        results['failed'] = True

    return results


def send_job_object_log(job_ctx,
                        message,
                        status,
                        result):
    results = {}
    results['failed'] = False

    results = validate_job_ctx(job_ctx, results)
    if results['failed']:
        return results

    try:
        job_log_util = JobLogUtils(
            sandesh_instance_id=str(
                uuid.uuid4()), config_args=json.dumps(
                job_ctx['config_args']))
        job_log_util.send_job_log(
            job_ctx['job_template_fqname'],
            job_ctx['job_execution_id'],
            message,
            status,
            result)
        time.sleep(10)
    except Exception as ex:
        results['msg'] = str(ex)
        results['failed'] = True

    return results
