#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC pod task management for  mesos
"""

import json
import uuid

from cStringIO import StringIO
from cfgm_common.exceptions import RefsExistError, NoIdError
from cfgm_common.utils import cgitb_hook
from vnc_api.vnc_api import ( InstanceIp, VirtualMachine,
                             VirtualMachineInterface,
                             VirtualMachineInterfacePropertiesType)
from mesos_manager.vnc.config_db import (
    DBBaseMM, VirtualNetworkMM, VirtualRouterMM, VirtualMachineMM,
    VirtualMachineInterfaceMM, InstanceIpMM)
from mesos_manager.vnc.vnc_mesos_config import (
    VncMesosConfig as vnc_mesos_config)

from cStringIO import StringIO
from cfgm_common.utils import cgitb_hook

class VncPodTask():
    vnc_pod_task_instance = None

    def __init__(self):
        self._name = type(self).__name__
        self._vnc_lib = vnc_mesos_config.vnc_lib()
        self._queue = vnc_mesos_config.queue()
        self._args = vnc_mesos_config.args()
        self._logger = vnc_mesos_config.logger()
        if not VncPodTask.vnc_pod_task_instance:
            VncPodTask.vnc_pod_task_instance = self

    def process(self, event):
        """Process ADD/DEL event"""
        obj_labels = MesosCniLabels(event, self._logger)
        if obj_labels.operation == 'ADD':
            self._logger.info('Add request.')
        elif obj_labels.operation == 'DEL':
            self._logger.info('Delete request')
        else:
            self._logger.error('Invalid operation')

class MesosCniLabels(object):
    """Handle label processing"""
    def __init__(self, event, logger):
        """Initialize all labels to default vaule"""
        self._logger = logger
        self.operation = event['cmd']
        self.task_uuid = event['cid']
        self.domain_name = 'default-domain'
        self.project_name = 'mesos-system'
        self.cluster_name = ''
        self.networks = ''
        self.security_groups = ''
        self.floating_ips = ''
        self.app_subnets = '10.10.10.0/24'
        self._extract_values(event)

    def _extract_values(self, event):
            """Extract values from  args"""
            if 'app_subnets' in event.keys():
                self.app_subnets =  event['app_subnets']
            labels = event['labels']
            """Extract values from label"""
            if 'domain-name' in labels.keys():
                self.domain_name = labels['domain-name']
            if 'project-name' in labels.keys():
                self.project_name = labels['project-name']
            if 'networks' in labels.keys():
                self.networks = labels['networks']
            if 'app_subnets' in labels.keys():
                self.app_subnets =  labels['network-subnets']
            if 'security-groups' in labels.keys():
                self.security_groups = labels['security-groups']
            if 'floating-ips' in labels.keys():
                self.floating_ips = labels['floating-ips']
            if 'cluster-name' in labels.keys():
                self.cluster_name = labels['cluster-name']
            print ("Debug:{} {} {} {} {} {} {}"
                             .format(self.domain_name, self.project_name,
                                     self.networks, self.security_groups,
                                     self.floating_ips, self.cluster_name,
                                     self.app_subnets))
            self._logger.info("Extracting labels done")

