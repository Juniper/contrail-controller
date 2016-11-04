#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC management for kubernetes
"""

import gevent
from gevent.queue import Empty

import requests
import argparse
import cgitb

from cfgm_common import importutils
from vnc_api.vnc_api import *
from config_db import *
import rabbit
import db
import label_cache

class VncKubernetes(object):

    def __init__(self, args=None, logger=None, q=None):
        self.args = args
        self.logger = logger
        self.q = q

        # init vnc connection
        self.vnc_lib = self._vnc_connect()

        # init access to db
        self._db = db.KubeNetworkManagerDB(self.args, self.logger)
        DBBaseKM.init(self, self.logger, self._db)

        # init rabbit connection
        self.rabbit = rabbit.RabbitConnection(self.logger, self.args)

        # sync api server db in local cache
        self._sync_sm()

        # provision cluster
        self._provision_cluster()

        # handle events
        self.label_cache = label_cache.LabelCache()
        self.namespace_mgr = importutils.import_object(
            'vnc.vnc_namespace.VncNamespace', self.vnc_lib)
        self.service_mgr = importutils.import_object(
            'vnc.vnc_service.VncService', self.vnc_lib,
            self.label_cache)
        self.pod_mgr = importutils.import_object(
            'vnc.vnc_pod.VncPod', self.vnc_lib,
            self.label_cache, self.service_mgr)
        self.network_policy_mgr = importutils.import_object(
            'vnc.vnc_network_policy.VncNetworkPolicy', self.vnc_lib)

    def _vnc_connect(self):
        # Retry till API server connection is up
        connected = False
        while not connected:
            try:
                vnc_lib = VncApi(self.args.admin_user,
                    self.args.admin_password, self.args.admin_tenant,
                    self.args.vnc_endpoint_ip, self.args.vnc_endpoint_port)
                connected = True
            except requests.exceptions.ConnectionError as e:
                time.sleep(3)
            except ResourceExhaustionError:
                time.sleep(3)
        return vnc_lib

    def _sync_sm(self):
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
        ProjectKM.locate(proj_obj.uuid)
        return proj_obj

    def _create_ipam(self, ipam_name, subnets, proj_obj):
        ipam_subnets = []
        for subnet in subnets:
            pfx, pfx_len = subnet.split('/')
            ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
            ipam_subnets.append(ipam_subnet)
        ipam_obj = NetworkIpam(name=ipam_name, parent_obj=proj_obj)
        try:
            self.vnc_lib.network_ipam_create(ipam_obj)
        except RefsExistError:
            vn_obj = self.vnc_lib.network_ipam_read(
                fq_name=ipam_obj.get_fq_name())
        return ipam_obj, ipam_subnets

    def _create_cluster_network(self, vn_name, proj_obj):
        vn_obj = VirtualNetwork(name=vn_name, parent_obj=proj_obj)
        ipam_obj, ipam_subnets= self._create_ipam('pod-ipam',
            self.args.pod_subnets, proj_obj)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(ipam_subnets))
        ipam_obj, ipam_subnets = self._create_ipam('service-ipam',
            self.args.service_subnets, proj_obj)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(ipam_subnets))

        #vn_obj.set_virtual_network_properties(VirtualNetworkType(forwarding_mode='l3'))
        try:
            self.vnc_lib.virtual_network_create(vn_obj)
        except RefsExistError:
            vn_obj = self.vnc_lib.virtual_network_read(
                fq_name=vn_obj.get_fq_name())
        VirtualNetworkKM.locate(vn_obj.uuid)

        return vn_obj.uuid

    def _provision_cluster(self):
        self._create_project('kube-system')
        proj_obj = self._create_project('default')
        self._create_cluster_network('cluster-network', proj_obj)

    def vnc_process(self):
        while True:
            try:
                event = self.q.get()
                print("\tGot %s %s %s:%s" % (event['type'],
                    event['object'].get('kind'),
                    event['object']['metadata'].get('namespace'),
                    event['object']['metadata'].get('name')))
                if event['object'].get('kind') == 'Pod':
                    self.pod_mgr.process(event)
                elif event['object'].get('kind') == 'Service':
                    self.service_mgr.process(event)
                elif event['object'].get('kind') == 'Namespace':
                    self.namespace_mgr.process(event)
                elif event['object'].get('kind') == 'NetworkPolicy':
                    self.network_policy_mgr.process(event)
            except Empty:
                gevent.sleep(0)
