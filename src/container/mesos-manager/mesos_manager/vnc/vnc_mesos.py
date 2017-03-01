#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC management for Mesos
"""

# Standard library import
import gevent
from gevent.queue import Empty
import requests

# Application library import
from cfgm_common import importutils
from cfgm_common import vnc_cgitb
from cfgm_common.vnc_amqp import VncAmqpHandle
import mesos_manager.mesos_consts as mesos_consts
from vnc_api.vnc_api import *
from config_db import *
from reaction_map import REACTION_MAP
from vnc_services import VncService


class MesosCniLabels(object):
    """Handle label processing"""
    def __init__(self, event, logger):
        """Initialize all labels to default vaule"""
        self.logger = logger
        self.operation = event['cmd']
        self.task_uuid = event['cid']
        self.domain_name = 'default-domain'
        self.project_name = 'meso-system'
        self.cluster_name = ''
        self.networks = ''
        self.security_groups = ''
        self.floating_ips = ''
        self._extract_values(event['labels'])

    def _extract_values(self, labels):
            """Extract values from  args label"""
            if 'domain-name' in labels.keys():
                self.domain_name = labels['domain-name']
            if 'project-name' in labels.keys():
                self.project_name = labels['project-name']
            if 'networks' in labels.keys():
                self.networks = labels['networks']
            if 'security-groups' in labels.keys():
                self.security_groups = labels['security-groups']
            if 'floating-ips' in labels.keys():
                self.floating_ips = labels['floating-ips']
            if 'cluster-name' in labels.keys():
                self.cluster_name = labels['cluster-name']
            self.logger.info("Debug:{}{}{}{}{}{}"
                             .format(self.domain_name, self.project_name,
                                     self.networks, self.security_groups,
                                     self.floating_ips, self.cluster_name))
            self.logger.info("Extracting labels done")


class VncMesos(object):
    "Class to handle vnc operations"
    def __init__(self, args=None, logger=None, queue=None):
        self.args = args
        self.logger = logger
        self.queue = queue

    def process_q_event(self, event):
        """Process ADD/DEL event"""
        obj_labels = MesosCniLabels(event, self.logger)
        if obj_labels.operation == 'ADD':
            vnc_service = VncService(self.args, self.logger)
            vnc_service.add_mesos_task_and_define_network(obj_labels)
        elif obj_labels.operation == 'DEL':
            vnc_service = VncService(self.args, self.logger)
            vnc_service.del_mesos_task_and_remove_network(obj_labels)
        else:
            self.logger.error('Invalid operation')

    def vnc_process(self):
        """Process event from the work queue"""
        while True:
            try:
                event = self.queue.get()
                self.logger.info("VNC: Handle CNI Data for ContainerId: {}."
                                 .format(event['cid']))
                self.process_q_event(event)
            except Empty:
                gevent.sleep(0)
