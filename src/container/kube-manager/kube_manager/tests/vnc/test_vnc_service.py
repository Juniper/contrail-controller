#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
from gevent import monkey

monkey.patch_all()

from six import string_types
from collections import namedtuple
from netaddr import IPNetwork, IPAddress
import uuid

from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_client import VirtualNetwork, FloatingIpPool
from vnc_api.gen.resource_xsd import IpamSubnetType, SubnetType, VnSubnetsType

from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.common.kube_config_db import NamespaceKM, ServiceKM
from kube_manager.tests.vnc.test_case import KMTestCase

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
        self.external_cidr = '10.0.0.0/24'

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
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)

        self.create_project(proj_name)
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network_uuid = self._create_virtual_network(proj_name)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self._delete_virtual_network(network_uuid)
        self._delete_project(proj_name)

        self._assert_all_is_down(uuids, skip_vn=False, check_public_fip=True)

    def test_add_delete_service_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)

        self._assert_all_is_down(uuids, skip_vn=True, check_public_fip=True)

    def test_add_delete_service_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)
        self.create_project(proj_name)
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        # wait till isolated service-network get deleted
        vn_fq_name = [
            'default-domain', proj_name,
            self.cluster_name() + '-' + namespace_name + '-service-network']
        self.wait_isolated_service_vn_get_deleted(vn_fq_name)
        self._delete_project(proj_name)

        self._assert_all_is_down(uuids, skip_vn=False, check_public_fip=True)

    def test_add_delete_service_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)

        self._assert_all_is_down(uuids, skip_vn=True, check_public_fip=True)

    def test_add_delete_service_with_custom_isolated_namespace_with_no_cluster_defined(self):
        custom_network = 'custom-service-network'
        project = 'default'
        network_uuid = self._create_virtual_network(project,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(project, custom_network)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self._delete_virtual_network(network_uuid)

        self._assert_all_is_down(uuids, skip_vn=False, check_public_fip=True)

    def test_add_delete_service_with_custom_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        custom_network = 'custom-service-network'
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)
        self.create_project(proj_name)
        network_uuid = self._create_virtual_network(proj_name,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(
                proj_name, custom_network)

        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self._delete_virtual_network(network_uuid)
        self._delete_project(proj_name)

        self._assert_all_is_down(uuids, skip_vn=False, check_public_fip=True)

    def test_add_delete_loadbalancer_with_default_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)
        self.create_project(proj_name)
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network_uuid = self._create_virtual_network(proj_name)

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self._delete_virtual_network(network_uuid)
        self._delete_project(proj_name)

        self._assert_all_is_down(uuids, check_public_fip=True)

    def test_add_delete_loadbalancer_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)

        self._assert_all_is_down(uuids, skip_vn=True, check_public_fip=True)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)
        self.create_project(proj_name)
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        # wait till isolated service-network get deleted
        vn_fq_name = [
            'default-domain', proj_name,
            self.cluster_name() + '-' + namespace_name + '-service-network']
        self.wait_isolated_service_vn_get_deleted(vn_fq_name)
        self._delete_project(proj_name)

        self._assert_all_is_down(uuids, check_public_fip=True)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(
            isolated=True)

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)

        self._assert_all_is_down(uuids, skip_vn=True, check_public_fip=True)

    def test_add_delete_loadbalancer_with_custom_isolated_namespace_with_no_cluster_defined(self):
        custom_network = 'custom-service-network'
        project = 'default'
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(project)
        network_uuid = self._create_virtual_network(proj_name,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(project, custom_network)

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self._delete_virtual_network(network_uuid)

        self._assert_all_is_down(uuids, check_public_fip=True)

    def test_add_delete_loadbalancer_with_custom_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)
        custom_network = 'custom-service-network'
        self.create_project(proj_name)
        network_uuid = self._create_virtual_network(proj_name,
                                                    network=custom_network)
        namespace_name, namespace_uuid = \
            self._enqueue_add_custom_isolated_namespace(cluster_project,
                                                        custom_network)

        ports, srv_meta, srv_uuid = \
            self._enqueue_add_loadbalancer(namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid,
                                       expected_vn_uuid=network_uuid)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self._delete_virtual_network(network_uuid)
        self._delete_project(proj_name)

        self._assert_all_is_down(uuids, check_public_fip=True)

    def test_add_delete_kubernetes_service(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()

        ports, srv_meta, srv_uuid = self._enqueue_add_kubernetes_service(
            namespace_name)

        uuids = self._assert_all_is_up(ports, srv_uuid)
        self._assert_link_local_service(ports)

        self._enqueue_delete_service(ports, srv_meta)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)

        self._assert_all_is_down(uuids, skip_vn=True, check_public_fip=True)

    def test_delete_add_service_after_kube_manager_is_killed(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        ports, srv_meta, _ = self._enqueue_add_service(namespace_name)

        self.kill_kube_manager()

        self._enqueue_delete_service(ports, srv_meta, wait=False)
        ports, srv_meta, srv_uuid = self._enqueue_add_service(namespace_name,
                                                              wait=False)
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name('default')
        public_fip_pool_config = str({
            'project': proj_name,
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

        self._assert_all_is_down(uuids, skip_vn=True, check_public_fip=True)

    def test_service_add_scaling(self):
        ServiceData = namedtuple(
            'ServiceData', ['uuids', 'uuid', 'name', 'ports', 'meta'])
        scale = 50
        cluster_project = self._set_cluster_project()
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)

        self.create_project(proj_name)
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network_uuid = self._create_virtual_network(proj_name)

        srvs_data = []
        first_ip = self.external_ip
        for i in range(scale):
            srv_name = namespace_name + str(i)
            self.external_ip = (IPAddress(first_ip) + i).format()
            ports, srv_meta, srv_uuid = \
                self._enqueue_add_service(namespace_name, srv_name=srv_name)

            uuids = self._assert_all_is_up(ports, srv_uuid,
                                           expected_vn_uuid=network_uuid)
            srvs_data.append(
                ServiceData(uuids, srv_uuid, srv_name, ports, srv_meta))

        for i, srv_data in enumerate(srvs_data):
            self._enqueue_delete_service(srv_data.ports, srv_data.meta)

            self._assert_all_is_down(srv_data.uuids, skip_vn=True, check_public_fip=True)

        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self._delete_virtual_network(network_uuid)
        self._delete_project(proj_name)
        self.external_ip = first_ip

    def _assert_all_is_up(self, ports, srv_uuid, expected_vn_uuid=None):
        """
        This method tests existence of these resources:
        - loadbalancer (lb)
        - loadbalancer pool (lb_pool)
        - loadbalancer listener (lb_listener)
        - virtual network (vn)
        - virtual machine interface (vmi)
        - instance ip (iip)
        - floating ip (fip)

        And links between them:
        lb_pool -> lb_listener -> lb -> vmi -> vn
        fip -> iip -> vn
        """
        # loadbalancer
        lb = self._vnc_lib.loadbalancer_read(
            id=srv_uuid, fields=['loadbalancer_listener_back_refs',
                                 'virtual_machine_interface_refs'])
        # loadbalancer listener -> loadbalancer
        lb_listeners = lb.loadbalancer_listener_back_refs
        # self.assertEqual(1, len(lb_listeners))
        lb_listener_uuid = lb_listeners[0]['uuid']
        lb_listener = self._vnc_lib.loadbalancer_listener_read(
            id=lb_listener_uuid, fields=['loadbalancer_pool_back_refs',
                                         'loadbalancer_listener_properties'])
        pool_ports = self._assert_loadbalancer_listener(lb_listener, ports)

        # loadbalancer pool -> loadbalancer listener
        lb_pools = lb_listener.loadbalancer_pool_back_refs
        self.assertEqual(1, len(lb_pools))
        lb_pool_uuid = lb_pools[0]['uuid']
        lb_pool = self._vnc_lib.loadbalancer_pool_read(id=lb_pool_uuid)
        self._assert_loadbalancer_pool(lb_pool, pool_ports)

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

        # instance ip -> virtual network
        srv_name = lb.name.split('__')[0]
        iips = vn.instance_ip_back_refs
        iip = None
        for _ in vn.instance_ip_back_refs:
            _iip = self._vnc_lib.instance_ip_read(
                id=_['uuid'], fields=['instance-ip-floating-ip'])
            if _iip.name.split('__')[0] == srv_name:
                iip = _iip
                break
        self.assertIsNotNone(iip)

        iip_uuid = iips[len(iips) - 1]['uuid']
        iip = self._vnc_lib.instance_ip_read(
            id=iip_uuid, fields=['instance-ip-floating-ip'])

        # floating ip -> instance ip
        fips = iip.get_floating_ips()
        self.assertEqual(1, len(fips))
        fip_uuid = fips[len(fips) - 1]['uuid']
        fip = self._vnc_lib.floating_ip_read(id=fip_uuid)
        self.assertIsNotNone(fip.floating_ip_address)

        # public floating ip
        pub_fip_pool = self._vnc_lib.floating_ip_pool_read(
            id=self.pub_fip_pool_uuid, fields=['floating-ip-pool-floating-ip'])
        pub_fips = pub_fip_pool.get_floating_ips()
        # self.assertEqual(1, len(pub_fips))
        if pub_fips is None:
            self.pub_fip_obj_uuid = None
        else:
            pub_fip_objs = \
                [self._vnc_lib.floating_ip_read(id=pub_fips[i]['uuid'])
                 for i in range(len(pub_fips))]
            pub_fip_ips = [ip.floating_ip_address for ip in pub_fip_objs]
            idx = pub_fip_ips.index(self.external_ip)
            self.pub_fip_obj_uuid = pub_fip_objs[idx].uuid
            self.assertTrue(idx >= 0)

        return Uuids(lb_uuid=lb.uuid,
                     vmi_uuid=vmi.uuid,
                     vn_uuid=vn.uuid,
                     iip_uuid=iip.uuid,
                     fip_uuid=fip.uuid,
                     lb_listener_uuid=lb_listener.uuid,
                     pub_fip_uuid=self.pub_fip_obj_uuid,
                     lb_pool_uuid=lb_pool.uuid)

    def _assert_all_is_down(self, uuids, skip_vn=False, check_public_fip=False):
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
        if check_public_fip:
            self.assertRaises(NoIdError,
                              self._vnc_lib.floating_ip_pool_read,
                              id=uuids.pub_fip_uuid)
        else:
            self.assertIsNone(uuids.pub_fip_uuid)

    def _create_fip_pool_and_public_network(self):
        net_uuid = self._create_virtual_network(network='public')
        net_obj = self._vnc_lib.virtual_network_read(id=net_uuid)
        fip_pool_obj = FloatingIpPool('public_fip_pool', parent_obj=net_obj)
        fip_pool_uuid = self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name('default')

        kube_config.VncKubernetesConfig.args().public_fip_pool = str({
            'project': proj_name,
            'domain': 'default-domain',
            'network': 'public',
            'name': 'public_fip_pool'
        })
        return net_uuid, fip_pool_uuid

    def _assert_link_local_service(self, ports):
        proj_obj = self._vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config',
                     'default-global-vrouter-config'])
        _ = ports[0]['port']
        ll_entries = proj_obj.linklocal_services.linklocal_service_entry
        ll_port_numbers = {ll_entries[i].linklocal_service_port
                           for i in range(len(ll_entries))}
        port_numbers = {ports[i]['port'] for i in range(len(ports))}
        self.assertTrue(ll_port_numbers == port_numbers)

    def _assert_loadbalancer_pool(self, lb_pool, ports):
        self.assertEquals(
            ports[0]['protocol'],
            lb_pool.loadbalancer_pool_properties.protocol)

    def _assert_loadbalancer_listener(self, lb_listener, ports):
        matching_ports = [
            ports[i] for i in range(len(ports))
            if (ports[i]['port'] == lb_listener.loadbalancer_listener_properties.protocol_port and
                ports[i]['protocol'] == lb_listener.loadbalancer_listener_properties.protocol)
        ]
        self.assertEquals(1, len(matching_ports))
        return matching_ports

    @staticmethod
    def _set_cluster_project():
        cluster_project = 'cluster_project'
        proj_name = kube_config.VncKubernetesConfig.cluster_project_name(cluster_project)
        kube_config.VncKubernetesConfig.args().cluster_project = str({'project': proj_name})
        return cluster_project

    @staticmethod
    def _set_default_kube_config():
        kube_config.VncKubernetesConfig.args().cluster_project = '{}'
        kube_config.VncKubernetesConfig.args().cluster_service_network = None

    @staticmethod
    def _set_cluster_service_network(self, service_cn_dict):
        kube_config.VncKubernetesConfig.args(). \
            cluster_service_network = repr(service_cn_dict)

    def _enqueue_add_namespace(self, isolated=False):
        ns_uuid = str(uuid.uuid4())
        namespace_name = 'namespace_name'
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uuid)
        if isolated:
            annotations = {'opencontrail.org/isolation': 'true'}
            ns_add_event['object']['metadata']['annotations'] = annotations
        NamespaceKM.locate(ns_uuid, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        self.wait_for_all_tasks_done()
        return namespace_name, ns_uuid

    def _enqueue_add_custom_isolated_namespace(self, project, network):
        custom_network_config = {'virtual_network': network,
                                 'domain': 'default-domain',
                                 'project': project,
                                 'name': network}
        kube_config.VncKubernetesConfig.args().cluster_service_network = str(
            custom_network_config)
        ns_uuid = str(uuid.uuid4())
        namespace_name = 'custom_isolated_namespace'
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uuid)
        annotations = {
            'opencontrail.org/network': str({
                'domain': 'default-domain',
                'project': project,
                'name': network})}
        ns_add_event['object']['metadata']['annotations'] = annotations
        NamespaceKM.locate(ns_uuid, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        self.wait_for_all_tasks_done()
        return namespace_name, ns_uuid

    def _enqueue_delete_namespace(self, namespace_name, ns_uuid):
        ns_delete_event = self.create_delete_namespace_event(namespace_name,
                                                             ns_uuid)
        NamespaceKM.delete(ns_uuid)
        self.enqueue_event(ns_delete_event)
        self.wait_for_all_tasks_done()

    def _enqueue_add_kubernetes_service(self, namespace_name):
        return self._enqueue_add_service(namespace_name, srv_name='kubernetes')

    def _enqueue_add_loadbalancer(self, namespace_name):
        return self._enqueue_add_service(namespace_name,
                                         srv_type='LoadBalancer')

    def _enqueue_add_service(self,
                             namespace_name,
                             srv_name='test-service',
                             srv_type='ClusterIP', wait=True):
        srv_uuid = str(uuid.uuid4())
        srv_meta = {'name': srv_name, 'uid': srv_uuid,
                    'namespace': namespace_name}
        if srv_name == 'kubernetes':
            ports = [
                {'name': 'https', 'protocol': 'TCP', 'port': 443},
                {'name': 'dns', 'protocol': 'UDP', 'port': 53},
                {'name': 'dns-tcp', 'protocol': 'TCP', 'port': 53}
            ]
        else:
            ports = [{'name': 'http', 'protocol': 'TCP', 'port': 80}]
        srv_spec = {'type': srv_type, 'ports': ports,
                    'externalIPs': [self.external_ip]}
        srv_add_event = self.create_event('Service', srv_spec, srv_meta,
                                          'ADDED')
        ServiceKM.locate(srv_uuid, srv_add_event['object'])
        self.enqueue_event(srv_add_event)
        if wait:
            self.wait_for_all_tasks_done()
        return ports, srv_meta, srv_uuid

    def _enqueue_delete_service(self, ports, srv_meta, wait=True):
        srv_spec = {'type': None, 'ports': ports}
        srv_del_event = self.create_event('Service', srv_spec, srv_meta,
                                          'DELETED')
        ServiceKM.delete(srv_meta['uid'])
        self.enqueue_event(srv_del_event)
        if wait:
            self.wait_for_all_tasks_done()

    def _create_virtual_network(self, project='default',
                                network='cluster-default-service-network'):
        if project == 'default':
            project = kube_config.VncKubernetesConfig.cluster_project_name('default')
        proj_fq_name = ['default-domain', project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj = VirtualNetwork(
            name=network,
            parent_obj=proj_obj,
            address_allocation_mode='user-defined-subnet-only')
        ipam_fq_name = ['default-domain', 'default-project',
                        'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        subnet_data = self._create_subnet_data(self.external_cidr)
        vn_obj.add_network_ipam(ipam_obj, subnet_data)
        return self._vnc_lib.virtual_network_create(vn_obj)

    @staticmethod
    def _create_subnet_data(vn_subnet):
        subnets = [vn_subnet] if isinstance(vn_subnet, string_types) else vn_subnet
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

    def wait_isolated_service_vn_get_deleted(self, vn_fq_name):
        while True:
            try:
                self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
                self.wait_for_all_tasks_done(sleep_interval=5)
            except NoIdError:
                break

    def _delete_virtual_network(self, nw_uuid):
        self._vnc_lib.virtual_network_delete(id=nw_uuid)
        while True:
            try:
                self._vnc_lib.virtual_network_read(id=nw_uuid)
                self.wait_for_all_tasks_done(sleep_interval=5)
            except NoIdError:
                break

    def _delete_public_network(self, pub_net_uuid, pub_fip_pool_uuid):
        self._vnc_lib.floating_ip_pool_delete(id=pub_fip_pool_uuid)
        self._vnc_lib.virtual_network_delete(id=pub_net_uuid)

    def _delete_project(self, project_name):
        self._vnc_lib.project_delete(fq_name=['default-domain', project_name])
