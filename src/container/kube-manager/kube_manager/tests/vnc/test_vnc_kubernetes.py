#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import unittest

from mock import patch, Mock

from vnc_api.vnc_api import Domain, Project, NetworkIpam, VirtualNetwork, VnSubnetsType
from kube_manager.vnc import vnc_kubernetes
from kube_manager.tests.vnc.db_mock import DBBaseKM, DBMock
from kube_manager.tests.vnc.vnc_api_mock import VncApiMock
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as \
                                                        vnc_kubernetes_config


class VncKubernetesTest(unittest.TestCase):
    def setUp(self):
        VncApiMock.init()
        DBMock.init()
        vnc_kubernetes.VncKubernetes.reset()

        self.args = Mock()
        self.args.admin_user = "admin"
        self.args.admin_password = "qwerty"
        self.args.admin_tenant = "default"
        self.args.vnc_endpoint_ip = '127.0.0.1'
        self.args.vnc_endpoint_port = "8082"
        self.args.auth_token_url = "token"
        self.args.cluster_project = None
        self.args.cluster_network = None
        self.args.cluster_pod_network = None
        self.args.cluster_service_network = None
        self.args.cluster_name = "cluster"
        self.args.pod_subnets = ['10.10.0.0/16']
        self.args.ip_fabric_subnets = ['20.20.0.0/16']
        self.args.service_subnets = ['192.168.0.0/24']
        self.args.kubernetes_api_secure_port = "8443"
        self.args.auth_user = "admin"
        self.args.auth_password = "qwerty"
        self.args.auth_tenant = "default"
        self.args.db_driver = "cassandra"
        self.args.cassandra_server_list = ()
        self.args.aps_name = "test-aps"
        self.args.rabbit_port = None
        self.args.collectors = ""

        api = VncApiMock(
            self.args.auth_user,
            self.args.auth_password,
            self.args.auth_tenant,
            self.args.vnc_endpoint_ip,
            self.args.vnc_endpoint_port,
            self.args.auth_token_url
        )
        domain_uuid = api.domain_create(Domain("default-domain"))
        domain = api.domain_read(id=domain_uuid)

        proj_uuid = api.project_create(Project("default-project", parent_obj=domain))
        proj = api.project_read(id=proj_uuid)
        net = VirtualNetwork("ip-fabric", proj)
        api.virtual_network_create(net)

    def tearDown(self):
        vnc_kubernetes.VncKubernetes.reset()
        pass

    def verify_if_created(self, res_type, name, parent_fq_name):
        obj = VncApiMock.read(res_type, fq_name=parent_fq_name+[name])
        self.assertEquals(name, obj.name)
        uuid = obj.uuid
        ok, obj_list = DBMock.read(res_type.replace('-', '_'), [uuid])
        self.assertEquals(True, ok)
        self.assertEquals(parent_fq_name+[name], obj_list[0]['fq_name'])
        return obj

    def verify_if_synchronized(self, cls, obj):
        km_obj = cls.get(obj.uuid)
        self.assertEquals(obj.name, km_obj.name)
        self.assertEquals(obj.uuid, km_obj.uuid)
        return km_obj

    @patch("kube_manager.vnc.db.KubeNetworkManagerDB", new=DBMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncApi", new=VncApiMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncAmqpHandle")
    def test_sync_km(self, mock_vnc_amqp_handle_init):
        # Put some objects to database
        DBMock.create('domain', '123', {
            'uuid': '123',
            'fq_name': ['test-domain']
        })
        DBMock.create('project', '234', {
            'uuid': '234',
            'fq_name': ['test-domain', 'test-proj-1'],
            'parent_uuid': '123'
        })
        DBMock.create('project', '345', {
            'uuid': '345',
            'fq_name': ['test-domain', 'test-proj-2'],
            'parent_uuid': '123'
        })

        mock_vnc_amqp_handle = Mock()
        mock_vnc_amqp_handle_init.return_value = mock_vnc_amqp_handle
        vnc_kubernetes.VncKubernetes(self.args, Mock())
        mock_vnc_amqp_handle.establish.assert_called_once_with()

        # check if KM dictionaries are synchronized with database
        self.assertEquals(2, len(vnc_kubernetes.DomainKM.list_obj()))
        self.assertEquals(5, len(vnc_kubernetes.ProjectKM.list_obj()))
        obj = vnc_kubernetes.DomainKM.get('123')
        self.assertIsNotNone(obj)
        self.assertEquals(['test-domain'], obj.fq_name)
        self.assertEquals('123', obj.uuid)
        obj = vnc_kubernetes.ProjectKM.get('234')
        self.assertIsNotNone(obj)
        self.assertEquals('test-proj-1', obj.name)
        self.assertEquals(['test-domain', 'test-proj-1'], obj.fq_name)
        self.assertEquals('234', obj.uuid)
        obj = vnc_kubernetes.ProjectKM.get('345')
        self.assertIsNotNone(obj)
        self.assertEquals('test-proj-2', obj.name)
        self.assertEquals(['test-domain', 'test-proj-2'], obj.fq_name)
        self.assertEquals('345', obj.uuid)

    @patch("kube_manager.vnc.db.KubeNetworkManagerDB", new=DBMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncApi", new=VncApiMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncAmqpHandle", new=Mock())
    def test_nested_mode(self):
        old_nested_mode = DBBaseKM.is_nested()
        self.args.nested_mode = "1"
        vnc_kubernetes.VncKubernetes(self.args, Mock())
        self.assertTrue(DBBaseKM.is_nested())
        DBBaseKM.set_nested(old_nested_mode)

    @patch("kube_manager.vnc.db.KubeNetworkManagerDB", new=DBMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncApi", new=VncApiMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncAmqpHandle", new=Mock())
    def test_create_resources(self):
        vnc_kubernetes.VncKubernetes(self.args, Mock())

        default_proj_name = vnc_kubernetes_config.cluster_project_name('default')
        kube_system_proj_name = vnc_kubernetes_config.cluster_project_name('kube-system')

        # Verify projects
        system_proj = self.verify_if_created('project', kube_system_proj_name,
                                                ['default-domain'])
        default_proj = self.verify_if_created('project', default_proj_name,
                                                ['default-domain'])
        self.verify_if_synchronized(vnc_kubernetes.ProjectKM, system_proj)
        self.verify_if_synchronized(vnc_kubernetes.ProjectKM, default_proj)

        # Verify cluster pod network
        net = self.verify_if_created('virtual-network', 'cluster-default-pod-network',
                                        ['default-domain', default_proj_name])
        self.verify_if_synchronized(vnc_kubernetes.VirtualNetworkKM, net)
        ipam_refs = net.get_network_ipam_refs()
        self.assertEquals(1, len(ipam_refs))
        self.assertEquals([], ipam_refs[0]['attr'].ipam_subnets)

        # Verify pod ipam
        pod_ipam = self.verify_if_created('network-ipam', self.args.cluster_name + '-pod-ipam',
                                          ['default-domain', default_proj_name])
        self.verify_if_synchronized(vnc_kubernetes.NetworkIpamKM, pod_ipam)
        self.assertEquals('flat-subnet', pod_ipam.get_ipam_subnet_method())
        self.assertEquals(16, pod_ipam.get_ipam_subnets().subnets[0].subnet.get_ip_prefix_len())
        self.assertEquals('10.10.0.0', pod_ipam.get_ipam_subnets().subnets[0].subnet.get_ip_prefix())

        # Verify cluster service network
        net = self.verify_if_created(
            'virtual-network', 'cluster-default-service-network',
            ['default-domain', default_proj_name])
        self.verify_if_synchronized(vnc_kubernetes.VirtualNetworkKM, net)
        ipam_refs = net.get_network_ipam_refs()
        self.assertEquals(1, len(ipam_refs))
        self.assertEquals([], ipam_refs[0]['attr'].ipam_subnets)

        # Verify service ipam
        service_ipam = self.verify_if_created('network-ipam', self.args.cluster_name +'-service-ipam',
                                          ['default-domain', default_proj_name])
        self.verify_if_synchronized(vnc_kubernetes.NetworkIpamKM, service_ipam)
        self.assertEquals('flat-subnet', pod_ipam.get_ipam_subnet_method())
        self.assertEquals(24, service_ipam.get_ipam_subnets().subnets[0].subnet.get_ip_prefix_len())
        self.assertEquals('192.168.0.0', service_ipam.get_ipam_subnets().subnets[0].subnet.get_ip_prefix())

    @patch("kube_manager.vnc.db.KubeNetworkManagerDB", new=DBMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncApi", new=VncApiMock)
    @patch("kube_manager.vnc.vnc_kubernetes.VncAmqpHandle", new=Mock())
    def test_resources_exists(self):
        api = VncApiMock(
            self.args.auth_user,
            self.args.auth_password,
            self.args.auth_tenant,
            self.args.vnc_endpoint_ip,
            self.args.vnc_endpoint_port,
            self.args.auth_token_url
        )
        domain_fq_name = ['default-domain']
        domain = api.domain_read(fq_name=domain_fq_name)

        proj_uuid = api.project_create(Project("default", parent_obj=domain))
        proj = api.project_read(id=proj_uuid)

        # Create cluster-default-pod-network
        ipam_uuid = api.network_ipam_create(NetworkIpam("pod-ipam", proj))
        ipam = api.network_ipam_read(id=ipam_uuid)
        net = VirtualNetwork("cluster-default-pod-network", proj)
        # No subnets are associated with IPAM at this point.
        # Subnets will be updated in the IPAM, when cluster is created.
        net.add_network_ipam(ipam, VnSubnetsType([]))
        api.virtual_network_create(net)

        # Create cluster-default-service-network
        ipam_uuid = api.network_ipam_create(NetworkIpam("service-ipam", proj))
        ipam = api.network_ipam_read(id=ipam_uuid)
        net = VirtualNetwork("cluster-default-service-network", proj)
        # No subnets are associated with IPAM at this point.
        # Subnets will be updated in the IPAM, when cluster is created.
        net.add_network_ipam(ipam, VnSubnetsType([]))
        api.virtual_network_create(net)

        vnc_kubernetes.VncKubernetes(self.args, Mock())
