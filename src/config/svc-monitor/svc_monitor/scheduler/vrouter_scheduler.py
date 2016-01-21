# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright (c) 2014 Cloudwatt
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# @author: Edouard Thuleau, Cloudwatt.

import abc
import ast
from distutils.version import StrictVersion as V
import random
import six

from cfgm_common import analytics_client
from cfgm_common import svc_info
from sandesh_common.vns import constants
from vnc_api.vnc_api import NoIdError

from svc_monitor.config_db import *

@six.add_metaclass(abc.ABCMeta)
class VRouterScheduler(object):

    def __init__(self, vnc_lib, nova_client, args):
        self._vnc_lib = vnc_lib
        self._args = args
        self._nc = nova_client

        # initialize analytics client
        endpoint = "http://%s:%s" % (self._args.analytics_server_ip,
                                     self._args.analytics_server_port)
        self._analytics = analytics_client.Client(endpoint)

    @abc.abstractmethod
    def schedule(self, plugin, context, router_id, candidates=None):
        """Schedule the virtual machine to an active vrouter agent.

        Schedule the virtual machine only if it is not already scheduled and
        there is no other virtual machines of the same service instance already
        scheduled on the vrouter.
        """
        pass

    def _get_az_vrouter_list(self):
        if not self._args.netns_availability_zone:
            return None
        az_list = self._nc.oper('availability_zones', 'list',
            'admin', detailed=True)
        az_vr_list = []
        for az in az_list:
            if self._args.netns_availability_zone not in str(az):
                continue
            az_vr_list.extend(az.hosts.keys())

        return az_vr_list

    def query_uve(self, filter_string):
        path = "/analytics/uves/vrouter/"
        response_dict = {}
        try:
            response = self._analytics.request(path, filter_string)
            for values in response['value']:
                response_dict[values['name']] = values['value']
        except analytics_client.OpenContrailAPIFailed:
            pass
        return response_dict

    def vrouters_running(self):
        # get az host list
        az_vrs = self._get_az_vrouter_list()

        # read all vrouter information
        agents_status = self.query_uve("*?cfilt=NodeStatus:process_status")
        vrouters_mode = self.query_uve("*?cfilt=VrouterAgent:mode")

        for vr in VirtualRouterSM.values():
            if az_vrs and vr.name not in az_vrs:
                vr.set_agent_state(False)
                continue

            if vr.name not in vrouters_mode or vr.name not in agents_status:
                vr.set_agent_state(False)
                continue

            try:
                vr_mode = vrouters_mode[vr.name]['VrouterAgent']
                if (vr_mode['mode'] != constants.VrouterAgentTypeMap[
                        constants.VrouterAgentType.VROUTER_AGENT_EMBEDDED]):
                    vr.set_agent_state(False)
                    continue
            except Exception as e:
                vr.set_agent_state(False)
                continue

            try:
                vr_status = agents_status[vr.name]['NodeStatus']['process_status'][0]
                if (vr_status['module_id'] == constants.MODULE_VROUTER_AGENT_NAME and
                        int(vr_status['instance_id']) == 0 and
                        vr_status['state'] == 'Functional'):
                    vr.set_agent_state(True)
                else:
                    vr.set_agent_state(False)
            except Exception as e:
                vr.set_agent_state(False)
                continue

    def _get_candidates(self, si, vm):
        if vm.virtual_router:
            return [vm.virtual_router]

        vr_list = VirtualRouterSM._dict.keys()
        for vm_id in si.virtual_machines:
            if vm_id == vm.uuid:
                continue
            anti_affinity_vm = VirtualMachineSM.get(vm_id)
            if anti_affinity_vm:
                try:
                    vr_list.remove(anti_affinity_vm.virtual_router)
                except ValueError:
                    pass

        for vr in VirtualRouterSM.values():
            if not vr.agent_state:
                try:
                    vr_list.remove(vr.uuid)
                except ValueError:
                    pass
        return vr_list

class RandomScheduler(VRouterScheduler):
    """Randomly allocate a vrouter agent for virtual machine of a service
    instance."""
    def schedule(self, si, vm):
        candidates = self._get_candidates(si, vm)
        if not candidates:
            return None
        chosen_vrouter = random.choice(candidates)
        self._vnc_lib.ref_update('virtual-router', chosen_vrouter,
            'virtual-machine', vm_uuid, None, 'ADD')
        return chosen_vrouter
