#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey

SERVICE_PORT = 80

monkey.patch_all()

from collections import namedtuple
import mock
from netaddr import IPNetwork, IPAddress
import uuid
from cfgm_common.exceptions import NoIdError
from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.common.kube_config_db import NamespaceKM, ServiceKM, IngressKM
from kube_manager.tests.vnc.test_case import KMTestCase
from kube_manager.vnc.vnc_kubernetes import VncKubernetes
from vnc_api.gen.resource_client import VirtualNetwork, FloatingIpPool
from vnc_api.gen.resource_xsd import IpamSubnetType, SubnetType, VnSubnetsType
from vnc_cfg_api_server.gen.resource_client import VirtualRouter

Uuids = namedtuple('Uuids', ['lb_uuid',
                             'vmi_uuid',
                             'vn_uuid',
                             'iips_uuid',
                             'lb_member_uuid',
                             'lb_listener_uuid',
                             'lb_pool_uuid'])


class VncIngressTest(KMTestCase):
    def __init__(self, *args, **kwargs):
        super(VncIngressTest, self).__init__(*args, **kwargs)
        self.namespace = 'guestbook'
        self.cluster_ip = '10.0.0.54'
        self.external_ip = '10.0.0.55'

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncIngressTest, cls).setUpClass(extra_config_knobs)

    def setUp(self, extra_config_knobs=None):
        super(VncIngressTest, self).setUp()

        self._set_default_kube_config()
        self.kube_mock = mock.MagicMock()

        VncKubernetes._vnc_kubernetes.ingress_mgr._kube = self.kube_mock
        self.pub_net_uuid, self.pub_fip_pool_uuid = \
            self._create_fip_pool_and_public_network()

        VncKubernetes.get_instance().ingress_mgr._default_vn_obj = None

    def tearDown(self):
        self._delete_public_network(self.pub_net_uuid, self.pub_fip_pool_uuid)

        super(VncIngressTest, self).tearDown()

    def test_add_delete_ingress_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_add_delete_service_with_default_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network_uuid = self._create_virtual_network(cluster_project)

        ports, srv_meta = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid,
                                       expected_vn_uuid=network_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_service_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)
        network_uuid = self._create_virtual_network(cluster_project)

        ports, srv_meta = self._enqueue_add_service(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid,
                                       expected_vn_uuid=network_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_service_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta = self._enqueue_add_service(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_add_delete_service_with_custom_isolated_namespace_with_no_cluster_defined(self):
        custom_network = 'custom_network'
        project = 'default'
        network_uuid = self._create_virtual_network(project,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(project, custom_network)

        ports, srv_meta = self._enqueue_add_service(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid,
                                       expected_vn_uuid=network_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)

        self._assert_all_is_down(uuids)

    def test_add_delete_service_with_custom_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        custom_network = 'custom_network'
        self.create_project(cluster_project)
        network_uuid = self._create_virtual_network(cluster_project,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(
                cluster_project, custom_network)

        ports, srv_meta = self._enqueue_add_service(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid,
                                       expected_vn_uuid=network_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_loadbalancer_with_default_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network_uuid = self._create_virtual_network(cluster_project)

        ports, srv_meta = self._enqueue_add_loadbalancer(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid,
                                       expected_vn_uuid=network_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_loadbalancer_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta = self._enqueue_add_loadbalancer(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)
        network_uuid = self._create_virtual_network(cluster_project)

        ports, srv_meta = self._enqueue_add_loadbalancer(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid,
                                       expected_vn_uuid=network_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta = self._enqueue_add_loadbalancer(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_add_delete_loadbalancer_with_custom_isolated_namespace_with_no_cluster_defined(self):
        custom_network = 'custom_network'
        project = 'default'
        network_uuid = self._create_virtual_network(project,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(project, custom_network)

        ports, srv_meta = self._enqueue_add_loadbalancer(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)

        self._assert_all_is_down(uuids)

    def test_add_delete_loadbalancer_with_custom_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        custom_network = 'custom_network'
        self.create_project(cluster_project)
        network_uuid = self._create_virtual_network(cluster_project,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(cluster_project,
                                                        custom_network)

        ports, srv_meta = self._enqueue_add_loadbalancer(namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid,
                                       expected_vn_uuid=network_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_kubernetes_service(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta = self._enqueue_add_kubernetes_service(
            namespace_name)
        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = \
            self._create_vrouters_for_all_service_instances(ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid)


        self._assert_link_local_service(ports)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_delete_add_service_after_kube_manager_is_killed(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        ports, srv_meta = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self.kill_kube_manager()

        self._enqueue_delete_service(ports, srv_meta)
        ports, srv_meta = self._enqueue_add_service(namespace_name)
        public_fip_pool_config = str({
            'project': 'default',
            'domain': 'default-domain',
            'network': 'public',
            'name': 'public_fip_pool'
        })
        self.spawn_kube_manager(
            extra_args=[('VNC', 'public_fip_pool', public_fip_pool_config)])
        VncKubernetes._vnc_kubernetes.ingress_mgr._kube = self.kube_mock

        ingress_meta, ingress_uuid = self._enqueue_add_ingress(namespace_name)
        self.wait_for_all_tasks_done()

        vrouter_uuids = self._create_vrouters_for_all_service_instances(
            ingress_uuid)

        uuids = self._assert_all_is_up(ingress_uuid)

        self._delete_vrouters(vrouter_uuids)
        self._enqueue_delete_ingress(ingress_meta)
        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def _assert_all_is_up(self, srv_uuid, expected_vn_uuid=None):
        """
        This method tests existence of these resources:
        - loadbalancer (lb)
        - loadbalancer pool (lb_pool)
        - loadbalancer listener (lb_listener)
        - loadbalancer member (lb_member)
        - virtual network (vn)
        - virtual machine interface (vmi)
        - instance ip (iip)

        And links between them:
        lb_member -> lb_pool -> lb_listener -> lb -> vmi -> vn
        iip -> vn
        """

        # loadbalancer
        lb = self._vnc_lib.loadbalancer_read(
            id=srv_uuid, fields=['loadbalancer_listener_back_refs'])

        # loadbalancer listener -> loadbalancer
        lb_listeners = lb.loadbalancer_listener_back_refs
        self.assertEqual(1, len(lb_listeners))
        lb_listener_uuid = lb_listeners[0]['uuid']
        lb_listener = self._vnc_lib.loadbalancer_listener_read(
            id=lb_listener_uuid, fields=['loadbalancer_pool_back_refs'])
        self._assert_loadbalancer_listener(lb_listener)

        # loadbalancer pool -> loadbalancer listener
        lb_pools = lb_listener.loadbalancer_pool_back_refs
        self.assertEqual(1, len(lb_pools))
        lb_pool_uuid = lb_pools[0]['uuid']
        lb_pool = self._vnc_lib.loadbalancer_pool_read(
            id=lb_pool_uuid, fields=['loadbalancer-pool-loadbalancer-member'])
        self._assert_loadbalancer_pool(lb_pool)

        # loadbalancer member
        lb_members = lb_pool.get_loadbalancer_members()
        self.assertEqual(1, len(lb_members))
        lb_member_uuid = lb_members[0]['uuid']
        lb_member = self._vnc_lib.loadbalancer_member_read(id=lb_member_uuid)
        self.assertEquals(self.cluster_ip,
                          lb_member.loadbalancer_member_properties.address)

        # virtual machine interface <- loadbalancer
        self.assertEqual(1, len(lb.virtual_machine_interface_refs))
        vmi_uuid = lb.virtual_machine_interface_refs[0]['uuid']
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)

        # virtual network <- virtual machine interface
        self.assertEqual(1, len(vmi.virtual_network_refs))
        vn_uuid = vmi.virtual_network_refs[0]['uuid']
        vn = self._vnc_lib.virtual_network_read(
            id=vn_uuid, fields=['instance_ip_back_refs'])
        if expected_vn_uuid:
            self.assertEquals(expected_vn_uuid, vn_uuid)
        #
        # # instance ip -> virtual network
        iips = vn.instance_ip_back_refs
        self.assertEqual(2, len(iips))

        iip_uuid1 = iips[0]['uuid']
        iip1 = self._vnc_lib.instance_ip_read(
            id=iip_uuid1)
        self.assertIsNotNone(iip1.instance_ip_address)

        iip_uuid2 = iips[1]['uuid']
        iip2 = self._vnc_lib.instance_ip_read(
            id=iip_uuid2)
        self.assertIsNotNone(iip2.instance_ip_address)

        return Uuids(lb_uuid=lb.uuid,
                     vmi_uuid=vmi.uuid,
                     vn_uuid=vn.uuid,
                     iips_uuid=[iip_uuid1, iip_uuid2],
                     lb_member_uuid=lb_member_uuid,
                     lb_listener_uuid=lb_listener.uuid,
                     lb_pool_uuid=lb_pool.uuid)


    def _assert_all_is_down(self, uuids, skip_vn=False):
        self.assertRaises(NoIdError,
                          self._vnc_lib.loadbalancer_read, id=uuids.lb_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.virtual_machine_interface_read,
                          id=uuids.vmi_uuid)
        if not skip_vn:
            self.assertRaises(NoIdError,
                              self._vnc_lib.virtual_network_read,
                              id=uuids.vn_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.instance_ip_read, id=uuids.iips_uuid[0])
        self.assertRaises(NoIdError,
                          self._vnc_lib.instance_ip_read, id=uuids.iips_uuid[1])
        self.assertRaises(NoIdError,
                          self._vnc_lib.loadbalancer_listener_read,
                          id=uuids.lb_listener_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.loadbalancer_member_read,
                          id=uuids.lb_member_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.loadbalancer_pool_read,
                          id=uuids.lb_pool_uuid)

    def _create_fip_pool_and_public_network(self):
        net_uuid = self._create_virtual_network('default', network='public')
        net_obj = self._vnc_lib.virtual_network_read(id=net_uuid)
        fip_pool_obj = FloatingIpPool('public_fip_pool', parent_obj=net_obj)
        fip_pool_uuid = self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        kube_config.VncKubernetesConfig.args().public_fip_pool = str({
            'project': 'default',
            'domain': 'default-domain',
            'network': 'public',
            'name': 'public_fip_pool'
        })
        return net_uuid, fip_pool_uuid

    def _assert_link_local_service(self, ports):
        proj_obj = self._vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config',
                     'default-global-vrouter-config'])
        expected_port = ports[0]['port']
        linklocal_service_port = \
            proj_obj.linklocal_services.linklocal_service_entry[
                0].linklocal_service_port
        self.assertEquals(expected_port, linklocal_service_port)

    def _assert_loadbalancer_pool(self, lb_pool):
        self.assertEquals(
            'HTTP',
            lb_pool.loadbalancer_pool_properties.protocol)

    def _assert_loadbalancer_listener(self, lb_listener):
        self.assertEquals(
            'HTTP', lb_listener.loadbalancer_listener_properties.protocol)
        self.assertEquals(
            str(SERVICE_PORT),
            lb_listener.loadbalancer_listener_properties.protocol_port)

    @staticmethod
    def _set_cluster_project():
        cluster_project = 'cluster_project'
        kube_config.VncKubernetesConfig.args().cluster_project = str(
            {'project': cluster_project})
        return cluster_project

    @staticmethod
    def _set_default_kube_config():
        kube_config.VncKubernetesConfig.args().cluster_project = '{}'
        kube_config.VncKubernetesConfig.args().cluster_network = None

    def _enqueue_add_namespace(self, isolated=False):
        ns_uuid = str(uuid.uuid4())
        namespace_name = 'namespace_name'
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uuid)
        # if isolated:
        #     annotations = {'opencontrail.org/isolation': 'true'}
        #     ns_add_event['object']['metadata']['annotations'] = annotations
        NamespaceKM.locate(ns_uuid, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        return namespace_name, ns_uuid

    def _enqueue_add_custom_isolated_namespace(self, project, network):
        custom_network_config = {'virtual_network': network,
                                 'domain': 'default-domain',
                                 'project': project,
                                 'name': network}
        kube_config.VncKubernetesConfig.args().cluster_network = str(
            custom_network_config)
        ns_uuid = str(uuid.uuid4())
        namespace_name = 'custom_isolated_namespace'
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uuid)
        annotations = {'opencontrail.org/network':
                           str({'domain': 'default-domain',
                                'project': project,
                                'name': network})}
        ns_add_event['object']['metadata']['annotations'] = annotations
        NamespaceKM.locate(ns_uuid, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        return namespace_name, ns_uuid

    def _enqueue_delete_namespace(self, namespace_name, ns_uuid):
        ns_delete_event = self.create_delete_namespace_event(namespace_name,
                                                             ns_uuid)
        NamespaceKM.delete(ns_uuid)
        self.enqueue_event(ns_delete_event)

    def _enqueue_add_kubernetes_service(self, namespace_name):
        return self._enqueue_add_service(namespace_name, srv_name='kubernetes')

    def _enqueue_add_loadbalancer(self, namespace_name):
        return self._enqueue_add_service(namespace_name,
                                         srv_type='LoadBalancer')

    def _enqueue_add_service(self,
                             namespace_name,
                             srv_name='test-service',
                             srv_type='ClusterIP'):
        srv_uuid = str(uuid.uuid4())
        srv_meta = {'name': srv_name, 'uid': srv_uuid,
                    'namespace': namespace_name}
        ports = [{'name': 'http', 'protocol': 'TCP', 'port': SERVICE_PORT}]
        srv_spec = {'type': srv_type,
                    'ports': ports,
                    # 'clusterIP': self.cluster_ip,
                    'externalIPs': [self.external_ip]}
        srv_add_event = self.create_event('Service', srv_spec, srv_meta,
                                          'ADDED')
        ServiceKM.locate(srv_uuid, srv_add_event['object'])
        self.enqueue_event(srv_add_event)
        return ports, srv_meta

    def _enqueue_add_ingress(self, namespace_name):
        ingress_uuid = str(uuid.uuid4())
        ingress_name = 'test-ingress'
        ingress_meta = {'name': ingress_name, 'uid': ingress_uuid,
                        'namespace': namespace_name}
        ingress_spec = {'rules': [{'http': {'paths': [{'path': '/testpath',
                                                       'backend': {
                                                           'serviceName': 'test-service',
                                                           'servicePort': SERVICE_PORT}}]}}]}
        annotations = {'ingress.kubernetes.io/rewrite-target': '/'}
        ingress_add_event = self.create_event('Ingress', ingress_spec,
                                              ingress_meta,
                                              'ADDED')
        ingress_add_event['object']['metadata']['annotations'] = annotations
        IngressKM.locate(ingress_uuid, ingress_add_event['object'])

        self.kube_mock.get_resource.return_value = {'spec': {
            "clusterIP": self.cluster_ip,
        }}

        self.enqueue_event(ingress_add_event)
        return ingress_meta, ingress_uuid

    def _enqueue_delete_service(self, ports, srv_meta):
        srv_spec = {'type': None, 'ports': ports}
        srv_del_event = self.create_event('Service', srv_spec, srv_meta,
                                          'DELETED')
        ServiceKM.delete(srv_meta['uid'])
        self.enqueue_event(srv_del_event)

    def _enqueue_delete_ingress(self, ingress_meta):
        ingress_spec = {'type': None}
        ingress_del_event = self.create_event(
            'Ingress', ingress_spec, ingress_meta, 'DELETED')
        IngressKM.delete(ingress_meta['uid'])
        self.enqueue_event(ingress_del_event)

    def _create_virtual_network(self, project,
                                network='cluster-network'):
        proj_fq_name = ['default-domain', project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj = VirtualNetwork(
            name=network,
            parent_obj=proj_obj,
            address_allocation_mode='user-defined-subnet-only')
        ipam_fq_name = ['default-domain', 'default-project',
                        'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        subnet_data = self._create_subnet_data('10.0.0.0/24')
        vn_obj.add_network_ipam(ipam_obj, subnet_data)
        return self._vnc_lib.virtual_network_create(vn_obj)

    @staticmethod
    def _create_subnet_data(vn_subnet):
        subnets = [vn_subnet] if isinstance(vn_subnet,
                                            basestring) else vn_subnet
        subnet_infos = []
        for subnet in subnets:
            cidr = IPNetwork(subnet)
            subnet_infos.append(
                IpamSubnetType(
                    subnet=SubnetType(
                        str(cidr.network),
                        int(cidr.prefixlen),
                    ),
                    default_gateway=str(IPAddress(cidr.last - 1)),
                    subnet_uuid=str(uuid.uuid4()),
                )
            )
        subnet_data = VnSubnetsType(subnet_infos)
        return subnet_data


    def _create_vrouter_for_vm(self, vmi, vm):
        vrouter_obj = VirtualRouter('phys-host-1' + vmi.uuid)
        self._vnc_lib.virtual_router_create(vrouter_obj)
        vrouter_obj.set_virtual_machine(vm)
        self._vnc_lib.virtual_router_update(vrouter_obj)
        return vrouter_obj

    def _create_vrouters_for_all_service_instances(self, lb_uuid):
        service_instance_refs = self._vnc_lib.loadbalancer_read(id=lb_uuid).service_instance_refs
        self.assertEqual(1, len(service_instance_refs))
        si_uuid = service_instance_refs[0]['uuid']
        si = self._vnc_lib.service_instance_read(id=si_uuid, fields=['virtual_machine_back_refs'])

        vrouter_uuids = []

        for vm_dict in si.virtual_machine_back_refs:
            vm = self._vnc_lib.virtual_machine_read(id=vm_dict['uuid'],
                                                    fields=['virtual_machine_interface_back_refs'])
            vmi = self._vnc_lib.virtual_machine_interface_read(id=vm.virtual_machine_interface_back_refs[0]['uuid'])
            vrouter = self._create_vrouter_for_vm(vmi=vmi, vm=vm)
            vrouter_uuids.append(vrouter.uuid)

        return vrouter_uuids

    def _delete_vrouters(self, uuids):
        for uuid in uuids:
            self._vnc_lib.virtual_router_delete(id=uuid)

    def _delete_virtual_network(self, nw_uuid):
        vn = self._vnc_lib.virtual_network_read(id=nw_uuid, fields=['virtual_machine_interface_back_refs'])

        for vmi in vn.virtual_machine_interface_back_refs:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi['uuid'])

        self._vnc_lib.virtual_network_delete(id=nw_uuid)

    def _delete_public_network(self, pub_net_uuid, pub_fip_pool_uuid):
        self._vnc_lib.floating_ip_pool_delete(id=pub_fip_pool_uuid)
        self._vnc_lib.virtual_network_delete(id=pub_net_uuid)

    def _delete_project(self, project_name):
        project_fq_name = ['default-domain', project_name]

        project = self._vnc_lib.project_read(fq_name=project_fq_name,
                                             fields=['project-security-group',
                                                     'project-service-instance'])

        if project.get_security_groups():
            for sg in project.get_security_groups():
                self._vnc_lib.security_group_delete(id=sg['uuid'])

        if project.get_service_instances():
            for si in project.get_service_instances():
                self._vnc_lib.service_instance_delete(id=si['uuid'])

        self._vnc_lib.project_delete(fq_name=project_fq_name)
