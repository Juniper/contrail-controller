#!/usr/bin/python

import traceback
import sys

from job_manager.job_utils import JobVncApi

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import FilterLog, _task_log, _task_done, \
    _task_error_log


class FilterModule(object):
    def filters(self):
        return {
            'get_mgmt_ip_frm_lo_ip': self.get_mgmt_ip_frm_lo_ip
        }
    # end filters

    def get_mgmt_ip_frm_lo_ip(self, job_ctx, lo0_ip, device_name):
        FilterLog.instance("GetMgmtIPFilter", device_name)
        _task_log("Starting to get the mgmt_ip")
        mgmt_ip = ''
        try:
            _task_log("Getting the vnc handle")
            self.vnc_lib = JobVncApi.vnc_init(job_ctx)
            mgmt_ip = self._get_mgmt_ip(lo0_ip)
            _task_done()
            return {
                'status': 'success',
                'get_mgmt_ip_log': FilterLog.instance().dump(),
                'get_mgmt_ip': mgmt_ip
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'get_mgmt_ip': mgmt_ip,
                    'get_mgmt_ip_log': FilterLog.instance().dump()}

    # end get_mgmt_ip_frm_lo_ip

    def _get_mgmt_ip(self, lo0_ip):
        # This should ideally be an array of just one device
        physical_routers_map = self.vnc_lib.physical_routers_list(
            fields = ["physical_router_management_ip"],
            filters={"physical_router_loopback_ip": lo0_ip})
        physical_routers_list = physical_routers_map.get('physical-routers')
        return physical_routers_list[0].get('physical_router_management_ip') \
            if physical_routers_list else ''
# end FilterModule