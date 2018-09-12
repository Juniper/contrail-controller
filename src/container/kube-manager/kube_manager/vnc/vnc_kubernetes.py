#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC management for kubernetes
"""

import gevent
from gevent.queue import Empty

import requests
import argparse
import uuid

from cStringIO import StringIO
from cfgm_common import importutils
from cfgm_common import vnc_cgitb
from cfgm_common.exceptions import *
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_amqp import VncAmqpHandle
from vnc_api.vnc_api import *
import kube_manager.common.args as kube_args
from config_db import *
import db
import label_cache
from label_cache import XLabelCache
from reaction_map import REACTION_MAP
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_common import VncCommon
import flow_aging_manager
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
from vnc_security_policy import VncSecurityPolicy

class VncKubernetes(VncCommon):

    _vnc_kubernetes = None

    def __init__(self, args=None, logger=None, q=None, kube=None,
                 vnc_kubernetes_config_dict=None):
        self._name = type(self).__name__
        self.args = args
        self.logger = logger
        self.q = q
        self.kube = kube
        self._cluster_pod_ipam_fq_name = None
        self._cluster_service_ipam_fq_name = None
        self._cluster_ip_fabric_ipam_fq_name = None

        # init vnc connection
        self.vnc_lib = self._vnc_connect()

        # Cache common config.
        self.vnc_kube_config = vnc_kube_config(logger=self.logger,
            vnc_lib=self.vnc_lib, args=self.args, queue=self.q, kube=self.kube)

        #
        # In nested mode, kube-manager connects to contrail components running
        # in underlay via global link local services. TCP flows established on
        # link local services will be torn down by vrouter, if there is no
        # activity for configured(or default) timeout. So disable flow timeout
        # on these connections, so these flows will persist.
        #
        # Note: The way to disable flow timeout is to set timeout to max
        #       possible value.
        #
        if self.args.nested_mode is '1':
            if self.args.db_driver == db.DRIVER_CASS:
                for cassandra_server in self.args.cassandra_server_list:
                    cassandra_port = cassandra_server.split(':')[-1]
                    flow_aging_manager.create_flow_aging_timeout_entry(
                        self.vnc_lib, "tcp", cassandra_port, 2147483647)
            elif self.args.db_driver == db.DRIVER_ETCD:
                etcd_host, etcd_port = self.args.etcd_server.split(':')
                flow_aging_manager.create_flow_aging_timeout_entry(
                    self.vnc_lib, "tcp", etcd_port, 2147483647)

            if self.args.rabbit_port:
                flow_aging_manager.create_flow_aging_timeout_entry(
                    self.vnc_lib, "tcp", self.args.rabbit_port, 2147483647)

            if self.args.vnc_endpoint_port:
                flow_aging_manager.create_flow_aging_timeout_entry(
                    self.vnc_lib, "tcp", self.args.vnc_endpoint_port, 2147483647)

            for collector in self.args.collectors:
                collector_port = collector.split(':')[-1]
                flow_aging_manager.create_flow_aging_timeout_entry(self.vnc_lib,
                    "tcp", collector_port, 2147483647)

        # init access to db
        self._db = db.KubeNetworkManagerDB(self.args, self.logger)
        DBBaseKM.init(self, self.logger, self._db)

        # If nested mode is enabled via config, then record the directive.
        if self.args.nested_mode is '1':
            DBBaseKM.set_nested(True)

        # sync api server db in local cache
        self._sync_km()

        # init rabbit connection
        rabbitmq_cfg = kube_args.rabbitmq_args(self.args)
        self.rabbit = VncAmqpHandle(self.logger._sandesh, self.logger, DBBaseKM,
            REACTION_MAP, 'kube_manager', rabbitmq_cfg)
        self.rabbit.establish()
        self.rabbit._db_resync_done.set()

        # Register label add and delete callbacks with label management entity.
        XLabelCache.register_label_add_callback(VncKubernetes.create_tags)
        XLabelCache.register_label_delete_callback(VncKubernetes.delete_tags)

        # Instantiate and init Security Policy Manager.
        self.security_policy_mgr = VncSecurityPolicy(self.vnc_lib,
                                                     VncKubernetes.get_tags)

        # provision cluster
        self._provision_cluster()

        if vnc_kubernetes_config_dict:
            self.vnc_kube_config.update(**vnc_kubernetes_config_dict)
        else:
            # Update common config.
            self.vnc_kube_config.update(
                cluster_pod_ipam_fq_name=self._get_cluster_pod_ipam_fq_name(),
                cluster_service_ipam_fq_name=self._get_cluster_service_ipam_fq_name(),
                cluster_ip_fabric_ipam_fq_name=self._get_cluster_ip_fabric_ipam_fq_name())

        # handle events
        self.label_cache = label_cache.LabelCache()
        self.vnc_kube_config.update(label_cache=self.label_cache)

        self.tags_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_tags.VncTags')
        self.network_policy_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_network_policy.VncNetworkPolicy')
        self.namespace_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_namespace.VncNamespace',
            self.network_policy_mgr)
        self.ingress_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_ingress.VncIngress', self.tags_mgr)
        self.service_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_service.VncService', self.ingress_mgr)
        self.pod_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_pod.VncPod', self.service_mgr,
            self.network_policy_mgr)
        self.endpoints_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_endpoints.VncEndpoints')

        # Create system default security policies.
        VncSecurityPolicy.create_deny_all_security_policy()
        VncSecurityPolicy.create_allow_all_security_policy()
        self.ingress_mgr.create_ingress_security_policy()

        VncKubernetes._vnc_kubernetes = self

        # Associate cluster with the APS.
        VncSecurityPolicy.tag_cluster_application_policy_set()

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

    def _sync_km(self):
        for cls in DBBaseKM.get_obj_type_map().values():
            for obj in cls.list_obj():
                cls.locate(obj['uuid'], obj)

    @staticmethod
    def reset():
        for cls in DBBaseKM.get_obj_type_map().values():
            cls.reset()

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

    def _create_vn_vn_policy(self, policy_name, proj_obj, *vn_obj):
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
        total_vn = len(vn_obj)
        for i in range(0, total_vn):
            for j in range(i+1, total_vn):
                policy_entry = self._create_policy_entry(vn_obj[i], vn_obj[j])
                network_policy_entries.add_policy_rule(policy_entry)
        policy_obj.set_network_policy_entries(network_policy_entries)
        if policy_exists:
            self.vnc_lib.network_policy_update(policy)
        else:
            self.vnc_lib.network_policy_create(policy)
        return policy_obj

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

    def _create_attach_policy(self, proj_obj, ip_fabric_vn_obj,
            pod_vn_obj, service_vn_obj, cluster_vn_obj):
        policy_name = vnc_kube_config.cluster_name() + \
            '-default-ip-fabric-np'
        ip_fabric_policy = \
            self._create_np_vn_policy(policy_name, proj_obj, ip_fabric_vn_obj)
        policy_name = vnc_kube_config.cluster_name() + \
            '-default-service-np'
        cluster_service_network_policy = \
            self._create_np_vn_policy(policy_name, proj_obj, service_vn_obj)
        policy_name = vnc_kube_config.cluster_name() + \
            '-default-pod-service-np'
        cluster_default_policy = self._create_vn_vn_policy(policy_name,
            proj_obj, pod_vn_obj, service_vn_obj)
        self._attach_policy(ip_fabric_vn_obj, ip_fabric_policy)
        self._attach_policy(pod_vn_obj,
            ip_fabric_policy, cluster_default_policy)
        self._attach_policy(service_vn_obj, ip_fabric_policy,
            cluster_service_network_policy, cluster_default_policy)

        # In nested mode, create and attach a network policy to the underlay
        # virtual network.
        if DBBaseKM.is_nested() and cluster_vn_obj:
            policy_name = vnc_kube_config.cluster_nested_underlay_policy_name()
            nested_underlay_policy = self._create_np_vn_policy(policy_name,
                                         proj_obj, cluster_vn_obj)
            self._attach_policy(cluster_vn_obj, nested_underlay_policy)

    def _create_project(self, project_name):
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(project_name)
        proj_obj = Project(name=proj_fq_name[-1], fq_name=proj_fq_name)
        try:
            self.vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self.vnc_lib.project_read(
                fq_name=proj_fq_name)
        ProjectKM.locate(proj_obj.uuid)
        return proj_obj

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

        # Cache ipam info.
        NetworkIpamKM.locate(ipam_uuid)

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

    def _provision_cluster(self):
        # Pre creating default project before namespace add event.
        proj_obj = self._create_project('default')

        # Create application policy set for the cluster project.
        VncSecurityPolicy.create_application_policy_set(
            vnc_kube_config.application_policy_set_name())

        # Allocate fabric snat port translation pools.
        self._allocate_fabric_snat_port_translation_pools()

        ip_fabric_fq_name = vnc_kube_config.cluster_ip_fabric_network_fq_name()
        ip_fabric_vn_obj = self.vnc_lib. \
            virtual_network_read(fq_name=ip_fabric_fq_name)

        cluster_vn_obj = None
        if DBBaseKM.is_nested():
            try:
                cluster_vn_obj = self.vnc_lib.virtual_network_read(
                    fq_name=vnc_kube_config.cluster_default_network_fq_name())
            except NoIdError:
                pass

        # Pre creating kube-system project before namespace add event.
        self._create_project('kube-system')
        # Create ip-fabric IPAM.
        ipam_name = vnc_kube_config.cluster_name() + '-ip-fabric-ipam'
        ip_fabric_ipam_update, ip_fabric_ipam_obj, ip_fabric_ipam_subnets = \
            self._create_ipam(ipam_name, self.args.ip_fabric_subnets, proj_obj)
        self._cluster_ip_fabric_ipam_fq_name = ip_fabric_ipam_obj.get_fq_name()
        # Create Pod IPAM.
        ipam_name = vnc_kube_config.cluster_name() + '-pod-ipam'
        pod_ipam_update, pod_ipam_obj, pod_ipam_subnets = \
            self._create_ipam(ipam_name, self.args.pod_subnets, proj_obj)
        # Cache cluster pod ipam name.
        # This will be referenced by ALL pods that are spawned in the cluster.
        self._cluster_pod_ipam_fq_name = pod_ipam_obj.get_fq_name()
        # Create a cluster-pod-network.
        if self.args.ip_fabric_forwarding:
            cluster_pod_vn_obj = self._create_network(
                vnc_kube_config.cluster_default_pod_network_name(),
                'pod-network', proj_obj,
                ip_fabric_ipam_obj, ip_fabric_ipam_update, ip_fabric_vn_obj)
        else:
            cluster_pod_vn_obj = self._create_network(
                vnc_kube_config.cluster_default_pod_network_name(),
                'pod-network', proj_obj,
                pod_ipam_obj, pod_ipam_update, ip_fabric_vn_obj)
        # Create Service IPAM.
        ipam_name = vnc_kube_config.cluster_name() + '-service-ipam'
        service_ipam_update, service_ipam_obj, service_ipam_subnets = \
            self._create_ipam(ipam_name, self.args.service_subnets, proj_obj)
        self._cluster_service_ipam_fq_name = service_ipam_obj.get_fq_name()
        # Create a cluster-service-network.
        cluster_service_vn_obj = self._create_network(
            vnc_kube_config.cluster_default_service_network_name(),
            'service-network', proj_obj, service_ipam_obj, service_ipam_update)
        self._create_attach_policy(proj_obj, ip_fabric_vn_obj,
            cluster_pod_vn_obj, cluster_service_vn_obj, cluster_vn_obj)

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
        if vn_type == 'pod-network':
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
        VirtualNetworkKM.locate(vn_obj.uuid)

        return vn_obj

    def _get_cluster_network(self):
        return VirtualNetworkKM.find_by_name_or_uuid(
            vnc_kube_config.cluster_default_network_name())

    def _get_cluster_pod_ipam_fq_name(self):
        return self._cluster_pod_ipam_fq_name

    def _get_cluster_service_ipam_fq_name(self):
        return self._cluster_service_ipam_fq_name

    def _get_cluster_ip_fabric_ipam_fq_name(self):
        return self._cluster_ip_fabric_ipam_fq_name

    def vnc_timer(self):
        try:
            self.network_policy_mgr.network_policy_timer()
            self.ingress_mgr.ingress_timer()
            self.service_mgr.service_timer()
            self.pod_mgr.pod_timer()
            self.namespace_mgr.namespace_timer()
        except Exception as e:
            string_buf = StringIO()
            cgitb_hook(file=string_buf, format="text")
            err_msg = string_buf.getvalue()
            self.logger.error("vnc_timer: %s - %s" %(self._name, err_msg))

    def vnc_process(self):
        while True:
            try:
                event = self.q.get()
                event_type = event['type']
                kind = event['object'].get('kind')
                metadata = event['object']['metadata']
                namespace = metadata.get('namespace')
                name = metadata.get('name')
                uid = metadata.get('uid')
                if kind == 'Pod':
                    self.pod_mgr.process(event)
                elif kind == 'Service':
                    self.service_mgr.process(event)
                elif kind == 'Namespace':
                    self.namespace_mgr.process(event)
                elif kind == 'NetworkPolicy':
                    self.network_policy_mgr.process(event)
                elif kind == 'Endpoints':
                    self.endpoints_mgr.process(event)
                elif kind == 'Ingress':
                    self.ingress_mgr.process(event)
                else:
                    print("%s - Event %s %s %s:%s:%s not handled"
                        %(self._name, event_type, kind, namespace, name, uid))
                    self.logger.error("%s - Event %s %s %s:%s:%s not handled"
                        %(self._name, event_type, kind, namespace, name, uid))
            except Empty:
                gevent.sleep(0)
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self.logger.error("%s - %s" %(self._name, err_msg))

    @classmethod
    def get_instance(cls):
        return VncKubernetes._vnc_kubernetes

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if inst is None:
            return
        inst.rabbit.close()
        for obj_cls in DBBaseKM.get_obj_type_map().values():
            obj_cls.reset()
        DBBase.clear()
        inst._db = None
        VncKubernetes._vnc_kubernetes = None

    @classmethod
    def create_tags(cls, type, value):
        if cls._vnc_kubernetes:
            cls.get_instance().tags_mgr.create(type, value)

    @classmethod
    def delete_tags(cls, type, value):
        if cls._vnc_kubernetes:
            cls.get_instance().tags_mgr.delete(type, value)

    @classmethod
    def get_tags(cls, kv_dict, create=False):
        if cls._vnc_kubernetes:
            return cls.get_instance().tags_mgr.get_tags_fq_name(kv_dict, create)
        return None
