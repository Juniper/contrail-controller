#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey

monkey.patch_all()

from collections import namedtuple
from netaddr import IPNetwork, IPAddress
import uuid
from cfgm_common.exceptions import NoIdError
from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.common.kube_config_db import NamespaceKM, ServiceKM
from kube_manager.tests.vnc.test_case import KMTestCase
from vnc_api.gen.resource_client import VirtualNetwork, FloatingIpPool
from vnc_api.gen.resource_xsd import IpamSubnetType, SubnetType, VnSubnetsType

Uuids = namedtuple('Uuids', ['lb_uuid',
                             'vmi_uuid',
                             'vn_uuid',
                             'iip_uuid',
                             'fip_uuid',
                             'lb_listener_uuid',
                             'pub_fip_uuid',
                             'lb_pool_uuid'])


class VncServiceTest(KMTestCase):
    def __init__(self, *args, **kwargs):
        super(VncServiceTest, self).__init__(*args, **kwargs)
        self.namespace = 'guestbook'
        self.external_ip = '10.0.0.54'

    def setUp(self, extra_config_knobs=None):
        super(VncServiceTest, self).setUp()
        self._set_default_kube_config()
        self.pub_net_uuid, self.pub_fip_pool_uuid = \
            self._create_fip_pool_and_public_network()

    def tearDown(self):
        self._delete_public_network(self.pub_net_uuid, self.pub_fip_pool_uuid)
        super(VncServiceTest, self).tearDown()

    def test_add_delete_service_with_default_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network_uuid = self._create_virtual_network(cluster_project)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_service_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_add_delete_service_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)
        network_uuid = self._create_virtual_network(cluster_project)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_service_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid)

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

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

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

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

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

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_loadbalancer_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)
        network_uuid = self._create_virtual_network(cluster_project)

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid)

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

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

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

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network_uuid)
        self._delete_project(cluster_project)

        self._assert_all_is_down(uuids)

    def test_add_delete_kubernetes_service(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta, srv_uuid = self._enqueue_add_kubernetes_service(
            namespace_name)
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid)
        self._assert_link_local_service(ports)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def test_delete_add_service_after_kube_manager_is_killed(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        ports, srv_meta, _ = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self.kill_kube_manager()

        self._enqueue_delete_service(ports, srv_meta)
        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)
        public_fip_pool_config = str({
            'project': 'default',
            'domain': 'default-domain',
            'network': 'public',
            'name': 'public_fip_pool'
        })
        self.spawn_kube_manager(
            extra_args=[('VNC', 'public_fip_pool', public_fip_pool_config)])
        self.wait_for_all_tasks_done()

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        self._assert_all_is_down(uuids, skip_vn=True)

    def _assert_all_is_up(self, ports, srv_uuid, expected_vn_uuid=None):
        # loadbalancer
        lb = self._vnc_lib.loadbalancer_read(
            id=srv_uuid, fields=['loadbalancer_listener_back_refs'])

        # loadbalancer listener
        lb_listeners = lb.loadbalancer_listener_back_refs
        self.assertEqual(1, len(lb_listeners))
        lb_listener_uuid = lb_listeners[0]['uuid']
        lb_listener = self._vnc_lib.loadbalancer_listener_read(
            id=lb_listener_uuid, fields=['loadbalancer_pool_back_refs'])
        self._assert_loadbalancer_listener(lb_listener, ports)

        # loadbalancer pool
        lb_pools = lb_listener.loadbalancer_pool_back_refs
        self.assertEqual(1, len(lb_pools))
        lb_pool_uuid = lb_pools[0]['uuid']
        lb_pool = self._vnc_lib.loadbalancer_pool_read(id=lb_pool_uuid)
        self._assert_loadbalancer_pool(lb_pool, ports)

        # virtual machine interface
        self.assertEqual(1, len(lb.virtual_machine_interface_refs))
        vmi_uuid = lb.virtual_machine_interface_refs[0]['uuid']
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)

        # virtual network
        self.assertEqual(1, len(lb.virtual_network_refs))
        vn_uuid = vmi.virtual_network_refs[0]['uuid']
        vn = self._vnc_lib.virtual_network_read(
            id=vn_uuid, fields=['instance_ip_back_refs'])
        if expected_vn_uuid:
            self.assertEquals(expected_vn_uuid, vn_uuid)

        # instance ip
        iips = vn.instance_ip_back_refs
        self.assertEqual(1, len(iips))
        iip_uuid = iips[0]['uuid']
        iip = self._vnc_lib.instance_ip_read(
            id=iip_uuid, fields=['instance-ip-floating-ip'])

        # floating ip
        fips = iip.get_floating_ips()
        self.assertEqual(1, len(fips))
        fip_uuid = fips[0]['uuid']
        fip = self._vnc_lib.floating_ip_read(id=fip_uuid)

        # public floating ip
        pub_fip_pool = self._vnc_lib.floating_ip_pool_read(
            id=self.pub_fip_pool_uuid, fields=['floating-ip-pool-floating-ip'])
        pub_fips = pub_fip_pool.get_floating_ips()
        self.assertEqual(1, len(pub_fips))
        pub_fip_uuid = pub_fips[0]['uuid']
        pub_fip = self._vnc_lib.floating_ip_read(id=pub_fip_uuid)
        self._assert_pub_fip(pub_fip, vmi_uuid, self.external_ip)

        return Uuids(lb_uuid=lb.uuid,
                     vmi_uuid=vmi.uuid,
                     vn_uuid=vn.uuid,
                     iip_uuid=iip.uuid,
                     fip_uuid=fip.uuid,
                     lb_listener_uuid=lb_listener.uuid,
                     pub_fip_uuid=pub_fip.uuid,
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
                          self._vnc_lib.instance_ip_read, id=uuids.iip_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.floating_ip_pool_read,
                          id=uuids.fip_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.loadbalancer_listener_read,
                          id=uuids.lb_listener_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.loadbalancer_pool_read,
                          id=uuids.lb_pool_uuid)
        self.assertRaises(NoIdError,
                          self._vnc_lib.floating_ip_pool_read,
                          id=uuids.pub_fip_uuid)

    def _create_fip_pool_and_public_network(self):
        net_uuid = self._create_virtual_network(network='public')
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

    def _assert_loadbalancer_pool(self, lb_pool, ports):
        self.assertEquals(
            ports[0]['protocol'],
            lb_pool.loadbalancer_pool_properties.protocol)

    def _assert_loadbalancer_listener(self, lb_listener, ports):
        self.assertEquals(
            ports[0]['protocol'],
            lb_listener.loadbalancer_listener_properties.protocol)
        self.assertEquals(
            ports[0]['port'],
            lb_listener.loadbalancer_listener_properties.protocol_port)

    def _assert_pub_fip(self, fip, vmi_uuid, external_ip):
        self.assertEquals(external_ip, fip.floating_ip_address)
        self.assertEquals(vmi_uuid,
                          fip.virtual_machine_interface_refs[0]['uuid'])

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
        if isolated:
            annotations = {'opencontrail.org/isolation': 'true'}
            ns_add_event['object']['metadata']['annotations'] = annotations
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
        ports = [{'name': 'http', 'protocol': 'TCP', 'port': 80}]
        srv_spec = {'type': srv_type, 'ports': ports,
                    'externalIPs': [self.external_ip]}
        srv_add_event = self.create_event('Service', srv_spec, srv_meta,
                                          'ADDED')
        ServiceKM.locate(srv_uuid, srv_add_event['object'])
        self.enqueue_event(srv_add_event)
        return ports, srv_meta, srv_uuid

    def _enqueue_delete_service(self, ports, srv_meta):
        srv_spec = {'type': None, 'ports': ports}
        srv_del_event = self.create_event('Service', srv_spec, srv_meta,
                                          'DELETED')
        ServiceKM.delete(srv_meta['uid'])
        self.enqueue_event(srv_del_event)

    def _create_virtual_network(self, project='default',
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

    def _delete_virtual_network(self, nw_uuid):
        self._vnc_lib.virtual_network_delete(id=nw_uuid)

    def _delete_public_network(self, pub_net_uuid, pub_fip_pool_uuid):
        self._vnc_lib.floating_ip_pool_delete(id=pub_fip_pool_uuid)
        self._vnc_lib.virtual_network_delete(id=pub_net_uuid)

    def _delete_project(self, project_name):
        self._vnc_lib.project_delete(fq_name=['default-domain', project_name])
