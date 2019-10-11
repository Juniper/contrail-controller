#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from future import standard_library
standard_library.install_aliases()
from builtins import object
import requests
import json
import gevent
import time
from gevent.queue import Empty

from six import StringIO
from cfgm_common.exceptions import RefsExistError, NoIdError
from cfgm_common.utils import cgitb_hook
#Fix me: remove non dependent import
from vnc_api.vnc_api import ( InstanceIp, VirtualMachine,
                             VirtualMachineInterface,
                             VirtualMachineInterfacePropertiesType,
                             KeyValuePair)
from mesos_manager.vnc.config_db import (
    DBBaseMM, VirtualNetworkMM, VirtualRouterMM, VirtualMachineMM,
    VirtualMachineInterfaceMM, InstanceIpMM)
from mesos_manager.vnc.vnc_common import VncCommon
from mesos_manager.vnc.vnc_mesos_config import (
    VncMesosConfig as vnc_mesos_config)

from cfgm_common.utils import cgitb_hook

class PodTaskMonitor(object):
    def __init__(self, args=None, logger=None, q=None):
        self.args = args
        self.logger = logger
        self.queue = q
        self._vnc_lib = vnc_mesos_config.vnc_lib()

    @staticmethod
    def cleanup_json(data):
        if isinstance(data, dict):
            return {k: PodTaskMonitor.cleanup_json(v) for k, v in list(data.items()) if v is not None}
        if isinstance(data, list):
            return [PodTaskMonitor.cleanup_json(e) for e in data]
        return data

    @staticmethod
    def api_req_raw(method, ip_addr, port, path, auth=None, body=None, **kwargs):
        path_str = 'http://%s:%s' % (ip_addr, port)
        payload = { 'type': 'GET_CONTAINERS' }
        for path_elem in path:
            path_str = path_str + "/" + path_elem
        response = requests.request(
            method,
            path_str,
            auth=auth,
            json=payload,
            headers={
                'Accept': 'application/json',
                'Content-Type': 'application/json'
            },
            timeout=(3.05, 46),
            **kwargs
        )

        #logger.debug("%s %s", method, response.url)
        #if response.status_code == 200:
        #    break

        response.raise_for_status()

        resp_json = PodTaskMonitor.cleanup_json(response.json())
        if 'message' in resp_json:
            response.reason = "%s (%s)" % (
                response.reason,
                resp_json['message'])
        return response

    @staticmethod
    def get_task(node_ip):
        data = PodTaskMonitor.api_req_raw('POST', node_ip, '5051',
                                          ['api', 'v1']).json()
        return PodTaskMonitor.cleanup_json(data)

    @staticmethod
    def get_task_pod_name_from_cid(cid, node_ip):
        result = PodTaskMonitor.get_task(node_ip)
        for container_info in result['get_containers']['containers']:
            if container_info['container_id']['value'] == cid:
                return container_info['executor_id']['value']
        return None

    def process_event(self, event):
        event_type = event['event_type']
        node_ip = event['labels']['node-ip']
        found = True;
        interval = vnc_mesos_config.get_mesos_agent_retry_sync_count()
        while (interval > 0) and found:
            time.sleep(vnc_mesos_config.get_mesos_agent_retry_sync_hold_time())
            result = PodTaskMonitor.get_task(node_ip)
            for container_info in result['get_containers']['containers']:
                if container_info['container_id']['value'] == event_type.encode('utf-8'):
                    task_name = container_info['executor_id']['value']
                    vm = VirtualMachineMM.find_by_name_or_uuid(container_info['container_id']['value'])
                    if not vm:
                        # It is possible our cache may not have the VN yet. Locate it.
                        vm = VirtualMachineMM.locate(container_info['container_id']['value'])
                    vm_obj = self._vnc_lib.virtual_machine_read(fq_name=vm.fq_name)
                    vm_obj.display_name = task_name
                    vm_uuid = self._vnc_lib.virtual_machine_update(vm_obj)
                    VirtualMachineMM.locate(vm_uuid)
                    if vm_obj:
                        found = False
            interval -= 1

    def sync_process(self):
        """Process event from the work queue"""
        while True:
            try:
                event = self.queue.get()
                self.process_event(event)
            except Empty:
                gevent.sleep(0)
