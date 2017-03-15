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

from sandesh_common.vns.constants import \
     ANALYTICS_API_SERVER_DISCOVERY_SERVICE_NAME as analytics_svc_name

@six.add_metaclass(abc.ABCMeta)
class VRouterScheduler(object):

    def __init__(self, vnc_lib, nova_client, disc, logger, args):
        self._vnc_lib = vnc_lib
        self._args = args
        self._nc = nova_client
        self._disc = disc
        self._logger = logger
        self._analytics_client_list = self._get_analytics_clients()

    def _get_analytics_clients(self):
        analytics_client_list = []
        analytics_servers = self._args.analytics_server_list[:]
        analytics_server_list = analytics_servers.split()
        for analytics_server in analytics_server_list or []:
            endpoint = "http://%s" % analytics_server
            obj = analytics_client.Client(endpoint)
            analytics_client_list.append(obj)
        return analytics_client_list

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
                                self._args.admin_tenant_name,
                                detailed=True)
        if not az_list:
            self._logger.error("Failed fetching azs from nova")
            return None

        az_vr_list = []
        for az in az_list:
            try:
                # check:
                # 1. If az is mentioned in config
                # 2. If the az is enabled & active
                # 3. If it has any hosts
                if az.zoneName not in self._args.netns_availability_zone or \
                   not az.zoneState['available'] or not az.hosts:
                    continue

                # check if hosts are active & enabled
                for host,host_status in az.hosts.iteritems():
                    if (('nova-compute' in host_status) and \
                        host_status['nova-compute']['available'] and \
                        host_status['nova-compute']['active']):
                        az_vr_list.append(host)
            except Exception as e:
                self._logger.error(str(e))
                continue

        return az_vr_list

    def query_uve(self, analytics, filter_string):
        path = "/analytics/uves/vrouter/"
        response_dict = {}
        if self._args.aaa_mode == 'no-auth':
            user_token = None
        else:
            user_token = self._vnc_lib.get_auth_token()
        response = analytics.request(path, filter_string,
                   user_token=user_token)
        for values in response['value']:
            response_dict[values['name']] = values['value']
        return response_dict

    def vrouters_running(self):
        # get az host list
        az_vrs = self._get_az_vrouter_list()

        # read all vrouter information
        client_cnt = len(self._analytics_client_list)
        analytics_client_list = random.sample(
                  self._analytics_client_list, client_cnt)
        analytics_response = False
        for analytics in analytics_client_list or []:
            try:
                vrouters_mode = self.query_uve(
                       analytics, "*?cfilt=VrouterAgent:mode")
                agents_status = self.query_uve(
                       analytics, "*?cfilt=NodeStatus:process_status")
                analytics_response = True
                break
            except Exception as e:
                error_msg = "Failed to get vrouter and agent info from " + \
                            "analytics endpoint %s" %analytics.endpoint
                self._logger.error(error_msg)
                self._logger.error(str(e))

        if not analytics_response:
            error_msg = "no response from analytics servers"
            self._logger.error(error_msg)
            return

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
                state_up = False
                for vr_status in agents_status[vr.name]['NodeStatus']['process_status'] or []:
                    if (vr_status['module_id'] != constants.MODULE_VROUTER_AGENT_NAME):
                        continue
                    if (int(vr_status['instance_id']) == 0 and
                            vr_status['state'] == 'Functional'):
                        vr.set_agent_state(True)
                        state_up = True
                        break
                if not state_up:
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
            'virtual-machine', vm.uuid, None, 'ADD')
        return chosen_vrouter
