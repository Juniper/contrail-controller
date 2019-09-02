#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
VNC management for Mesos
"""
from __future__ import print_function
from __future__ import absolute_import

from builtins import str
from builtins import object
import gevent
from gevent.queue import Empty
import requests
from .vnc_mesos_config import VncMesosConfig as vnc_mesos_config
from cfgm_common import importutils
from cfgm_common import vnc_cgitb
from cfgm_common.exceptions import *
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_amqp import VncAmqpHandle
from vnc_api.vnc_api import *
from .config_db import *
from . import db
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
    _vnc_mesos = None
    def __init__(self, args=None, logger=None, queue=None, sync_queue=None):
        self.args = args
        self.logger = logger
        self.queue = queue
        self.sync_queue = sync_queue
        self._cluster_pod_task_ipam_fq_name = None
        self._cluster_ip_fabric_ipam_fq_name = None

        """Initialize vnc connection"""
        self.vnc_lib = self._vnc_connect()

        # Cache common config.
        self.vnc_mesos_config = vnc_mesos_config(logger=self.logger,
            vnc_lib=self.vnc_lib, args=self.args, queue=self.queue,
            sync_queue = self.sync_queue)

        # init access to db
        self._db = db.MesosNetworkManagerDB(self.args, self.logger)
        DBBaseMM.init(self, self.logger, self._db)

        # sync api server db in local cache
        self._sync_mm()

        # provision cluster
        self._provision_cluster()
        self.vnc_mesos_config.update(
         cluster_pod_task_ipam_fq_name=self._get_cluster_pod_task_ipam_fq_name(),
         cluster_ip_fabric_ipam_fq_name=self._get_cluster_ip_fabric_ipam_fq_name())
        self.pod_task_mgr = importutils.import_object(
            'mesos_manager.vnc.vnc_pod_task.VncPodTask')
        VncMesos._vnc_mesos = self

    def _sync_mm(self):
        for cls in list(DBBaseMM.get_obj_type_map().values()):
            for obj in cls.list_obj():
                cls.locate(obj['uuid'], obj)

    @staticmethod
    def reset():
        for cls in list(DBBaseMM.get_obj_type_map().values()):
            cls.reset()

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

    def _create_project(self, project_name):
        proj_fq_name = vnc_mesos_config.cluster_project_fq_name(project_name)
        proj_obj = Project(name=proj_fq_name[-1], fq_name=proj_fq_name)
        try:
            self.vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self.vnc_lib.project_read(
                fq_name=proj_fq_name)
        ProjectMM.locate(proj_obj.uuid)
        return proj_obj

    def _attach_policy(self, vn_obj, *policies):
        for policy in policies or []:
            vn_obj.add_network_policy(policy,
                VirtualNetworkPolicyType(sequence=SequenceType(0, 0)))
        self.vnc_lib.virtual_network_update(vn_obj)
        for policy in policies or []:
            self.vnc_lib.ref_relax_for_delete(vn_obj.uuid, policy.uuid)

    def _create_policy_entry(self, src_vn_obj, dst_vn_obj, src_np_obj=None):
        if src_vn_obj:
            src_addresses = [
                AddressType(virtual_network = src_vn_obj.get_fq_name_str())
            ]
        else:
            src_addresses = [
                AddressType(network_policy = src_np_obj.get_fq_name_str())
            ]
        return PolicyRuleType(
                direction = '<>',
                action_list = ActionListType(simple_action='pass'),
                protocol = 'any',
                src_addresses = src_addresses,
                src_ports = [PortType(-1, -1)],
                dst_addresses = [
                    AddressType(virtual_network = dst_vn_obj.get_fq_name_str())
                ],
                dst_ports = [PortType(-1, -1)])

    def _create_np_vn_policy(self, policy_name, proj_obj, dst_vn_obj):
        policy_exists = False
        policy = NetworkPolicy(name=policy_name, parent_obj=proj_obj)
        try:
            policy_obj = self.vnc_lib.network_policy_read(
                fq_name=policy.get_fq_name())
            policy_exists = True
        except NoIdError:
            # policy does not exist. Create one.
            policy_obj = policy
        network_policy_entries = PolicyEntriesType()
        policy_entry = self._create_policy_entry(None, dst_vn_obj, policy)
        network_policy_entries.add_policy_rule(policy_entry)
        policy_obj.set_network_policy_entries(network_policy_entries)
        if policy_exists:
            self.vnc_lib.network_policy_update(policy)
        else:
            self.vnc_lib.network_policy_create(policy)
        return policy_obj

    def _create_attach_policy(self, proj_obj, ip_fabric_vn_obj, pod_task_vn_obj):
        policy_name = vnc_mesos_config.cluster_name() + \
            '-default-ip-fabric-np'
        ip_fabric_policy = \
            self._create_np_vn_policy(policy_name, proj_obj, ip_fabric_vn_obj)
        self._attach_policy(ip_fabric_vn_obj, ip_fabric_policy)
        self._attach_policy(pod_task_vn_obj, ip_fabric_policy)

    def _create_network(self, vn_name, vn_type, proj_obj,
            ipam_obj, ipam_update, provider=None):
        # Check if the VN already exists.
        # If yes, update existing VN object with k8s config.
        vn_exists = False
        vn = VirtualNetwork(name=vn_name, parent_obj=proj_obj,
                 address_allocation_mode='flat-subnet-only')
        try:
            vn_obj = self.vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())
            vn_exists = True
        except NoIdError:
            # VN does not exist. Create one.
            vn_obj = vn

        # Attach IPAM to virtual network.
        #
        # For flat-subnets, the subnets are specified on the IPAM and
        # not on the virtual-network to IPAM link. So pass an empty
        # list of VnSubnetsType.
        if ipam_update or \
           not self._is_ipam_exists(vn_obj, ipam_obj.get_fq_name()):
            vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([]))

        vn_obj.set_virtual_network_properties(
             VirtualNetworkType(forwarding_mode='l3'))

        fabric_snat = False
        if vn_type == 'pod-task-network':
            fabric_snat = True

        if not vn_exists:
            if self.args.ip_fabric_forwarding:
                if provider:
                    #enable ip_fabric_forwarding
                    vn_obj.add_virtual_network(provider)
            elif fabric_snat and self.args.ip_fabric_snat:
                #enable fabric_snat
                vn_obj.set_fabric_snat(True)
            else:
                #disable fabric_snat
                vn_obj.set_fabric_snat(False)
            # Create VN.
            self.vnc_lib.virtual_network_create(vn_obj)
        else:
            self.vnc_lib.virtual_network_update(vn_obj)

        vn_obj = self.vnc_lib.virtual_network_read(
            fq_name=vn_obj.get_fq_name())
        VirtualNetworkMM.locate(vn_obj.uuid)
        return vn_obj

    def _create_ipam(self, ipam_name, subnets, proj_obj,
            type='flat-subnet'):
        ipam_obj = NetworkIpam(name=ipam_name, parent_obj=proj_obj)

        ipam_subnets = []
        for subnet in subnets:
            pfx, pfx_len = subnet.split('/')
            ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
            ipam_subnets.append(ipam_subnet)
        if not len(ipam_subnets):
            self.logger.error("%s - %s subnet is empty for %s" \
                 %(self._name, ipam_name, subnets))

        if type == 'flat-subnet':
            ipam_obj.set_ipam_subnet_method('flat-subnet')
            ipam_obj.set_ipam_subnets(IpamSubnets(ipam_subnets))

        ipam_update = False
        try:
            ipam_uuid = self.vnc_lib.network_ipam_create(ipam_obj)
            ipam_update = True
        except RefsExistError:
            curr_ipam_obj = self.vnc_lib.network_ipam_read(
                fq_name=ipam_obj.get_fq_name())
            ipam_uuid = curr_ipam_obj.get_uuid()
            if type == 'flat-subnet' and not curr_ipam_obj.get_ipam_subnets():
                self.vnc_lib.network_ipam_update(ipam_obj)
                ipam_update = True

        NetworkIpamMM.locate(ipam_uuid)
        return ipam_update, ipam_obj, ipam_subnets

    def _is_ipam_exists(self, vn_obj, ipam_fq_name, subnet=None):
        curr_ipam_refs = vn_obj.get_network_ipam_refs()
        if curr_ipam_refs:
            for ipam_ref in curr_ipam_refs:
                if ipam_fq_name == ipam_ref['to']:
                   if subnet:
                       # Subnet is specified.
                       # Validate that we are able to match subnect as well.
                       if len(ipam_ref['attr'].ipam_subnets) and \
                           subnet == ipam_ref['attr'].ipam_subnets[0].subnet:
                           return True
                   else:
                       # Subnet is not specified.
                       # So ipam-fq-name match will suffice.
                       return True
        return False

    def _allocate_fabric_snat_port_translation_pools(self):
        global_vrouter_fq_name = \
            ['default-global-system-config', 'default-global-vrouter-config']
        try:
            global_vrouter_obj = \
                self.vnc_lib.global_vrouter_config_read(
                    fq_name=global_vrouter_fq_name)
        except NoIdError:
            return
        snat_port_range = PortType(start_port = 56000, end_port = 57023)
        port_pool_tcp = PortTranslationPool(
            protocol="tcp", port_count='1024', port_range=snat_port_range)
        snat_port_range = PortType(start_port = 57024, end_port = 58047)
        port_pool_udp = PortTranslationPool(
            protocol="udp", port_count='1024', port_range=snat_port_range)
        port_pools = PortTranslationPools([port_pool_tcp, port_pool_udp])
        global_vrouter_obj.set_port_translation_pools(port_pools)
        try:
            self.vnc_lib.global_vrouter_config_update(global_vrouter_obj)
        except NoIdError:
            pass

    def _get_cluster_pod_task_ipam_fq_name(self):
        return self._cluster_pod_task_ipam_fq_name

    def _get_cluster_ip_fabric_ipam_fq_name(self):
        return self._cluster_ip_fabric_ipam_fq_name

    def _provision_cluster(self):
        ''' Pre creating default project before namespace add event.'''
        proj_obj = self._create_project('default')

        # Allocate fabric snat port translation pools.
        self._allocate_fabric_snat_port_translation_pools()

        ip_fabric_fq_name = vnc_mesos_config.cluster_ip_fabric_network_fq_name()
        ip_fabric_vn_obj = self.vnc_lib. \
            virtual_network_read(fq_name=ip_fabric_fq_name)

        # Create ip-fabric IPAM.
        ipam_name = vnc_mesos_config.cluster_name() + '-ip-fabric-ipam'
        ip_fabric_ipam_update, ip_fabric_ipam_obj, ip_fabric_ipam_subnets = \
            self._create_ipam(ipam_name, self.args.ip_fabric_subnets, proj_obj)
        self._cluster_ip_fabric_ipam_fq_name = ip_fabric_ipam_obj.get_fq_name()

        # Create Pod Task IPAM.
        ipam_name = vnc_mesos_config.cluster_name() + '-pod-task-ipam'
        pod_task_ipam_update, pod_task_ipam_obj, pod_task_ipam_subnets = \
            self._create_ipam(ipam_name, self.args.pod_task_subnets, proj_obj)
        # Cache cluster pod ipam name.
        # This will be referenced by ALL pods that are spawned in the cluster.
        self._cluster_pod_task_ipam_fq_name = pod_task_ipam_obj.get_fq_name()

        ''' Create a  default pod-task-network. '''
        if self.args.ip_fabric_forwarding:
            cluster_pod_task_vn_obj = self._create_network(
                vnc_mesos_config.cluster_default_pod_task_network_name(),
                'pod-task-network', proj_obj,
                ip_fabric_ipam_obj, ip_fabric_ipam_update, ip_fabric_vn_obj)
        else:
            cluster_pod_task_vn_obj = self._create_network(
                vnc_mesos_config.cluster_default_pod_task_network_name(),
                'pod-task-network', proj_obj,
                pod_task_ipam_obj, pod_task_ipam_update, ip_fabric_vn_obj)

        self._create_attach_policy(proj_obj, ip_fabric_vn_obj,
            cluster_pod_task_vn_obj)

    def vnc_process(self):
        """Process event from the work queue"""
        while True:
            try:
                event = self.queue.get()
                self.logger.info("VNC: Handle CNI Data for ContainerId: {}."
                                 .format(event['cid']))
                print ("VNC: Handle CNI Data for ContainerId: {}."
                                 .format(event['cid']))
                self.pod_task_mgr.process(event)
            except Empty:
                gevent.sleep(0)

