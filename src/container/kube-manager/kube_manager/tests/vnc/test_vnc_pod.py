#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import uuid
import ipaddress

from cfgm_common.exceptions import RefsExistError
from kube_manager.tests.vnc.test_case import KMTestCase
from kube_manager.vnc.config_db import (
    VirtualNetworkKM, VirtualMachineKM, VirtualMachineInterfaceKM)
from kube_manager.kube_manager import NoIdError
from kube_manager.vnc.vnc_pod import NamespaceKM
from kube_manager.vnc.vnc_pod import PodKM
from kube_manager.vnc import vnc_kubernetes_config as kube_config

class VncPodTest(KMTestCase):
    def setUp(self, extra_config_knobs=None):
        super(VncPodTest, self).setUp(extra_config_knobs=extra_config_knobs)
    #end setUp

    def tearDown(self):
        super(VncPodTest, self).tearDown()
    #end tearDown

    def _create_namespace(self, ns_name, ns_eval_vn_dict, is_isolated=False):
        ns_uuid = str(uuid.uuid4())
        ns_add_event = self.create_add_namespace_event(ns_name, ns_uuid)
        ns_object = ns_add_event['object']
        ns_object['spec'] = {}
        ns_meta = ns_object['metadata']
        ns_meta['annotations'] = {}

        ns_meta['name'] = ns_name
        ns_meta['uid'] = ns_uuid
        ns_meta['namespace'] = ns_name
        ns_meta['labels'] = {}

        if ns_eval_vn_dict:
            ns_meta['annotations']['opencontrail.org/network'] = \
                ns_eval_vn_dict
        if is_isolated:
            ns_meta['annotations']['opencontrail.org/isolation'] = 'true'

        NamespaceKM.locate(ns_name, ns_object)

        self.enqueue_event(ns_add_event)
        self.wait_for_all_tasks_done()

        return ns_uuid

    def _create_virtual_network(self, proj_obj, vn_name):
        # Create network for Pod
        vn_obj = self.create_network(vn_name, proj_obj, '10.32.0.0/12',
                                     '10.96.0.0/12')
        return vn_obj.uuid

    def _create_pod(self, pod_name, pod_namespace, pod_status, eval_vn_dict):
        pod_uuid = str(uuid.uuid4())
        pod_spec = {'nodeName': 'test-node'}
        pod_labels = {}
        pod_meta = {'name': pod_name, 'uid': pod_uuid,
                    'namespace': pod_namespace, 'labels': pod_labels}
        if eval_vn_dict:
            pod_meta['annotations'] = {
                'opencontrail.org/network': eval_vn_dict}
        pod_add_event = self.create_event('Pod', pod_spec, pod_meta, 'ADDED')
        pod_add_event['object']['status'] = pod_status
        pod = PodKM.locate(pod_uuid, pod_add_event['object'])
        self.enqueue_event(pod_add_event)
        return pod.uuid, pod_meta, pod_spec

    def _assert_virtual_network(self, vn_obj_uuid):
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj_uuid)
        self.assertIsNotNone(vn_obj)
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertIsNotNone(vn_obj)

    def _assert_pod_ip_is_from_vn_ipam(self, iip_obj, vn_obj_uuid):
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj_uuid)
        pod_ipam = [ipam for ipam in vn_obj.get_network_ipam_refs()
                    if ipam['to'][-1] == 'pod-ipam'][0]

        self.assertTrue(len(pod_ipam['attr'].ipam_subnets) == 1)
        pod_ipam_subnet = pod_ipam['attr'].ipam_subnets[0]
        self.assertIsNotNone(pod_ipam_subnet)
        subnet = pod_ipam_subnet.subnet
        if subnet is None:
            try:
                pod_ipam = self._vnc_lib.network_ipam_read(id=pod_ipam['uuid'])
                subnet = pod_ipam.ipam_subnets.subnets[0].subnet
            except:
                pass

        self.assertIsNotNone(subnet)
        iip_ip = ipaddress.ip_address(unicode(iip_obj.instance_ip_address))
        vn_network = ipaddress.ip_network(subnet.ip_prefix + u'/'
                                          + unicode(subnet.ip_prefix_len))
        self.assertTrue(iip_ip in vn_network)

    def _assert_virtual_machine(self, pod_uuid, cluster_project,
                                proj_obj, vn_obj_uuid):
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
            self.assertTrue(len(vmi.instance_ips) == 1)
            iip_uuid = list(vmi.instance_ips)[0]
            iip = self._vnc_lib.instance_ip_read(id=iip_uuid)
            self.assertIsNotNone(iip)
            self._assert_pod_ip_is_from_vn_ipam(iip, vn_obj_uuid)

    def test_pod_add_delete_when_cluster_project_is_defined(self):
        cluster_project = 'test-project'
        vn_name = 'test-network'
        ns_name = 'test-namespace'
        pod_name = 'test-pod'
        pod_status = {'hostIP': '192.168.0.1', 'phase': 'created'}

        args = {}
        args['domain'] = 'default-domain'
        args['project'] = cluster_project
        args['name'] = vn_name

        kube_config.VncKubernetesConfig.args().cluster_project = \
            "{'project':'" + cluster_project + "'}"
        kube_config.VncKubernetesConfig.args().cluster_network = repr(args)
        kube_config.VncKubernetesConfig.vnc_kubernetes_config[
            'cluster_pod_ipam_fq_name'] = \
            ['default-domain', cluster_project, 'pod-ipam']

        self._create_namespace(ns_name, None)

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj_uuid = self._create_virtual_network(proj_obj, vn_name)

        pod_uuid, pod_meta, pod_spec = self._create_pod(pod_name,
                                                        ns_name,
                                                        pod_status,
                                                        None)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj_uuid)
        self._assert_virtual_machine(pod_uuid, cluster_project,
                                     proj_obj, vn_obj_uuid)

        pod_delete_event = self.create_event('Pod', pod_spec,
                                             pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj_uuid)

        tmp_fq_name = ['default-domain', cluster_project, pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj_uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def test_pod_add_delete_when_cluster_project_is_not_defined(self):
        vn_name = 'test-network'
        ns_name = 'test-namespace'
        cluster_project = ns_name
        pod_name = 'test-pod'
        pod_status = {'hostIP': '192.168.0.1', 'phase': 'created'}

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None
        kube_config.VncKubernetesConfig.vnc_kubernetes_config[
            'cluster_pod_ipam_fq_name'] = \
            ['default-domain', cluster_project, 'pod-ipam']

        self._create_namespace(ns_name, None)

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        try:
            vn_obj_uuid = self._create_virtual_network(proj_obj, vn_name)
        except RefsExistError:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=proj_fq_name)
            vn_obj_uuid = vn_obj.uuid

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        pod_uuid, pod_meta, pod_spec = self._create_pod(pod_name,
                                                        ns_name,
                                                        pod_status,
                                                        None)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj_uuid)
        self._assert_virtual_machine(pod_uuid, cluster_project, proj_obj,
                                     vn_obj_uuid)

        pod_delete_event = self.create_event('Pod', pod_spec,
                                             pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj_uuid)

        tmp_fq_name = ['default-domain', ns_name, pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj_uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def test_pod_add_delete_with_namespace_isolation_true_when_cluster_project_is_not_defined(self):
        ns_name = 'test-namespace-isolated'
        cluster_project = ns_name
        pod_name = 'test-pod'
        pod_status = {'hostIP': '192.168.0.1', 'phase': 'created'}

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None

        self._create_namespace(ns_name, None, True)
        vn_obj = self._vnc_lib.virtual_network_read(
            fq_name=['default-domain', cluster_project, ns_name+'-vn'])

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        pod_uuid, pod_meta, pod_spec = self._create_pod(pod_name,
                                                        ns_name,
                                                        pod_status,
                                                        None)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(pod_uuid, cluster_project, proj_obj,
                                     vn_obj.uuid)

        pod_delete_event = self.create_event('Pod', pod_spec,
                                             pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)

        tmp_fq_name = ['default-domain', ns_name, pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def test_pod_add_delete_with_namespace_isolation_true_when_cluster_project_is_defined(self):
        ns_name = 'test-namespace-isolated'
        cluster_project = ns_name
        pod_name = 'test-pod'
        pod_status = {'hostIP': '192.168.0.1', 'phase': 'created'}

        kube_config.VncKubernetesConfig.args().cluster_project = \
            "{'project':'" + cluster_project + "'}"
        kube_config.VncKubernetesConfig.args().cluster_network = None

        self._create_namespace(ns_name, None, True)
        vn_obj = self._vnc_lib.virtual_network_read(
            fq_name=['default-domain', cluster_project, ns_name + '-vn'])

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        pod_uuid, pod_meta, pod_spec = self._create_pod(pod_name,
                                                        ns_name,
                                                        pod_status,
                                                        None)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(pod_uuid, cluster_project, proj_obj,
                                     vn_obj.uuid)

        pod_delete_event = self.create_event('Pod', pod_spec,
                                             pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)

        tmp_fq_name = ['default-domain', ns_name, pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def pod_add_delete_with_namespace_custom_network_annotation(self):
        vn_name = 'test-network'
        ns_name = 'test-namespace'
        cluster_project = ns_name
        pod_name = 'test-pod'
        pod_status = {'hostIP': '192.168.0.1', 'phase': 'created'}
        eval_vn_dict = '{"domain":"default-domain",\
                         "project":"%s",\
                         "name":"%s"}' % (cluster_project, vn_name)

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None
        kube_config.VncKubernetesConfig.vnc_kubernetes_config[
            'cluster_pod_ipam_fq_name'] = \
            ['default-domain', cluster_project, 'pod-ipam']

        self.create_project(cluster_project)
        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        try:
            vn_obj_uuid = self._create_virtual_network(proj_obj, vn_name)
            vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj_uuid)
        except RefsExistError:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=proj_fq_name)
            vn_obj_uuid = vn_obj.uuid

        self._create_namespace(ns_name, eval_vn_dict)

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)


        pod_uuid, pod_meta, pod_spec = self._create_pod(pod_name,
                                                        ns_name,
                                                        pod_status,
                                                        eval_vn_dict)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(pod_uuid, cluster_project, proj_obj,
                                     vn_obj_uuid)

        pod_delete_event = self.create_event('Pod', pod_spec,
                                             pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)

        tmp_fq_name = ['default-domain', ns_name, pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj_uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def test_delete_add_pod_after_kube_manager_is_killed(self):
        vn_name = 'cluster-network'
        ns_name = 'guestbook'
        cluster_project = ns_name
        pod_name = 'test-pod'
        pod_status = {'hostIP': '192.168.0.1', 'phase': 'created'}

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None

        self._create_namespace(ns_name, None)

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        try:
            vn_obj_uuid = self._create_virtual_network(proj_obj, vn_name)
            vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj_uuid)
        except RefsExistError:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=proj_fq_name)
            vn_obj_uuid = vn_obj.uuid

        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        pod_uuid, pod_meta, pod_spec = self._create_pod(pod_name,
                                                        ns_name,
                                                        pod_status,
                                                        None)

        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj_uuid)
        self._assert_virtual_machine(pod_uuid, cluster_project, proj_obj,
                                     vn_obj_uuid)

        self.kill_kube_manager()

        pod_delete_event = self.create_event('Pod', pod_spec,
                                             pod_meta, 'DELETED')
        self.enqueue_event(pod_delete_event)
        pod_uuid, pod_meta, pod_spec = self._create_pod(pod_name,
                                                        ns_name,
                                                        pod_status,
                                                        None)

        self.spawn_kube_manager()
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(pod_uuid, cluster_project, proj_obj,
                                     vn_obj_uuid)
