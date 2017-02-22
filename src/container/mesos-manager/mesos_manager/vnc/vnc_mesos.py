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
from cfgm_common.vnc_amqp import VncAmqpHandle
import mesos_manager.mesos_consts as mesos_consts
from vnc_api.vnc_api import *
from config_db import *
import db
from reaction_map import REACTION_MAP

class VncMesos(object):

    def __init__(self, args=None, logger=None, q=None):
        self.args = args
        self.logger = logger
        self.q = q

        # init vnc connection
        self.vnc_lib = self._vnc_connect()

        # init access to db
        self._db = db.MesosNetworkManagerDB(self.args, self.logger)
        DBBaseMM.init(self, self.logger, self._db)

        # init rabbit connection
        self.rabbit = VncAmqpHandle(self.logger, DBBaseMM,
            REACTION_MAP, 'mesos_manager', args=self.args)
        self.rabbit.establish()

        # sync api server db in local cache
        self._sync_sm()
        self.rabbit._db_resync_done.set()

    def _vnc_connect(self):
        # Retry till API server connection is up
        connected = False
        while not connected:
            try:
                vnc_lib = VncApi(self.args.admin_user,
                    self.args.admin_password, self.args.admin_tenant,
                    self.args.vnc_endpoint_ip, self.args.vnc_endpoint_port)
                connected = True
                self.logger.info("Connected to API-server %s:%s."
                    %(self.args.vnc_endpoint_ip, self.args.vnc_endpoint_port))
            except requests.exceptions.ConnectionError as e:
                time.sleep(3)
            except ResourceExhaustionError:
                time.sleep(3)
        return vnc_lib

    def _sync_sm(self):
        for cls in DBBaseMM.get_obj_type_map().values():
            for obj in cls.list_obj():
                cls.locate(obj['uuid'], obj)

    @staticmethod
    def reset():
        for cls in DBBaseMM.get_obj_type_map().values():
            cls.reset()

    def _create_project(self, project_name):
        proj_fq_name = ['default-domain', project_name]
        proj_obj = Project(name=project_name, fq_name=proj_fq_name)
        try:
            self.vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self.vnc_lib.project_read(
                fq_name=proj_fq_name)
        ProjectMM.locate(proj_obj.uuid)
        return proj_obj

    def _create_ipam(self, ipam_name, subnet, proj_obj):
        ipam_subnets = []
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

    def _create_network(self,labels, mesos_proj_obj):
        vn_obj = VirtualNetwork(name=labels['network-tag'],
            parent_obj=mesos_proj_obj,
            address_allocation_mode='user-defined-subnet-only')
        ipam_obj, ipam_subnets = self._create_ipam(labels['network-tag'],
            labels['public_subnet'],mesos_proj_obj)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(ipam_subnets))
        vn_obj.set_virtual_network_properties(
             VirtualNetworkType(forwarding_mode='l3'))
        try:
            self.vnc_lib.virtual_network_create(vn_obj)
        except RefsExistError:
            vn_obj = self.vnc_lib.virtual_network_read(
                fq_name=vn_obj.get_fq_name())
        return vn_obj

    def _create_vm(self, pod_id, pod_name):
        vm_obj = VirtualMachine(name=pod_name)
        vm_obj.uuid = pod_id
        try:
            self.vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self.vnc_lib.virtual_machine_read(id=pod_id)
        vm = VirtualMachineMM.locate(vm_obj.uuid)
        return vm_obj

    def _create_vmi(self, pod_name, pod_namespace, vm_obj, vn_obj, 
                    sg_tags, fip_pool_name):
        proj_fq_name = ['default-domain', pod_namespace]
        proj_obj = self.vnc_lib.project_read(fq_name=proj_fq_name)

        vmi_obj = VirtualMachineInterface(name=pod_name, parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine(vm_obj)

        if sg_tags != "": 
            sg_strings = sg_tags.split(",")
            for sg_string in sg_strings:
                sg_obj = self.vnc_lib.security_group_read(fq_name_str=sg_string)
                vmi_obj.add_security_group(sg_obj)
        try:
            self.vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            self.vnc_lib.virtual_machine_interface_update(vmi_obj)
        VirtualMachineInterfaceMM.locate(vmi_obj.uuid)
        
        #Creation of floating-ip
        if fip_pool_name != "": 
            fip_pool_fq_name = fip_pool_name.split(':')
            fip_pool_obj = self.vnc_lib.floating_ip_pool_read(fq_name=fip_pool_fq_name)
            fip_obj = FloatingIp(name="mesos-svc-fip-%s"% (pod_name),
                parent_obj=fip_pool_obj)
            fip_obj.set_project(proj_obj)
            fip_obj.set_virtual_machine_interface(vmi_obj)
            try:
                fip_uuid = self.vnc_lib.floating_ip_create(fip_obj)
            except RefsExistError:
                fip_uuid = self.vnc_lib.floating_ip_update(fip_obj)
        return vmi_obj

    def _create_iip(self, pod_name, vn_obj, vmi_obj):
        iip_obj = InstanceIp(name=pod_name)
        iip_obj.add_virtual_network(vn_obj)
        iip_obj.add_virtual_machine_interface(vmi_obj)
        try:
            self.vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self.vnc_lib.instance_ip_update(iip_obj)
        InstanceIpMM.locate(iip_obj.uuid)
        return iip_obj

    def _link_vm_to_node(self, vm_obj, pod_node):
        vrouter_fq_name = ['default-global-system-config', pod_node]
        try:
            vrouter_obj = self.vnc_lib.virtual_router_read(fq_name=vrouter_fq_name)
        except Exception as e:
            return

        self.vnc_lib.ref_update('virtual-router', vrouter_obj.uuid,
            'virtual-machine', vm_obj.uuid, None, 'ADD')
        vm = VirtualMachineMM.get(vm_obj.uuid)
        if vm: 
            vm.virtual_router = vrouter_obj.uuid

    def _setup_all(self, labels, pod_name, pod_id):
        pod_namespace = 'meso-system'
        #pod_name = pod_id
        cluster_name = labels['cluster_name']
        if 'sg_tags' in labels.keys():
            sg_tags = labels['sg_tags'] 
        else:
            sg_tags = ''
        if 'fip_pool' in labels.keys():
            fip_pool = labels['fip_pool'] 
        else:
            fip_pool = ''
        mesos_proj_obj = self._create_project(pod_namespace)
        vn_obj = self._create_network(labels, mesos_proj_obj)
        vm_obj = self._create_vm(pod_id, pod_name)
        vmi_obj = self._create_vmi(pod_name, pod_namespace, vm_obj, 
                                   vn_obj, sg_tags, fip_pool)
        self._create_iip(pod_name, vn_obj, vmi_obj)
        self._link_vm_to_node(vm_obj, cluster_name)

    def vnc_interface_and_instance_delete(self, vmi_id):
        vmi = VirtualMachineInterfaceMM.get(vmi_id)
        if not vmi:
            return
        for iip_id in list(vmi.instance_ips):
            try:
                self.vnc_lib.instance_ip_delete(id=iip_id)
            except NoIdError:
                pass
        for fip_id in list(vmi.floating_ips):
            try:
                self.vnc_lib.floating_ip_delete(id=fip_id)
            except NoIdError:
                pass

        try:
            self.vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        except NoIdError:
            pass

    def _delete_task(self, labels, task_uuid, task_uuid_str):
	vm = VirtualMachineMM.get(task_uuid)
	if not vm:
	    self.logger.error("task entry does not exist")
        if vm.virtual_router:
            self.vnc_lib.ref_update('virtual-router', vm.virtual_router,
                'virtual-machine', vm.uuid, None, 'DELETE')
        for vmi_id in list(vm.virtual_machine_interfaces):
            self.vnc_interface_and_instance_delete(vmi_id)
        try:
            self.vnc_lib.virtual_machine_delete(id=task_uuid)
        except NoIdError:
            pass
	#Delete network
        vn = VirtualNetworkMM.find_by_name_or_uuid(task_uuid_str)
        if not vn:
            return
        self.vnc_lib.virtual_network_delete(id=vn.uuid)
        VirtualNetworkMM.delete(vn.uuid)

    def process_q_event(self, event):
        labels = event['labels']
        for k,v in labels.items():
            if k == mesos_consts.MESOS_LABEL_PRIVATE_NETWORK:
                print v
                print "Cid for this is %s" %event['cid']
            elif k == mesos_consts.MESOS_LABEL_PUBLIC_NETWORK:
                print v
            elif k == mesos_consts.MESOS_LABEL_PUBLIC_SUBNET:
                print v
            else:
                pass
	if event['cmd'] == 'ADD':
	    self._setup_all(labels, str(event['cid']), event['cid'])
	elif event['cmd'] == 'DEL':
	    self._delete_task(labels, event['cid'], str(event['cid']))

    def vnc_process(self):
        while True:
            try:
                event = self.q.get()
                print event
                self.logger.info("VNC: Handle CNI Data for ContainerId: %s."
                    %(event['cid']))
                self.process_q_event(event)
            except Empty:
                gevent.sleep(0)

