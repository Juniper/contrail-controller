#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import uuid

from cfgm_common.exceptions import NoIdError

from kube_manager.tests.vnc.test_case import KMTestCase
from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.vnc.vnc_namespace import NamespaceKM
from kube_manager.vnc.config_db import TagKM


class VncNamespaceTest(KMTestCase):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTest, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTest, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None, kube_args=()):
        super(VncNamespaceTest, cls).setUpClass(
            extra_config_knobs=extra_config_knobs, kube_args=())
        cls.domain = 'default-domain'
        cls.cluster_project = 'test-project'
        cls.ns_name = 'test-namespace'

        prj_dict = {}
        prj_dict['project'] = cls.cluster_project
        kube_config.VncKubernetesConfig.args().cluster_project = repr(prj_dict)
        kube_config.VncKubernetesConfig.args().cluster_network = None

    def _create_and_add_namespace(self, name, spec, annotations,
                                  labels=None, locate=False):
        ns_uuid = str(uuid.uuid4())
        ns_meta = {'name': name,
                   'uid': ns_uuid}
        if annotations:
            ns_meta['annotations'] = annotations
        if labels:
            ns_meta['labels'] = labels

        ns_add_event = self.create_event('Namespace', spec, ns_meta, 'ADDED')
        if locate:
            NamespaceKM.locate(name, ns_add_event['object'])
            NamespaceKM.locate(ns_uuid, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        self.wait_for_all_tasks_done()
        return ns_uuid


class VncNamespaceTestClusterProjectDefined(VncNamespaceTest):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTestClusterProjectDefined, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTestClusterProjectDefined, self).tearDown()

    def _mod_namespace(self, uuid, name, spec, annotations,
                       labels=None, locate=False):
        ns_uuid = uuid
        ns_meta = {'name': name,
                   'uid': ns_uuid}
        if annotations:
            ns_meta['annotations'] = annotations
        if labels:
            ns_meta['labels'] = labels

        ns_mod_event = self.create_event('Namespace', spec, ns_meta, 'MODIFIED')
        if locate:
            NamespaceKM.locate(name, ns_mod_event['object'])
        self.enqueue_event(ns_mod_event)
        self.wait_for_all_tasks_done()

    def _delete_namespace(self, name, uuid):
        ns_del_event = self.create_delete_namespace_event(name, uuid)
        self.enqueue_event(ns_del_event)
        self.wait_for_all_tasks_done()

    def test_add_namespace(self):
        ns_uuid = self._create_and_add_namespace(
            self.ns_name, {}, None, locate=True)
        # Check for project
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(
            self.cluster_project)
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", proj_name])
        self.assertIsNotNone(proj)
        self.assertEquals(proj_name, proj.name)

        NamespaceKM.delete(ns_uuid)
        NamespaceKM.delete(self.ns_name)

    def test_add_namespace_with_isolation_annotation(self):
        ns_annotations = {'opencontrail.org/isolation': "true"}

        ns_uuid = self._create_and_add_namespace(
            self.ns_name, {}, ns_annotations, locate=True)
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(
            self.cluster_project)
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", proj_name])
        self.assertIsNotNone(proj)
        self.assertEquals(proj_name, proj.name)

        fqname = [
            'default-domain', proj_name,
            self.cluster_name() + '-' + self.ns_name + '-pod-network']
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
    def setUpClass(cls, extra_config_knobs=None, kube_args=()):
        super(VncNamespaceTestClusterProjectUndefined, cls).setUpClass(
            extra_config_knobs=extra_config_knobs, kube_args=())
        cls.domain = 'default-domain'
        cls.ns_name = 'test-namespace'
        cls.cluster_project = cls.ns_name

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None

    def test_add_namespace(self):
        ns_uuid = self._create_and_add_namespace(
            self.ns_name, {}, None, locate=True)
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(
            self.ns_name)
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", proj_name])
        self.assertIsNotNone(proj)
        self.assertEquals(proj_name, proj.name)

        self._delete_namespace(self.ns_name, ns_uuid)

        # Validate that the project is deleted.
        try:
            self._vnc_lib.project_read(
                fq_name=["default-domain", proj_name])
        except NoIdError:
            pass
        else:
            # Project exists. Assert.
            self.assertIsNotNone(None)


class VncNamespaceTestCustomNetwork(VncNamespaceTest):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTestCustomNetwork, self).setUp(
            extra_config_knobs=extra_config_knobs)

    def tearDown(self):
        super(VncNamespaceTestCustomNetwork, self).tearDown()

    @classmethod
    def setUpClass(cls, extra_config_knobs=None, kube_args=()):
        super(VncNamespaceTestCustomNetwork, cls).setUpClass(
            extra_config_knobs=extra_config_knobs, kube_args=())
        cls.domain = 'default-domain'
        cls.ns_name = 'test-namespace'
        cls.vn_name = 'test-network'
        cls.cluster_project = 'default'

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None

    def test_add_namespace_with_custom_network_annotation(self):
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(self.cluster_project)
        proj_fq_name = [self.domain, proj_name]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        self.create_network(self.vn_name, proj_obj, '10.32.0.0/12',
                            'pod-ipam')

        vn_dict = {'domain': self.domain,
                   'project': proj_name,
                   'name': self.vn_name}
        ns_annotations = {'opencontrail.org/network': repr(vn_dict)}

        self._create_and_add_namespace(self.ns_name, {}, ns_annotations, locate=True)

        ns_proj_name = kube_config.VncKubernetesConfig.cluster_project_name(self.ns_name)
        proj = self._vnc_lib.project_read(fq_name=["default-domain",
                                                   ns_proj_name])
        self.assertIsNotNone(proj)
        self.assertEquals(ns_proj_name, proj.name)

        fqname = [self.domain, proj_name, self.vn_name]
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
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(self.cluster_project)
        for i in range(scale):
            ns_uuid = self._create_and_add_namespace(self.ns_name + str(i), {},
                                                     None, locate=True)
            proj = self._vnc_lib.project_read(fq_name=["default-domain",
                                                       proj_name])
            self.assertIsNotNone(proj)
            self.assertEquals(proj_name, proj.name)

            ns_uuids.append(ns_uuid)

        for i, ns_uuid in enumerate(ns_uuids):
            ns = NamespaceKM.find_by_name_or_uuid(ns_uuid)
            if ns:
                NamespaceKM.delete(ns_uuid)
                NamespaceKM.delete(ns.name)


class VncNamespaceLabelsTest(VncNamespaceTestClusterProjectDefined):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceLabelsTest, self).setUp(
            extra_config_knobs=extra_config_knobs)
    # end setUp

    def tearDown(self):
        super(VncNamespaceLabelsTest, self).tearDown()
    # end tearDown

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncNamespaceLabelsTest, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        cls.domain = 'default-domain'
        cls.ns_name = 'test-namespace'
        cls.vn_name = 'test-network'
        cls.cluster_project = 'default'

        kube_config.VncKubernetesConfig.args().cluster_project = None
        kube_config.VncKubernetesConfig.args().cluster_network = None

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

    def test_add_namespace(self):
        labels = {'nsname': self.ns_name}
        ns_uuid = self._create_and_add_namespace(
            self.ns_name, {}, None, labels, locate=True)

        # Validate that tags have been created in VNC.
        self._validate_tags(labels)

        # Add addition label on the namespace.
        labels['region'] = "US-WEST"

        self._mod_namespace(ns_uuid, self.ns_name, {}, None, labels)

        # Validate that tags have been created in VNC.
        self._validate_tags(labels)

        # Remove a label from the namespace and verify mod.
        removed_label = {}
        removed_label['region'] = labels.pop('region')
        self._mod_namespace(ns_uuid, self.ns_name, {}, None, labels)
        self._validate_tags(labels)
        self._validate_tags(removed_label, validate_delete=True)

        # Modify existing label on a namespace and verify mod.
        labels['nsname'] = "new-" + self.ns_name
        self._mod_namespace(ns_uuid, self.ns_name, {}, None, labels)
        self._validate_tags(labels)

        # Delete the namespace and validate labels are gone.

        self._delete_namespace(self.ns_name, ns_uuid)
        self._validate_tags(labels, validate_delete=True)
        pass

    def test_add_namespace_with_isolation_annotation(self):
        pass
