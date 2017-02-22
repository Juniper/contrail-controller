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

from cfgm_common import importutils
from cfgm_common import vnc_cgitb
from cfgm_common.vnc_amqp import VncAmqpHandle
from vnc_api.vnc_api import *
from config_db import *
import db
import label_cache
from reaction_map import REACTION_MAP

class VncKubernetes(object):

    def __init__(self, args=None, logger=None, q=None, kube=None):
        self._name = type(self).__name__
        self.args = args
        self.logger = logger
        self.q = q
        self.kube = kube

        # init vnc connection
        self.vnc_lib = self._vnc_connect()

        # init access to db
        self._db = db.KubeNetworkManagerDB(self.args, self.logger)
        DBBaseKM.init(self, self.logger, self._db)

        # If nested mode is enabled via config, then record the directive.
        if self.args.nested_mode is '1':
            DBBaseKM.set_nested(True)

        # init rabbit connection
        self.rabbit = VncAmqpHandle(self.logger, DBBaseKM,
            REACTION_MAP, 'kube_manager', args=self.args)
        self.rabbit.establish()

        # sync api server db in local cache
        self._sync_km()
        self.rabbit._db_resync_done.set()

        # provision cluster
        self._provision_cluster()

        # handle events
        self.label_cache = label_cache.LabelCache()
        self.namespace_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_namespace.VncNamespace',vnc_lib=self.vnc_lib,
            logger=self.logger, cluster_pod_subnets = self.args.pod_subnets)
        self.service_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_service.VncService', self.vnc_lib,
            self.label_cache, self.args, self.logger, self.kube)
        self.network_policy_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_network_policy.VncNetworkPolicy',
            self.vnc_lib, self.label_cache, self.logger)
        self.pod_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_pod.VncPod', self.vnc_lib,
            self.label_cache, self.args, self.logger, self.service_mgr,
            self.network_policy_mgr, self.q,
            svc_fip_pool = self._get_cluster_service_fip_pool())
        self.endpoints_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_endpoints.VncEndpoints',
            self.vnc_lib, self.logger, self.kube)
        self.ingress_mgr = importutils.import_object(
            'kube_manager.vnc.vnc_ingress.VncIngress', self.args, self.q,
            self.vnc_lib, self.label_cache, self.logger, self.kube)

    def _vnc_connect(self):
        # Retry till API server connection is up
        connected = False
        while not connected:
            try:
                vnc_lib = VncApi(self.args.admin_user,
                    self.args.admin_password, self.args.admin_tenant,
                    self.args.vnc_endpoint_ip, self.args.vnc_endpoint_port,
                    auth_token_url=self.args.auth_token_url)
                connected = True
            except requests.exceptions.ConnectionError as e:
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
        proj_fq_name = ['default-domain', project_name]
        proj_obj = Project(name=project_name, fq_name=proj_fq_name)
        try:
            self.vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self.vnc_lib.project_read(
                fq_name=proj_fq_name)
        try:
            self._create_default_security_group(proj_obj)
        except RefsExistError:
            pass
        ProjectKM.locate(proj_obj.uuid)
        return proj_obj

    def _create_default_security_group(self, proj_obj):
        DEFAULT_SECGROUP_DESCRIPTION = "Default security group"
        def _get_rule(ingress, sg, prefix, ethertype):
            sgr_uuid = str(uuid.uuid4())
            if sg:
                addr = AddressType(
                    security_group=proj_obj.get_fq_name_str() + ':' + sg)
            elif prefix:
                addr = AddressType(subnet=SubnetType(prefix, 0))
            local_addr = AddressType(security_group='local')
            if ingress:
                src_addr = addr
                dst_addr = local_addr
            else:
                src_addr = local_addr
                dst_addr = addr
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                  protocol='any',
                                  src_addresses=[src_addr],
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=[dst_addr],
                                  dst_ports=[PortType(0, 65535)],
                                  ethertype=ethertype)
            return rule

        rules = [_get_rule(True, 'default', None, 'IPv4'),
                 _get_rule(True, 'default', None, 'IPv6'),
                 _get_rule(False, None, '0.0.0.0', 'IPv4'),
                 _get_rule(False, None, '::', 'IPv6')]
        sg_rules = PolicyEntriesType(rules)

        # create security group
        id_perms = IdPermsType(enable=True,
                               description=DEFAULT_SECGROUP_DESCRIPTION)
        sg_obj = SecurityGroup(name='default', parent_obj=proj_obj,
                               id_perms=id_perms,
                               security_group_entries=sg_rules)

        self.vnc_lib.security_group_create(sg_obj)
        self.vnc_lib.chown(sg_obj.get_uuid(), proj_obj.get_uuid())

    def _create_ipam(self, ipam_name, subnets, proj_obj,
            type='user-defined-subnet'):
        ipam_obj = NetworkIpam(name=ipam_name, parent_obj=proj_obj)

        ipam_subnets = []
        for subnet in subnets:
            pfx, pfx_len = subnet.split('/')
            ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
            ipam_subnets.append(ipam_subnet)

        if type == 'flat-subnet':
            ipam_obj.set_ipam_subnet_method('flat-subnet')
            ipam_obj.set_ipam_subnets(IpamSubnets(ipam_subnets))

        try:
            self.vnc_lib.network_ipam_create(ipam_obj)
        except RefsExistError:
            ipam_obj = self.vnc_lib.network_ipam_read(
                fq_name=ipam_obj.get_fq_name())
        return ipam_obj, ipam_subnets

    def _create_cluster_network(self, vn_name, proj_obj):
        vn_obj = VirtualNetwork(name=vn_name, parent_obj=proj_obj,
            address_allocation_mode='user-defined-subnet-only')

        # Create Pod IPAM.
        ipam_obj, ipam_subnets= self._create_ipam('pod-ipam',
            self.args.pod_subnets, proj_obj)

        # Attach Pod IPAM to virtual-network.
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(ipam_subnets))

        #
        # Create Service IPAM.
        #
        svc_ipam_obj, ipam_subnets = self._create_ipam('service-ipam',
            self.args.service_subnets, proj_obj, type='flat-subnet')

        # Attach Service IPAM to virtual-network.
        #
        # For flat-subnets, the subnets are specified on the IPAM and
        # not on the virtual-network to IPAM link. So pass an empty
        # list of VnSubnetsType.
        vn_obj.add_network_ipam(svc_ipam_obj, VnSubnetsType([]))

        vn_obj.set_virtual_network_properties(
             VirtualNetworkType(forwarding_mode='l3'))
        try:
            self.vnc_lib.virtual_network_create(vn_obj)
        except RefsExistError:
            pass

        # FIP pool creation requires a vnc object. Get it.
        vn_obj = self.vnc_lib.virtual_network_read(
            fq_name=vn_obj.get_fq_name())

        # Create service floating ip pool.
        self._create_cluster_service_fip_pool(vn_obj, svc_ipam_obj)

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
        # Service IP's in the k8s cluster are allocated from service
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
            fip_pool_vnc_obj =\
                self.vnc_lib.floating_ip_pool_create(fip_pool_obj)

        except RefsExistError:
            # Floating-ip-pool exists.
            #
            # Validate that existing floating-ip-pool has the service subnet
            # uuid as one of its subnets. If not raise an exception, as the
            # floating-ip-pool cannot be created, as one with the same name but
            # different attributes exists in the system.
            fip_pool_vnc_obj = self._get_cluster_service_fip_pool()
            svc_subnet_found = False
            fip_subnets = None

            if hasattr(fip_pool_vnc_obj, 'floating_ip_pool_subnets'):
                fip_subnets = fip_pool_vnc_obj.floating_ip_pool_subnets

            if fip_subnets:
                for subnet in fip_subnets['subnet_uuid']:
                    if subnet == svc_subnet_uuid:
                        svc_subnet_found = True
                        break

            if not svc_subnet_found:
                self.logger.error("Failed to create floating-ip-pool %s for"\
                    "subnet %s. A floating-ip-pool with same name exists." %\
                    (":".join(fip_pool_vnc_obj.fq_name), svc_subnet_uuid))

        else:
            # Update local cache.
            FloatingIpPoolKM.locate(fip_pool_vnc_obj)

        return

    def _provision_cluster(self):
        self._create_project('kube-system')
        proj_obj = self._create_project('default')
        self._create_cluster_network('cluster-network', proj_obj)

    def _get_cluster_network(self):
        return VirtualNetworkKM.find_by_name_or_uuid('cluster-network')

    def vnc_timer(self):
        self.ingress_mgr.ingress_timer()
        self.service_mgr.service_timer()
        self.pod_mgr.pod_timer()

    def vnc_process(self):
        while True:
            try:
                event = self.q.get()
                event_type = event['type']
                kind = event['object'].get('kind')
                metadata = event['object']['metadata']
                namespace = metadata.get('namespace')
                name = metadata.get('name')
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
                    print("$s - Event %s %s %s:%s not handled"
                        %(self._name, event_type, kind, namespace, name))
                    self.logger.error("%s - Event %s %s %s:%s not handled"
                        %(self._name, event_type, kind, namespace, name))
            except Empty:
                gevent.sleep(0)
