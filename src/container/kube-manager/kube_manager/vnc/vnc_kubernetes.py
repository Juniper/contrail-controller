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
from cfgm_common.utils import cgitb_hook
from cfgm_common.vnc_amqp import VncAmqpHandle
from vnc_api.vnc_api import *
from config_db import *
import db
import label_cache
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

class VncKubernetes(VncCommon):

    _vnc_kubernetes = None

    def __init__(self, args=None, logger=None, q=None, kube=None):
        self._name = type(self).__name__
        self.args = args
        self.logger = logger
        self.q = q
        self.kube = kube
        self._cluster_pod_ipam_fq_name = None

        # init vnc connection
        self.vnc_lib = self._vnc_connect()

        # Cache common config.
        self.vnc_kube_config = vnc_kube_config(logger=self.logger,
            vnc_lib=self.vnc_lib, args=self.args, queue=self.q, kube=self.kube)

        # HACK ALERT.
        # Till we have an alternate means to get config objects,  we will
        # direcly connect to cassandra. Such a persistant connection is
        # discouraged, but is the only option we have for now.
        #
        # Disable flow timeout on this connection, so the flow persists.
        #
        if self.args.nested_mode is '1':
            for cassandra_server in self.args.cassandra_server_list:
                cassandra_port = cassandra_server.split(':')[-1]
                flow_aging_manager.create_flow_aging_timeout_entry(self.vnc_lib,
                    "tcp", cassandra_port, 2147483647)

        # init access to db
        self._db = db.KubeNetworkManagerDB(self.args, self.logger)
        DBBaseKM.init(self, self.logger, self._db)

        # If nested mode is enabled via config, then record the directive.
        if self.args.nested_mode is '1':
            DBBaseKM.set_nested(True)

        # sync api server db in local cache
        self._sync_km()

        # init rabbit connection
        self.rabbit = VncAmqpHandle(self.logger, DBBaseKM,
            REACTION_MAP, 'kube_manager', args=self.args)
        self.rabbit.establish()
        self.rabbit._db_resync_done.set()

        # provision cluster
        self._provision_cluster()

        # handle events
        self.label_cache = label_cache.LabelCache()

        # Update common config.
        self.vnc_kube_config.update(label_cache=self.label_cache,
            cluster_pod_ipam_fq_name=self._get_cluster_pod_ipam_fq_name(),
            cluster_service_fip_pool=self._get_cluster_service_fip_pool())

        self.network_policy_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_network_policy.VncNetworkPolicy')
        self.namespace_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_namespace.VncNamespace',
            self.network_policy_mgr)
        self.ingress_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_ingress.VncIngress')
        self.service_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_service.VncService', self.ingress_mgr)
        self.pod_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_pod.VncPod', self.service_mgr,
            self.network_policy_mgr)
        self.endpoints_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_endpoints.VncEndpoints')

        VncKubernetes._vnc_kubernetes = self

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
            type='user-defined-subnet'):
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

    def _create_cluster_network(self, vn_name, proj_obj):
        # Check if the VN already exists.
        # If yes, update existing VN object with k8s config.
        vn_exists = False
        vn = VirtualNetwork(name=vn_name, parent_obj=proj_obj,
                 address_allocation_mode='user-defined-subnet-only')
        try:
            vn_obj = self.vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())
            vn_exists = True
        except NoIdError:
            # VN does not exist. Create one.
            vn_obj = vn

        # Create Pod IPAM.
        pod_ipam_update, pod_ipam_obj, pod_ipam_subnets = \
            self._create_ipam('pod-ipam',
                self.args.pod_subnets, proj_obj, type='flat-subnet')

        # Cache cluster pod ipam name.
        # This will be referenced by ALL pods that are spawned in the cluster.
        self._cluster_pod_ipam_fq_name = pod_ipam_obj.get_fq_name()

        # Attach Pod IPAM to cluster virtual network.
        #
        # For flat-subnets, the subnets are specified on the IPAM and
        # not on the virtual-network to IPAM link. So pass an empty
        # list of VnSubnetsType.
        if pod_ipam_update or \
           not self._is_ipam_exists(vn_obj, pod_ipam_obj.get_fq_name()):
            vn_obj.add_network_ipam(pod_ipam_obj, VnSubnetsType([]))

        #
        # Create Service IPAM.
        #
        svc_ipam_update, svc_ipam_obj, svc_ipam_subnets = \
            self._create_ipam('service-ipam', self.args.service_subnets, proj_obj)

        # Attach Service IPAM to virtual-network.
        svc_subnet = None
        if len(svc_ipam_subnets):
            svc_subnet = svc_ipam_subnets[0].subnet
        if svc_ipam_update or \
           not self._is_ipam_exists(vn_obj, svc_ipam_obj.get_fq_name(), svc_subnet):
            vn_obj.add_network_ipam(svc_ipam_obj, VnSubnetsType(svc_ipam_subnets))

        vn_obj.set_virtual_network_properties(
             VirtualNetworkType(forwarding_mode='l3'))

        if vn_exists:
            # Update VN.
            self.vnc_lib.virtual_network_update(vn_obj)
        else:
            # Create VN.
            self.vnc_lib.virtual_network_create(vn_obj)

        # FIP pool creation requires a vnc object. Get it.
        vn_obj = self.vnc_lib.virtual_network_read(
            fq_name=vn_obj.get_fq_name())

        # Create service floating ip pool.
        self._create_cluster_service_fip_pool(vn_obj, pod_ipam_obj)

        VirtualNetworkKM.locate(vn_obj.uuid)
        return vn_obj.uuid

    def _get_cluster_service_fip_pool_name(self, vn_name):
        """
        Return fip pool name of cluster service network.
        """
        return 'svc-fip-pool-%s' %(vn_name)

    def _get_cluster_service_fip_pool(self):
        """
        Get floating ip pool of cluster service network.
        """
        vn_obj = self._get_cluster_network()
        return FloatingIpPoolKM.find_by_name_or_uuid(
            self._get_cluster_service_fip_pool_name(vn_obj.name))

    def _create_cluster_service_fip_pool(self, vn_obj, ipam_obj):
        # Create a floating-ip-pool in cluster service network.
        #
        # Service IP's in the k8s cluster are allocated from pod
        # IPAM in the cluster network. All pods spawned in isolated
        # virtual networks will be allocated an IP from this floating-ip-
        # pool. These pods, in those isolated virtual networks, will use this
        # floating-ip for outbound traffic to services in the k8s cluster.

        # Get IPAM refs from virtual-network.
        ipam_refs = vn_obj.get_network_ipam_refs()
        svc_subnet_uuid = None
        for ipam_ref in ipam_refs:
            if ipam_ref['to'] == ipam_obj.get_fq_name():
                ipam_subnets = ipam_ref['attr'].get_ipam_subnets()
                if not ipam_subnets:
                    continue
                # We will use the first subnet in the matching IPAM.
                svc_subnet_uuid = ipam_subnets[0].get_subnet_uuid()
                break

        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid = [svc_subnet_uuid])
        fip_pool_obj = FloatingIpPool(
            self._get_cluster_service_fip_pool_name(vn_obj.name),
            floating_ip_pool_subnets = fip_subnets,
            parent_obj=vn_obj)

        try:
            # Create floating ip pool for cluster service network.
            self.vnc_lib.floating_ip_pool_create(fip_pool_obj)
        except RefsExistError:
            # Floating-ip-pool exists.
            #
            # Validate that existing floating-ip-pool has the service subnet
            # uuid as one of its subnets. If not raise an exception, as the
            # floating-ip-pool cannot be created, as one with the same name but
            # different attributes exists in the system.
            fip_pool_db_obj = self._get_cluster_service_fip_pool()
            svc_subnet_found = False
            fip_subnets = None

            if hasattr(fip_pool_db_obj, 'floating_ip_pool_subnets'):
                fip_subnets = fip_pool_db_obj.floating_ip_pool_subnets

            if fip_subnets:
                for subnet in fip_subnets['subnet_uuid']:
                    if subnet == svc_subnet_uuid:
                        svc_subnet_found = True
                        break

            if not svc_subnet_found:
                # Requested service subnet was not found in existing fip pool.
                # Update existing fip pool entry with desired subnet.
                self.logger.debug("Failed to create floating-ip-pool %s for"\
                    "subnet %s. A floating-ip-pool with same name exists."\
                    " Update existing entry." %\
                    (":".join(fip_pool_db_obj.fq_name), svc_subnet_uuid))

                # Update vnc.
                self.vnc_lib.floating_ip_pool_update(fip_pool_obj)
                # Update subnet info in local cache.
                fip_subnets['subnet_uuid'] = [svc_subnet_uuid]

        else:
            # Read and update local cache.
            fip_pool_obj = self.vnc_lib.floating_ip_pool_read(
                fq_name=fip_pool_obj.fq_name)
            FloatingIpPoolKM.locate(fip_pool_obj.get_uuid())

        return

    def _provision_cluster(self):
        self._create_project('kube-system')
        proj_obj = self._create_project(\
            vnc_kube_config.cluster_default_project_name())
        self._create_cluster_network(\
            vnc_kube_config.cluster_default_network_name(), proj_obj)

    def _get_cluster_network(self):
        return VirtualNetworkKM.find_by_name_or_uuid(
            vnc_kube_config.cluster_default_network_name())

    def _get_cluster_pod_ipam_fq_name(self):
        return self._cluster_pod_ipam_fq_name

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
                    print("$s - Event %s %s %s:%s:%s not handled"
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



