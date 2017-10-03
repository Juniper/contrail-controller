#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import uuid

from kube_manager.tests.vnc.test_case import KMTestCase
from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.vnc.vnc_namespace import NamespaceKM

class VncNamespaceTest(KMTestCase):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTest, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTest, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncNamespaceTest, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        cls.domain = 'default-domain'
        cls.cluster_project = 'test-project'
        cls.ns_name = 'test-namespace'

        prj_dict = {}
        prj_dict['project'] = cls.cluster_project
        kube_config.VncKubernetesConfig.args().cluster_project = repr(prj_dict)
        kube_config.VncKubernetesConfig.args().cluster_network = None

    def _create_and_add_namespace(self, name, spec, annotations,
                                  locate=False):
        ns_uuid = str(uuid.uuid4())
        ns_meta = {'name': name,
                   'uid': ns_uuid}
        if annotations:
            ns_meta['annotations'] = annotations
        ns_add_event = self.create_event('Namespace', spec, ns_meta, 'ADDED')
        if locate:
            NamespaceKM.locate(name, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        self.wait_for_all_tasks_done()
        return ns_uuid

class VncNamespaceTestClusterProjectDefined(VncNamespaceTest):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTestClusterProjectDefined, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTestClusterProjectDefined, self).tearDown()

    def test_add_namespace(self):
        ns_uuid = self._create_and_add_namespace(self.ns_name, {}, None)

        # Check for project
        proj = self._vnc_lib.project_read(fq_name=["default-domain",
                                                   self.cluster_project])
        self.assertIsNotNone(proj)
        self.assertEquals(self.cluster_project, proj.name)

        NamespaceKM.delete(ns_uuid)
        NamespaceKM.delete(self.ns_name)

    def test_add_namespace_with_isolation_annotation(self):
        ns_annotations = {'opencontrail.org/isolation': "true"}

        ns_uuid = self._create_and_add_namespace(self.ns_name, {},
                                                 ns_annotations, True)

        proj = self._vnc_lib.project_read(fq_name=["default-domain",
                                                   self.cluster_project])
        self.assertIsNotNone(proj)
        self.assertEquals(self.cluster_project, proj.name)

        fqname = ['default-domain', self.cluster_project, self.ns_name+'-vn']
        vn = self._vnc_lib.virtual_network_read(fq_name=fqname)
        self.assertIsNotNone(vn)

        NamespaceKM.delete(ns_uuid)
        NamespaceKM.delete(self.ns_name)


class VncNamespaceTestClusterProjectUndefined(
        VncNamespaceTestClusterProjectDefined):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTestClusterProjectUndefined, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTestClusterProjectUndefined, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncNamespaceTestClusterProjectUndefined, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        cls.domain = 'default-domain'
        cls.ns_name = 'test-namespace'
        cls.cluster_project = cls.ns_name

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None


class VncNamespaceTestCustomNetwork(VncNamespaceTest):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTestCustomNetwork, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTestCustomNetwork, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncNamespaceTestCustomNetwork, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        cls.domain = 'default-domain'
        cls.ns_name = 'test-namespace'
        cls.vn_name = 'test-network'
        cls.cluster_project = 'default'

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None

    def test_add_namespace_with_custom_network_annotation(self):
        proj_fq_name = [self.domain, self.cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        self.create_network(self.vn_name, proj_obj, '10.32.0.0/12',
                            '10.96.0.0/12')

        vn_dict = {'domain': self.domain,
                   'project': self.cluster_project,
                   'name': self.vn_name}
        ns_annotations = {'opencontrail.org/network': repr(vn_dict)}

        self._create_and_add_namespace(self.ns_name, {}, ns_annotations, True)

        proj = self._vnc_lib.project_read(fq_name=["default-domain",
                                                   self.ns_name])
        self.assertIsNotNone(proj)
        self.assertEquals(self.ns_name, proj.name)

        fqname = [self.domain, self.cluster_project, self.vn_name]
        vn = self._vnc_lib.virtual_network_read(fq_name=fqname)
        self.assertIsNotNone(vn)

class VncNamespaceTestScaling(VncNamespaceTest):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTestScaling, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTestScaling, self).tearDown()

    def test_add_namespace_scaling(self):
        scale = 100
        ns_uuids = []

        for i in xrange(scale):
            ns_uuid = self._create_and_add_namespace(self.ns_name + str(i), {},
                                                     None)
            proj = self._vnc_lib.project_read(fq_name=["default-domain",
                                                       self.cluster_project])
            self.assertIsNotNone(proj)
            self.assertEquals(self.cluster_project, proj.name)

            ns_uuids.append(ns_uuid)

        for i, ns_uuid in enumerate(ns_uuids):
            ns = NamespaceKM.find_by_name_or_uuid(ns_uuid)
            if ns:
                NamespaceKM.delete(ns_uuid)
                NamespaceKM.delete(ns.name)
