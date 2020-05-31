#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import uuid
from collections import namedtuple
import ipaddress
import unittest
import mock

from cfgm_common.exceptions import NoIdError

from kube_manager.tests.vnc.test_case import KMTestCase
from kube_manager.common.kube_config_db import (NamespaceKM, PodKM)
from kube_manager.vnc.config_db import (
    VirtualNetworkKM, VirtualMachineKM, VirtualMachineInterfaceKM)
from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.vnc.config_db import TagKM
from kube_manager.vnc.label_cache import XLabelCache
from kube_manager.tests.vnc.db_mock import DBBaseKM
from kube_manager.vnc.vnc_kubernetes import VncKubernetes

TestPod = namedtuple('TestPod', ['uuid', 'meta', 'spec'])


class VncPodTest(KMTestCase):
    def setUp(self, extra_config_knobs=None):
        super(VncPodTest, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncPodTest, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncPodTest, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        DBBaseKM.set_nested(False)
        cls.domain = 'default-domain'
        cls.cluster_project = 'test-project'
        cls.vn_name = cls.cluster_name() + '-test-pod-network'
        cls.service_vn_name = cls.cluster_name() + '-test-service-network'
        cls.ns_name = 'test-namespace'

        cls.pod_name = 'test-pod'
        cls.pod_status = {
            'hostIP': cls.get_kubernetes_node_ip(),
            'phase': 'created'
        }

        cn_dict = {
            'domain': cls.domain,
            'project': cls.cluster_project,
            'name': cls.vn_name
        }
        service_cn_dict = {
            'domain': cls.domain,
            'project': cls.cluster_project,
            'name': cls.service_vn_name
        }
        cp_dict = {'project': cls.cluster_project}

        kube_config.VncKubernetesConfig.args(). \
            cluster_project = repr(cp_dict)
        kube_config.VncKubernetesConfig.args(). \
            cluster_pod_network = repr(cn_dict)
        kube_config.VncKubernetesConfig.args(). \
            cluster_service_network = repr(service_cn_dict)
        kube_config.VncKubernetesConfig.vnc_kubernetes_config[
            'cluster_pod_ipam_fq_name'] = \
            ['default-domain', cls.cluster_project, 'pod-ipam']

        # Create Vrouter Object.
        cls.vrouter_name = 'test-VncPodTest-vrouter'
        cls.vrouter_obj = cls.create_virtual_router(cls.vrouter_name)

    @classmethod
    def tearDownClass(cls):
        for pod in list(PodKM):
            PodKM.delete(pod)
        for namespace in list(NamespaceKM):
            NamespaceKM.delete(namespace)

        # Cleanup the Vrouter object.
        cls.delete_virtual_router(cls.vrouter_obj.uuid)

        super(VncPodTest, cls).tearDownClass()

    def _construct_pod_spec(self, nodeName):
        return {'nodeName': nodeName}

    def _construct_pod_meta(self, name, uuid, namespace, labels={}):
        meta = {}
        meta['name'] = name
        meta['uid'] = uuid
        meta['namespace'] = namespace
        if labels:
            meta['labels'] = labels
        return meta

    def _create_namespace(self, ns_name, ns_eval_vn_dict, is_isolated=False, labels={}):
        ns_uuid = str(uuid.uuid4())
        ns_add_event = self.create_add_namespace_event(ns_name, ns_uuid)
        ns_object = ns_add_event['object']
        ns_object['spec'] = {}
        ns_meta = ns_object['metadata']
        ns_meta['annotations'] = {}

        ns_meta['name'] = ns_name
        ns_meta['uid'] = ns_uuid
        ns_meta['namespace'] = ns_name
        ns_meta['labels'] = labels

        if ns_eval_vn_dict:
            ns_meta['annotations']['opencontrail.org/network'] = \
                ns_eval_vn_dict
        if is_isolated:
            ns_meta['annotations']['opencontrail.org/isolation'] = 'true'

        NamespaceKM.delete(ns_name)
        NamespaceKM.delete(ns_uuid)
        NamespaceKM.locate(ns_name, ns_object)
        NamespaceKM.locate(ns_uuid, ns_object)

        self.enqueue_event(ns_add_event)
        self.wait_for_all_tasks_done()

        return ns_uuid

    def _create_virtual_network(self, proj_obj, vn_name):
        pod_vn_obj, service_vn_obj = \
            self.create_pod_service_network(
                vn_name, self.service_vn_name,
                proj_obj, '10.32.0.0/12', '10.96.0.0/12')
        return pod_vn_obj

    def _create_update_pod(self, pod_name, pod_namespace, pod_status,
                           eval_vn_dict, action, labels={}, req_uuid=None,
                           wait=True):

        pod_uuid = req_uuid if req_uuid else str(uuid.uuid4())
        pod_spec = {'nodeName': 'test-node'}
        pod_labels = labels
        pod_meta = {'name': pod_name, 'uid': pod_uuid,
                    'namespace': pod_namespace, 'labels': pod_labels}
        if eval_vn_dict:
            pod_meta['annotations'] = {
                'opencontrail.org/network': eval_vn_dict}
        self.set_mock_for_kube()
        pod_add_event = self.create_event('Pod', pod_spec, pod_meta, action)
        pod_add_event['object']['status'] = pod_status
        pod = PodKM.locate(pod_uuid, pod_add_event['object'])
        self.enqueue_event(pod_add_event)
        if wait:
            self.wait_for_all_tasks_done()
        return TestPod(pod.uuid, pod_meta, pod_spec)

    def set_mock_for_kube(self):
        if VncKubernetes._vnc_kubernetes is not None:
            VncKubernetes._vnc_kubernetes.pod_mgr._kube = mock.MagicMock()

    def _delete_pod(self, testpod, uuid=None, spec=None, meta=None, wait=True):
        pod_del_event = self.create_event(
            'Pod',
            spec if spec else testpod.spec,
            meta if meta else testpod.meta,
            'DELETED')
        PodKM.delete(uuid if uuid else testpod.uuid)
        self.enqueue_event(pod_del_event)
        if wait:
            self.wait_for_all_tasks_done()

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
            except Exception:
                pass

        self.assertIsNotNone(subnet)
        iip_ip = ipaddress.ip_address(str(iip_obj.instance_ip_address))
        vn_network = ipaddress.ip_network(subnet.ip_prefix + u'/' +
                                          str(subnet.ip_prefix_len))
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
#            self.assertTrue(len(vmi.security_groups) > 1)
#            for sg_uuid in list(vmi.security_groups):
#                sg = self._vnc_lib.security_group_read(id=sg_uuid)
#                self.assertIsNotNone(sg)
            self.assertTrue(len(vmi.instance_ips) == 1)
            iip_uuid = list(vmi.instance_ips)[0]
            iip = self._vnc_lib.instance_ip_read(id=iip_uuid)
            self.assertIsNotNone(iip)
            self._assert_pod_ip_is_from_vn_ipam(iip, vn_obj_uuid)


class VncPodTestClusterProjectDefined(VncPodTest):
    def setUp(self, extra_config_knobs=None):
        super(VncPodTestClusterProjectDefined, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncPodTestClusterProjectDefined, self).tearDown()

    def _add_update_pod(self, action):
        ns_name = self.ns_name + '_' + str(uuid.uuid4())
        self._create_namespace(ns_name, None)

        proj_fq_name = ['default-domain', self.cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj_uuid = self._create_virtual_network(proj_obj, self.vn_name).uuid
        testpod = self._create_update_pod(self.pod_name,
                                          ns_name,
                                          self.pod_status,
                                          None, action)

        self._assert_virtual_network(vn_obj_uuid)
        self._assert_virtual_machine(testpod.uuid, self.cluster_project,
                                     proj_obj, vn_obj_uuid)

        self._delete_pod(testpod)

        self._assert_virtual_network(vn_obj_uuid)

        tmp_fq_name = ['default-domain', self.cluster_project, self.pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj_uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def test_pod_add_delete(self):
        self._add_update_pod('ADDED')

    def test_update_pod_before_add(self):
        self._add_update_pod('MODIFIED')

    def test_delete_add_pod_after_kube_manager_is_killed(self):
        self._create_namespace(self.ns_name, None)

        proj_fq_name = ['default-domain', self.cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        vn_obj = self._create_virtual_network(proj_obj, self.vn_name)

        testpod = self._create_update_pod(self.pod_name,
                                          self.ns_name,
                                          self.pod_status,
                                          None, 'ADDED')

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(testpod.uuid, self.cluster_project,
                                     proj_obj, vn_obj.uuid)

        self.kill_kube_manager()

        self._delete_pod(testpod, wait=False)
        testpod = self._create_update_pod(self.pod_name,
                                          self.ns_name,
                                          self.pod_status,
                                          None, 'ADDED',
                                          wait=False)

        self.spawn_kube_manager()
        self.set_mock_for_kube()
        self.wait_for_all_tasks_done()

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(testpod.uuid, self.cluster_project,
                                     proj_obj, vn_obj.uuid)

        self._delete_pod(testpod)
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)

        self._assert_virtual_network(vn_obj.uuid)

        tmp_fq_name = ['default-domain', self.cluster_project, self.pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)


class VncPodTestClusterProjectUndefined(VncPodTestClusterProjectDefined):
    def setUp(self, extra_config_knobs=None):
        super(VncPodTestClusterProjectUndefined, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncPodTestClusterProjectUndefined, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncPodTestClusterProjectUndefined, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)

        cls.cluster_project = cls.ns_name

        args = {}
        args['domain'] = 'default-domain'
        args['project'] = cls.cluster_project
        args['name'] = cls.vn_name

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = repr(args)
        kube_config.VncKubernetesConfig.vnc_kubernetes_config[
            'cluster_pod_ipam_fq_name'] = \
            ['default-domain', cls.cluster_project, 'pod-ipam']


class VncPodTestNamespaceIsolation(VncPodTest):
    def setUp(self, extra_config_knobs=None):
        super(VncPodTestNamespaceIsolation, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncPodTestNamespaceIsolation, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncPodTestNamespaceIsolation, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        cls.ns_name = 'test-namespace-isolated'
        cls.cluster_project = cls.ns_name
        cls.vn_name = cls.cluster_name() + '-' + cls.ns_name + '-pod-network'
        args = {}
        args['domain'] = 'default-domain'
        args['project'] = cls.cluster_project
        args['name'] = cls.vn_name

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = repr(args)
        kube_config.VncKubernetesConfig.vnc_kubernetes_config[
            'cluster_pod_ipam_fq_name'] = \
            ['default-domain', cls.cluster_project, 'pod-ipam']

    def test_pod_add_delete_with_namespace_isolation_true(self):
        proj_obj = self.create_project(self.cluster_project)
        self._create_network_ipam('pod-ipam', 'flat-subnet', '10.32.0.0/12',
                                  proj_obj)

        self._create_namespace(self.ns_name, None, True)
        vn_obj = self._vnc_lib.virtual_network_read(
            fq_name=['default-domain', self.cluster_project, self.vn_name])

        testpod = self._create_update_pod(self.pod_name,
                                          self.ns_name,
                                          self.pod_status,
                                          None, 'ADDED')

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(testpod.uuid, self.cluster_project,
                                     proj_obj, vn_obj.uuid)

        self._delete_pod(testpod)

        self._assert_virtual_network(vn_obj.uuid)

        tmp_fq_name = ['default-domain', self.ns_name, self.pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)


class VncPodTestCustomNetworkAnnotation(VncPodTest):
    def setUp(self, extra_config_knobs=None):
        super(VncPodTestCustomNetworkAnnotation, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncPodTestCustomNetworkAnnotation, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncPodTestCustomNetworkAnnotation, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        cls.eval_vn_dict = '{"domain":"default-domain",\
                             "project":"%s",\
                             "name":"%s"}' % (cls.cluster_project,
                                              cls.vn_name)

    def test_pod_add_delete_with_namespace_custom_network_annotation(self):
        proj_obj = self.create_project(self.cluster_project)
        vn_obj = self._create_virtual_network(proj_obj, self.vn_name)
        self._create_namespace(self.ns_name, self.eval_vn_dict)

        proj_fq_name = ['default-domain', self.cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        testpod = self._create_update_pod(self.pod_name,
                                          self.ns_name,
                                          self.pod_status,
                                          None, 'ADDED')

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(testpod.uuid, self.cluster_project,
                                     proj_obj, vn_obj.uuid)

        self._delete_pod(testpod)

        self._assert_virtual_network(vn_obj.uuid)

        tmp_fq_name = ['default-domain', self.ns_name, self.pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)

    def test_pod_add_delete_with_pod_custom_network_annotation(self):
        proj_obj = self.create_project(self.cluster_project)
        vn_obj = self._create_virtual_network(proj_obj, self.vn_name)
        self._create_namespace(self.ns_name, None)

        proj_fq_name = ['default-domain', self.cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        testpod = self._create_update_pod(self.pod_name,
                                          self.ns_name,
                                          self.pod_status,
                                          self.eval_vn_dict, 'ADDED')

        self._assert_virtual_network(vn_obj.uuid)
        self._assert_virtual_machine(testpod.uuid, self.cluster_project,
                                     proj_obj, vn_obj.uuid)

        self._delete_pod(testpod)

        self._assert_virtual_network(vn_obj.uuid)

        tmp_fq_name = ['default-domain', self.ns_name, self.pod_name]
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_machine_read,
            fq_name=tmp_fq_name
        )
        vn_obj = VirtualNetworkKM.locate(vn_obj.uuid)
        self.assertTrue(len(vn_obj.instance_ips) == 0)


class VncPodTestScaling(VncPodTest):
    def setUp(self, extra_config_knobs=None):
        super(VncPodTestScaling, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncPodTestScaling, self).tearDown()

    def test_pod_add_scaling(self):
        scale = 100
        self._create_namespace(self.ns_name, None)

        proj_fq_name = ['default-domain', self.cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj_uuid = self._create_virtual_network(proj_obj, self.vn_name).uuid
        self._assert_virtual_network(vn_obj_uuid)

        pods = []
        for i in range(scale):
            testpod = self._create_update_pod(self.pod_name + str(i),
                                              self.ns_name,
                                              self.pod_status,
                                              None, 'ADDED')
            self._assert_virtual_machine(testpod.uuid, self.cluster_project,
                                         proj_obj, vn_obj_uuid)
            pods.append(testpod)

        vn_obj = VirtualNetworkKM.locate(vn_obj_uuid)
        self.assertTrue(len(vn_obj.instance_ips) == scale)

        for i, pod in enumerate(pods):
            self._delete_pod(pod)
            vn_obj = VirtualNetworkKM.locate(vn_obj_uuid)
            self.assertTrue(len(vn_obj.instance_ips) == scale - 1 - i)


class VncPodLabelsTest(VncPodTest):

    def _construct_tag_name(self, type, value):
        return "=".join([type, value])

    def _construct_tag_fq_name(self, type, value, proj_obj=None):
        if proj_obj:
            tag_fq_name = proj_obj['fq_name'] + \
                [self._construct_tag_name(type, value)]
        else:
            tag_fq_name = [self._construct_tag_name(type, value)]
        return tag_fq_name

    def _validate_tags(self, labels, validate_delete=False, proj_obj=None):

        for key, value in labels.items():
            tag_fq_name = self._construct_tag_fq_name(key, value)
            try:
                _ = self._vnc_lib.tag_read(fq_name=tag_fq_name)
            except NoIdError:
                if not validate_delete:
                    self.assertTrue(False)

            tag_uuid = TagKM.get_fq_name_to_uuid(tag_fq_name)
            if validate_delete:
                self.assertIsNone(tag_uuid)
            else:
                self.assertIsNotNone(tag_uuid)

            # TBD: validate tags are available on the VM.

    def _validate_label_cache(self, uuid, labels):
        obj_labels = XLabelCache.get_labels(uuid)
        for key, value in labels.items():
            label_key = XLabelCache.get_key(key, value)
            self.assertIn(label_key, obj_labels)

    def _add_update_pod(self, action, labels={}, uuid=None):
        self._create_namespace(self.ns_name, None)

        proj_fq_name = ['default-domain', self.cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj_uuid = self._create_virtual_network(proj_obj, self.vn_name).uuid

        pod_uuid, pod_meta, pod_spec = self._create_update_pod(self.pod_name,
                                                               self.ns_name,
                                                               self.pod_status,
                                                               None, action,
                                                               labels=labels,
                                                               req_uuid=uuid)
        self._assert_virtual_network(vn_obj_uuid)
        self._assert_virtual_machine(pod_uuid, self.cluster_project,
                                     proj_obj, vn_obj_uuid)

        if labels:
            self._validate_label_cache(pod_uuid, labels)

        return pod_uuid

    def _delete_pod(self, pod_uuid):

        pod = PodKM.find_by_name_or_uuid(pod_uuid)
        self.assertIsNotNone(pod)

        pod_spec = self._construct_pod_spec(pod.nodename)
        pod_meta = self._construct_pod_meta(pod.name, pod_uuid,
                                            pod.namespace)
        super(VncPodLabelsTest, self)._delete_pod(None, pod_uuid, pod_spec, pod_meta)

    def test_pod_add_delete(self):

        labels = {"testcase": unittest.TestCase.id(self)}
        pod_uuid = self._add_update_pod('ADDED', dict(labels))
        self._validate_tags(labels)

        # Verify that namespace tag is associated with this pod,internally.
        ns_label = XLabelCache.get_namespace_label(self.ns_name)
        self._validate_label_cache(pod_uuid, ns_label)

        labels['modify'] = "testing_label_modify"
        pod_uuid = self._add_update_pod('MODIFIED', dict(labels), pod_uuid)
        self._validate_tags(labels)

        self._delete_pod(pod_uuid)
        self._validate_tags(labels, validate_delete=True)
