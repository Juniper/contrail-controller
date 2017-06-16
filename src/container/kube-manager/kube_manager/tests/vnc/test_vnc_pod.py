#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import json
import mock
import time
import unittest
from mock import patch

from gevent.queue import Queue
from test_case import KMTestCase
from kube_manager.vnc.config_db import *
from kube_manager.vnc.loadbalancer import *
from kube_manager.vnc.config_db import *
from kube_manager.kube_manager import *
from kube_manager.vnc import db
from kube_manager import kube_manager
from kube_manager.vnc import vnc_namespace
from kube_manager.vnc import vnc_pod
from kube_manager.vnc.vnc_pod import LoadbalancerKM
from kube_manager.vnc.vnc_pod import NamespaceKM
from kube_manager.vnc.vnc_pod import PodKM
from kube_manager.vnc import vnc_kubernetes_config as kube_config

class VncPodTest(KMTestCase):
    def setUp(self):
        super(VncPodTest, self).setUp()
    #end setUp

    def tearDown(self):
        super(VncPodTest, self).tearDown()
    #end tearDown

    def _create_namespace(self, ns_name, ns_eval_vn_dict):
        ns_uuid = str(uuid.uuid4())
        ns_add_event = self.create_add_namespace_event(ns_name, ns_uuid)
        self.enqueue_event(ns_add_event)
        time.sleep(2)

        # Add Namespace to DB
        ns_labels = []
        ns_meta = {'name': ns_name, 'uid': ns_uuid, 'namespace': ns_name, 'labels': []}
        ns_meta['annotations'] = {'opencontrail.org/network': ns_eval_vn_dict}
        ns_obj = {}
        ns_obj['kind'] = 'Namespace'
        ns_obj['spec'] = {}
        ns_obj['metadata'] = ns_meta
        NamespaceKM.locate(ns_name, ns_obj)

        return ns_uuid

    def _create_virtual_network(self, proj_obj, vn_name):
        # Create network for Pod
        vn_obj = self.create_network(vn_name, proj_obj)
        vn_obj.set_virtual_network_properties(VirtualNetworkType(forwarding_mode='l3'))
        time.sleep(2)

        # Create pod-ipam
        pod_ipam_obj = NetworkIpam(name='pod-ipam', parent_obj=proj_obj)
        pfx, pfx_len = '10.32.0.0/12'.split('/')
        ipam_subnets = []
        ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
        ipam_subnets.append(ipam_subnet)
        pod_ipam_obj.set_ipam_subnet_method('flat-subnet')
        pod_ipam_obj.set_ipam_subnets(IpamSubnets(ipam_subnets))
        try:
            ipam_uuid = self._vnc_lib.network_ipam_create(pod_ipam_obj)
        except RefsExistError:
            ipam_obj = self._vnc_lib.network_ipam_read(
                fq_name=pod_ipam_obj.get_fq_name())
            ipam_uuid = pod_ipam_obj.get_uuid()
        except Exception as e:
            print e
        NetworkIpamKM.locate(ipam_uuid)
        vn_obj.add_network_ipam(pod_ipam_obj, VnSubnetsType([]))

        # Create service-ipam
        ipam_obj = NetworkIpam(name='service-ipam', parent_obj=proj_obj)
        pfx, pfx_len = '10.92.0.0/12'.split('/')
        ipam_subnets = []
        ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
        ipam_subnets.append(ipam_subnet)
        ipam_obj.set_ipam_subnet_method('user-defined-subnet')
        try:
            ipam_uuid = self._vnc_lib.network_ipam_create(ipam_obj)
        except RefsExistError:
            ipam_obj = self._vnc_lib.network_ipam_read(
                fq_name=ipam_obj.get_fq_name())
            ipam_uuid = ipam_obj.get_uuid()
        NetworkIpamKM.locate(ipam_uuid)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(ipam_subnets))

        vn_obj = self._vnc_lib.virtual_network_update(vn_obj)
        time.sleep(2)
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=proj_obj.fq_name)

        # Create pod fip
        ipam_refs = vn_obj.get_network_ipam_refs()
        svc_subnet_uuid = None
        for ipam_ref in ipam_refs:
            if ipam_ref['to'] == pod_ipam_obj.get_fq_name():
                ipam_subnets = ipam_ref['attr'].get_ipam_subnets()
                if not ipam_subnets:
                    continue
                # We will use the first subnet in the matching IPAM.
                svc_subnet_uuid = ipam_subnets[0].get_subnet_uuid()
                break

        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid=[svc_subnet_uuid])
        fip_pool_obj = FloatingIpPool(
            'svc-fip-pool-%s' % (vn_obj.name),
            floating_ip_pool_subnets=fip_subnets,
            parent_obj=vn_obj)
        try:
            # Create floating ip pool for cluster service network.
            fip_pool_vnc_obj = self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        except Exception as e:
            raise
        else:
            # Update local cache.
            FloatingIpPoolKM.locate(fip_pool_vnc_obj)
        return vn_obj

    def test_vnc_pod_add_delete_cluster_project_defined(self):
        cluster_project = 'cluster_project'
        kube_config.VncKubernetesConfig.args().cluster_project = "{'project':'" + cluster_project + "'}"
        eval_vn_dict = '{"domain":"default-domain","project":"cluster_project","name":"cluster-network"}'

        vn_name = 'cluster-network'
        ns_name = 'guestbook'
        ns_uuid = self._create_namespace(ns_name, eval_vn_dict)

        # create and add Pod to DB
        pod_name = "test-pod"
        pod_uuid = str(uuid.uuid4())
        pod_namespace = 'guestbook'
        pod_spec = {'nodeName': 'test-node'}  # , 'hostNetwork': 'test-host-network'}
        pod_labels = []
        pod_meta = {'name': pod_name, 'uuid': pod_uuid, 'namespace': pod_namespace, 'labels': pod_labels}
        pod_meta['annotations'] = {'opencontrail.org/network': eval_vn_dict}
        pod_add_event = self.create_event('Pod', pod_spec, pod_meta, 'ADDED')
        pod_add_event['object']['status'] = {'hostIP': '192.168.0.1', 'phase': 'created'}
        pod_obj = PodKM.locate(pod_uuid, pod_add_event['object'])

        # Create network for Pod
        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj = self._create_virtual_network(proj_obj, vn_name)

        # Checking Pod addition
        self.enqueue_event(pod_add_event)
        time.sleep(3)
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertIsNotNone(vn_obj)
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertIsNotNone(vn_obj)

        tmp_fq_name = ['default-domain', cluster_project, pod_name]
        vm = self._vnc_lib.virtual_machine_read(id=pod_uuid)
        self.assertIsNotNone(vm)
        vm = VirtualMachineKM.locate(vm.uuid)
        self.assertIsNotNone(vm)
        self.assertTrue(len(vm.virtual_machine_interfaces) > 0)

        for vmi_id in list(vm.virtual_machine_interfaces):
            vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
            self.assertIsNotNone(vmi)
            self.assertEqual(vmi.parent_name, cluster_project)
            self.assertEqual(vmi.parent_uuid, proj_obj.uuid)
            vmi = VirtualMachineInterfaceKM.locate(vmi_id)
            self.assertTrue(len(vmi.security_groups) > 1)
            for sg_uuid in list(vmi.security_groups):
                sg = self._vnc_lib.security_group_read(id=sg_uuid)
                self.assertIsNotNone(sg)
            self.assertTrue(len(vmi.instance_ips) > 0)
            for iip_uuid in list(vmi.instance_ips):
                iip = self._vnc_lib.instance_ip_read(id=iip_uuid)
                self.assertIsNotNone(iip)

        pod_delete_event = self.create_event('Pod', pod_spec, pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        time.sleep(3)

        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertIsNotNone(vn_obj)
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertIsNotNone(vn_obj)

        tmp_fq_name = ['default-domain', cluster_project, pod_name]
        try:
            vm = self._vnc_lib.virtual_machine_read(fq_name=tmp_fq_name)
        except NoIdError:
            pass
        else:
            raise
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def test_vnc_pod_add_delete_cluster_project_undefined(self):
        eval_vn_dict = '{"domain":"default-domain","project":"guestbook","name":"cluster-network"}'

        vn_name = 'cluster-network'
        ns_name = 'guestbook'
        ns_uuid = self._create_namespace(ns_name, eval_vn_dict)

        # create and add Pod to DB
        pod_name = "test-pod"
        pod_uuid = str(uuid.uuid4())
        pod_namespace = 'guestbook'
        pod_spec = {'nodeName': 'test-node'}  # , 'hostNetwork': 'test-host-network'}
        pod_labels = []
        pod_meta = {'name': pod_name, 'uuid': pod_uuid, 'namespace': pod_namespace, 'labels': pod_labels}
        pod_meta['annotations'] = {'opencontrail.org/network': eval_vn_dict}
        pod_add_event = self.create_event('Pod', pod_spec, pod_meta, 'ADDED')
        pod_add_event['object']['status'] = {'hostIP': '192.168.0.1', 'phase': 'created'}
        pod_obj = PodKM.locate(pod_uuid, pod_add_event['object'])

        # Create network for Pod
        proj_fq_name = ['default-domain', ns_name]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj = self._create_virtual_network(proj_obj, vn_name)

        # Checking Pod addition
        self.enqueue_event(pod_add_event)
        time.sleep(3)

        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertIsNotNone(vn_obj)
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertIsNotNone(vn_obj)

        tmp_fq_name = ['default-domain', ns_name, pod_name]
        vm = self._vnc_lib.virtual_machine_read(id=pod_uuid)
        self.assertIsNotNone(vm)
        vm = VirtualMachineKM.locate(vm.uuid)
        self.assertIsNotNone(vm)
        self.assertTrue(len(vm.virtual_machine_interfaces) > 0)

        for vmi_id in list(vm.virtual_machine_interfaces):
            vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
            self.assertIsNotNone(vmi)
            self.assertEqual(vmi.parent_name, ns_name)
            self.assertEqual(vmi.parent_uuid, proj_obj.uuid)
            vmi = VirtualMachineInterfaceKM.locate(vmi_id)
            self.assertTrue(len(vmi.security_groups) > 1)
            for sg_uuid in list(vmi.security_groups):
                sg = self._vnc_lib.security_group_read(id=sg_uuid)
                self.assertIsNotNone(sg)
            self.assertTrue(len(vmi.instance_ips) > 0)
            for iip_uuid in list(vmi.instance_ips):
                iip = self._vnc_lib.instance_ip_read(id=iip_uuid)
                self.assertIsNotNone(iip)

        pod_delete_event = self.create_event('Pod', pod_spec, pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        time.sleep(3)

        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertIsNotNone(vn_obj)
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertIsNotNone(vn_obj)

        tmp_fq_name = ['default-domain', ns_name, pod_name]
        try:
            vm = self._vnc_lib.virtual_machine_read(fq_name=tmp_fq_name)
        except NoIdError:
            pass
        else:
            raise
        self.assertTrue(len(vn_obj.instance_ips) == 0)

