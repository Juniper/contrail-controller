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
import time

from cfgm_common import analytics_client
from cfgm_common import svc_info
from vnc_api.vnc_api import NoIdError

@six.add_metaclass(abc.ABCMeta)
class VRouterScheduler(object):

    def __init__(self, logger, vnc_lib, args):
        self._logger = logger
        self._vnc_lib = vnc_lib
        self._args = args
        self._vrouter_precedent_status = {}

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
        vrs_fq_name = [vr_fq_name for vr_fq_name in vrs_fq_name
                       if self.vrouter_check_version(
                           vr_fq_name[-1],
                           svc_info._VROUTER_NETNS_SUPPORTED_VERSION)]

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

    def vrouter_running(self, vrouter_name, retry=0):
        """Check if a vrouter agent is up and running."""
        path = "/analytics/uves/vrouter/"
        fqdn_uuid = "%s?cfilt=NodeStatus" % vrouter_name

        try:
            vrouter_status = self._analytics.request(path, fqdn_uuid)
        except analytics_client.OpenContrailAPIFailed:
            self._logger.log("Fail to request analytics API to get "
                "vrouter '%s' agent status: %s" % (vrouter_name, e))
            self._logger.log(
                "Consider vrouter '%s' not available for scheduling" %\
                vrouter_name)
            return False

        if not vrouter_status or 'NodeStatus' not in vrouter_status or \
                'process_status' not in vrouter_status['NodeStatus']:
            self._logger.log("vrouter %s UVE status does not contains "
                "all informations" % vrouter_name)
            self._logger.log(
                "Consider vrouter '%s' not available for scheduling" %\
                vrouter_name)
            return False

        default_vrouter_status = {'count': 0, 'timestamp': 0}
        vrouter_last_status = self._vrouter_precedent_status.get(
            vrouter_name, default_vrouter_status)
        fails_count = vrouter_last_status['count']
        for process in vrouter_status['NodeStatus']['process_status']:
            if (process['module_id'] == 'VRouterAgent' and
                int(process['instance_id']) == 0 and
                process['state'] == 'Functional'):
                self._vrouter_precedent_status[vrouter_name] = \
                    default_vrouter_status
                return True

        # When retry is set to 0, we are trying to schedule a new VM on vrouter
        if retry == 0:
            return False

        if (int(time.time()) - vrouter_last_status['timestamp']) > \
            self._args.cleanup_delay:
            fails_count += 1
        self._vrouter_precedent_status[vrouter_name] = \
            {'count': fails_count,
             'timestamp': int(time.time())}
        reason = vrouter_status['NodeStatus']['process_status'][0]['description']
        self._logger.log("vrouter '%s' is not functional. Reason: %s" %\
                         (vrouter_name, reason))
        self._logger.log("That's happened %d times consecutively" %\
                         fails_count)
        if fails_count > retry:
            self._logger.log(
                "Consider vrouter '%s' not available for scheduling "
                "(retried: %d/%d)" % (vrouter_name, fails_count, retry))
            return False
        self._logger.log("Consider vrouter '%s' available for scheduling "
                         "(retry %d/%d)" % (vrouter_name, fails_count, retry))
        return True

    def vrouter_check_version(self, vrouter_name, version):
        """Check the vrouter version is upper or equal to a desired version."""
        path = "/analytics/uves/vrouter/"
        fqdn_uuid = "%s?cfilt=VrouterAgent" % vrouter_name

        try:
            vrouter_agent = self._analytics.request(path, fqdn_uuid)
        except analytics_client.OpenContrailAPIFailed:
            return False

        if not vrouter_agent:
            return False

        try:
            build_info = ast.literal_eval(
                vrouter_agent['VrouterAgent']['build_info'])
            vrouter_version = V(build_info['build-info'][0]['build-version'])
            requested_version = V(version)
        except KeyError, ValueError:
            return False

        return vrouter_version >= requested_version

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
