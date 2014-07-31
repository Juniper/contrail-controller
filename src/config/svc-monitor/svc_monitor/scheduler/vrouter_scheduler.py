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
import random
import six

from cfgm_common import analytics_client
from vnc_api.vnc_api import NoIdError

@six.add_metaclass(abc.ABCMeta)
class VRouterScheduler(object):

    def __init__(self, vnc_lib, args):
        self._vnc_lib = vnc_lib
        self._args = args

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

    def _get_candidates(self, si_uuid, vm_uuid):
        """Return vrouter agents where a service instance virtual machine
        could be scheduled.

        If a VM of a same service instance is already scheduled on the vrouter,
        that vrouter is exclude from the candidates list.
        If the VM is already scheduled on a running vrouter, only that vrouter
        is return in the candidates list.
        """

        vrs_fq_name = [vr['fq_name'] for vr in
                       self._vnc_lib.virtual_routers_list()['virtual-routers']
                       if self.vrouter_running(vr['fq_name'][-1])]
        for vr_fq_name in vrs_fq_name:
            try:
                vr_obj = self._vnc_lib.virtual_router_read(fq_name=vr_fq_name)
            except NoIdError:
                vrs_fq_name.remove(vr_fq_name)
                continue
            for vm_ref in vr_obj.get_virtual_machine_refs() or []:
                if vm_uuid == vm_ref['uuid']:
                    return [vr_fq_name]
                try:
                    vm_obj = self._vnc_lib.virtual_machine_read(
                        id=vm_ref['uuid'])
                except NoIdError:
                    continue
                if si_uuid in [si['uuid'] for si in
                               vm_obj.get_service_instance_refs()]:
                    vrs_fq_name.remove(vr_fq_name)
                    continue
        return vrs_fq_name

    def vrouter_running(self, vrouter_name):
        """Check if a vrouter agent is up and running."""
        path = "/analytics/uves/vrouter/"
        fqdn_uuid = "%s?cfilt=NodeStatus" % vrouter_name

        try:
            vrouter_status = self._analytics.request(path, fqdn_uuid)
        except analytics_client.OpenContrailAPIFailed:
            return False

        if not vrouter_status:
            return False

        for process in vrouter_status['NodeStatus']['process_status']:
            if (process['module_id'] == 'VRouterAgent' and
                int(process['instance_id']) == 0 and
                process['state'] == 'Functional'):
                return True
        return False

    def _bind_vrouter(self, vm_uuid, vr_fq_name):
        """Bind the virtual machine to the vrouter which has been chosen."""
        vm_obj = self._vnc_lib.virtual_machine_read(id=vm_uuid)
        vr_obj = self._vnc_lib.virtual_router_read(fq_name=vr_fq_name)
        vr_obj.add_virtual_machine(vm_obj)
        self._vnc_lib.virtual_router_update(vr_obj)


class RandomScheduler(VRouterScheduler):
    """Randomly allocate a vrouter agent for virtual machine of a service
    instance."""

    def schedule(self, si_uuid, vm_uuid):
        candidates = self._get_candidates(si_uuid, vm_uuid)
        if not candidates:
            return
        chosen_vrouter = random.choice(candidates)
        self._bind_vrouter(vm_uuid, chosen_vrouter)
        return chosen_vrouter