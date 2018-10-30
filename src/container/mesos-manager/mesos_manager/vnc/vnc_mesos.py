#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC management for Mesos
"""

import gevent
from gevent.queue import Empty
import requests
from cfgm_common import importutils
from cfgm_common import vnc_cgitb
from cfgm_common.exceptions import *
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_amqp import VncAmqpHandle
from vnc_api.vnc_api import *
from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.uve.virtual_network.ttypes import *
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, \
          NodeTypeNames, INSTANCE_ID_DEFAULT
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus

class VncMesos(object):
    "Class to handle vnc operations"
    def __init__(self, args=None, logger=None, queue=None):
        self.args = args
        self.logger = logger
        self.queue = queue

        """Initialize vnc connection"""
        self.vnc_lib = self._vnc_connect()

    def connection_state_update(self, status, message=None):
        ConnectionState.update(
            conn_type=ConnType.APISERVER, name='ApiServer',
            status=status, message=message or '',
            server_addrs=['%s:%s' % (self.args.vnc_endpoint_ip,
                                     self.args.vnc_endpoint_port)])
    # end connection_state_update

    def _vnc_connect(self):
        # Retry till API server connection is up
        connected = False
        self.connection_state_update(ConnectionStatus.INIT)
        api_server_list = self.args.vnc_endpoint_ip.split(',')
        while not connected:
            try:
                vnc_lib = VncApi(self.args.auth_user,
                    self.args.auth_password, self.args.auth_tenant,
                    api_server_list, self.args.vnc_endpoint_port,
                    auth_token_url=self.args.auth_token_url)
                connected = True
                self.connection_state_update(ConnectionStatus.UP)
            except requests.exceptions.ConnectionError as e:
                # Update connection info
                self.connection_state_update(ConnectionStatus.DOWN, str(e))
                time.sleep(3)
            except ResourceExhaustionError:
                time.sleep(3)
        return vnc_lib

    def process_q_event(self, event):
        """Process ADD/DEL event"""
        obj_labels = MesosCniLabels(event, self.logger)
        if obj_labels.operation == 'ADD':
            self.logger.info('Add request.')
        elif obj_labels.operation == 'DEL':
            self.logger.info('Delete request')
        else:
            self.logger.error('Invalid operation')

    def vnc_process(self):
        """Process event from the work queue"""
        while True:
            try:
                event = self.queue.get()
                self.logger.info("VNC: Handle CNI Data for ContainerId: {}."
                                 .format(event['cid']))
                print ("VNC: Handle CNI Data for ContainerId: {}."
                                 .format(event['cid']))
                self.process_q_event(event)
            except Empty:
                gevent.sleep(0)


class MesosCniLabels(object):
    """Handle label processing"""
    def __init__(self, event, logger):
        """Initialize all labels to default vaule"""
        self.logger = logger
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
            self.logger.info("Debug:{}{}{}{}{}{}{}"
                             .format(self.domain_name, self.project_name,
                                     self.networks, self.security_groups,
                                     self.floating_ips, self.cluster_name,
                                     self.app_subnets))
            self.logger.info("Extracting labels done")

