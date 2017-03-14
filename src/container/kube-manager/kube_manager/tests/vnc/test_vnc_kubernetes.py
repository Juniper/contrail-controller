#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import mock
import unittest
from mock import patch, Mock

from vnc_api.vnc_api import *
from kube_manager import kube_manager
from kube_manager.vnc import vnc_kubernetes
from kube_manager.tests.vnc.db_mock import *
from kube_manager.tests.vnc.vnc_api_mock import *


class VncKubernetesTest(unittest.TestCase):
    def setUp(self):
        VncApiMock.init()
        DBMock.init()
        for cls in DBBaseKM.get_obj_type_map().values():
            cls.reset()

        self.args = Mock()
        self.args.admin_user = VncApiMock.admin_user
        self.args.admin_password = VncApiMock.admin_password
        self.args.admin_tenant = VncApiMock.admin_tenant
        self.args.vnc_endpoint_ip = VncApiMock.vnc_endpoint_ip
        self.args.vnc_endpoint_port = VncApiMock.vnc_endpoint_port
        self.args.auth_token_url = VncApiMock.auth_token_url

    def tearDown(self):
        pass

    def verify_if_created(self, res_type, name, parent_fq_name, uuid=None):
        obj2 = VncApiMock.read(res_type, fq_name=parent_fq_name+[name])
        self.assertEquals(name, obj2.name)
        if uuid is not None:
            self.assertEquals(uuid, obj2.uuid)
        else:
            uuid = obj2.uuid
        ok, obj_list = DBMock.read(res_type.replace('-', '_'), [uuid])
        self.assertEquals(True, ok)
        self.assertEquals(parent_fq_name+[name], obj_list[0]["fq_name"])

    @patch("kube_manager.vnc.db.KubeNetworkManagerDB", new=DBMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncApi", new=VncApiMock)
    @patch.object(vnc_kubernetes.VncKubernetes,
                  "_get_cluster_service_fip_pool", new=Mock())
    @patch("kube_manager.vnc.vnc_service.VncService", new=Mock())
    @patch("kube_manager.vnc.vnc_kubernetes.VncAmqpHandle")
    @patch.object(vnc_kubernetes.VncKubernetes, "_provision_cluster")
    def test_vnc_kubernetes_init(self, mock_provision_cluster, mock_vnc_amqp_handle_init):
        # Put some objects to database
        DBMock.create("domain", "123", {
            "uuid": "123",
            "fq_name": ["default-domain"]
        })
        DBMock.create("project", "234", {
            "uuid": "234",
            "fq_name": ["default-domain", "proj1"],
            "parent_uuid": "123"
        })
        DBMock.create("project", "345", {
            "uuid": "345",
            "fq_name": ["default-domain", "proj2"],
            "parent_uuid": "123"
        })

        mock_vnc_amqp_handle = Mock()
        mock_vnc_amqp_handle_init.return_value = mock_vnc_amqp_handle
        vnc = vnc_kubernetes.VncKubernetes(self.args, Mock())
        mock_vnc_amqp_handle.establish.assert_called_once_with()
        mock_provision_cluster.assert_called_once_with()

        # check if KM dictionaries are synchronized with database
        self.assertEquals(1, len(vnc_kubernetes.DomainKM.list_obj()))
        self.assertEquals(2, len(vnc_kubernetes.ProjectKM.list_obj()))
        obj = vnc_kubernetes.DomainKM.get("123")
        self.assertIsNotNone(obj)
        self.assertEquals(["default-domain"], obj.fq_name)
        self.assertEquals("123", obj.uuid)
        obj = vnc_kubernetes.ProjectKM.get("234")
        self.assertIsNotNone(obj)
        self.assertEquals("proj1", obj.name)
        self.assertEquals(["default-domain", "proj1"], obj.fq_name)
        self.assertEquals("234", obj.uuid)
        obj = vnc_kubernetes.ProjectKM.get("345")
        self.assertIsNotNone(obj)
        self.assertEquals("proj2", obj.name)
        self.assertEquals(["default-domain", "proj2"], obj.fq_name)
        self.assertEquals("345", obj.uuid)

    @patch("kube_manager.vnc.db.KubeNetworkManagerDB", new=DBMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncApi", new=VncApiMock)
    @patch.object(vnc_kubernetes.VncKubernetes,
                  "_get_cluster_service_fip_pool", new=Mock())
    @patch("kube_manager.vnc.vnc_service.VncService", new=Mock())
    @patch("kube_manager.vnc.vnc_kubernetes.VncAmqpHandle", new=Mock())
    @patch.object(vnc_kubernetes.VncKubernetes, "_provision_cluster", new=Mock())
    def test_create_project(self):
        vnc = vnc_kubernetes.VncKubernetes(self.args, Mock())

        # Create project
        proj = vnc._create_project("test-project")
        self.assertEquals("test-project", proj.name)
        self.verify_if_created("project", "test-project", ["default-domain"], proj.uuid)
        self.verify_if_created("security-group", "default", ["default-domain", "test-project"])

        # Check if project was loaded to ProjectKM._dict
        proj2 = vnc_kubernetes.ProjectKM.get(proj.uuid)
        self.assertEquals("test-project", proj2.name)
        self.assertEquals(proj.uuid, proj2.uuid)

        # Try to create project with the same name
        proj3 = vnc._create_project("test-project")
        self.assertEquals(proj.uuid, proj3.uuid)

    @patch("kube_manager.vnc.db.KubeNetworkManagerDB", new=DBMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncApi", new=VncApiMock)
    @patch.object(vnc_kubernetes.VncKubernetes,
                  "_get_cluster_service_fip_pool", new=Mock())
    @patch("kube_manager.vnc.vnc_service.VncService", new=Mock())
    @patch("kube_manager.vnc.vnc_kubernetes.VncAmqpHandle", new=Mock())
    @patch.object(vnc_kubernetes.VncKubernetes, "_provision_cluster", new=Mock())
    def test_create_ipam(self):
        vnc = vnc_kubernetes.VncKubernetes(self.args, Mock())

        # Create ipam
        proj = vnc._create_project("default-project")
        ipam, subnets = vnc._create_ipam("ipam1", ["10.0.0.0/8", "192.168.1.0/24"], proj)
        self.assertEquals("ipam1", ipam.name)
        self.verify_if_created("network-ipam", "ipam1", ["default-domain", "default-project"], ipam.uuid)

        # Verify subnets
        self.assertEquals("10.0.0.0", subnets[0].get_subnet().get_ip_prefix())
        self.assertEquals(8, subnets[0].get_subnet().get_ip_prefix_len())
        self.assertEquals("192.168.1.0", subnets[1].get_subnet().get_ip_prefix())
        self.assertEquals(24, subnets[1].get_subnet().get_ip_prefix_len())
