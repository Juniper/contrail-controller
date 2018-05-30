#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of job api handler code
"""
import gevent

class JobHandler(object):

    # job types
    JOB_TYPE_UNDERLAY = 1
    JOB_TYPE_OVERLAY = 2

    # job status
    JOB_STATUS_INIT = 1
    JOB_STATUS_IN_PROGRESS = 2
    JOB_STATUS_FAIL = 3
    JOB_STATUS_COMPLETE = 4

    # job status check wait interval
    JOB_STATUS_WAIT_INTERVAL = 4 #seconds

    def __init__(self, pr, job_type, config, logobj):
        self.pr_uuid = pr
        self.job_type = job_type
        self.job_config = config
        self.logger = logobj
        self.job_id = None
        self.job_status = self.JOB_STATUS_INIT
        super(JobHandler, self).__init__()
    # end __init__

    def push(self):
        self.logger.info("job handler: push for (%s, %s): " %(self.pr_uuid, str(self.job_type)))
        self.job_status = self.JOB_STATUS_IN_PROGRESS
        try:
            # invoke job api
            # TODO
            self.wait()
        except Exception as e:
            self.logger.info("job handler: push fail for (%s, %s): %s"%(self.pr_uuid, str(self.job_type), str(e))) 
            self.job_status = self.JOB_STATUS_FAIL   
    # end push

    def get_sandesh_status(self):
        # 0 : progress, -1: fail, 1: complete
        return 1
    # end get_sandesh_status

    def wait(self):
        #use sandesh API to check the job status
        while(True):
            # check status
            status = self.get_sandesh_status()
            if status == 1:
                self.job_status = self.JOB_STATUS_COMPLETE    
                break
            # if error, set job_status and return
            if status == -1: 
                job_status = self.JOB_STATUS_FAIL
                return
            # if not complete yet, sleep and try later
            if status == 0: 
                gevent.sleep(self.JOB_STATUS_WAIT_INTERVAL)
                continue
    # end wait

    def is_job_done(self):
        if self.job_status == self.JOB_STATUS_COMPLETE or self.job_status == self.JOB_STATUS_FAIL:
            return True
        return False
    # end is_job_done

# end JobHandler
