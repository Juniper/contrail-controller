#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from netaddr import IPNetwork, IPAddress
import uuid
from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.vnc.config_db import LoadbalancerListenerKM
from kube_manager.vnc.vnc_service import LoadbalancerKM
from test_case import KMTestCase
from vnc_api.gen.resource_client import VirtualNetwork
from vnc_api.gen.resource_xsd import IpamSubnetType, SubnetType, VnSubnetsType


class VncServiceTest(KMTestCase):

    def setUp(self, extra_config_knobs=None):
        super(VncServiceTest, self).setUp()
        self.namespace = 'guestbook'
        self._set_default_kube_config()
    #end setUp

    def tearDown(self):
        super(VncServiceTest, self).tearDown()
    #end tearDown

    def test_add_delete_service_with_default_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network = self._create_virtual_network(cluster_project)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_service_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_service_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace(isolated=True)
        network = self._create_virtual_network(cluster_project)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_service_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(isolated=True)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_service_with_custom_isolated_namespace_with_no_cluster_defined(self):
        custom_network = 'custom_network'
        project = 'default'
        network = self._create_virtual_network(project, network=custom_network)
        namespace_name, namespace_uuid = self._enqueue_add_custom_isolated_namespace(project, custom_network)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_service_with_custom_isolated_namespace_with_cluster_defined(self):
        project = self._set_cluster_project()
        custom_network = 'custom_network'
        cluster_network_config = {'virtual_network': 'custom_network',
                                  'domain': 'default-domain',
                                  'project': project,
                                  'name': custom_network}
        kube_config.VncKubernetesConfig.args().cluster_network = str(cluster_network_config)

        self.create_project(project)

        network = self._create_virtual_network(project, network=custom_network)
        namespace_name, namespace_uuid = self._enqueue_add_custom_isolated_namespace(project, custom_network)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)
        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_loadbalancer_with_default_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        network = self._create_virtual_network(cluster_project)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_loadbalancer_with_default_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_cluster_defined(self):
        cluster_project = self._set_cluster_project()
        namespace_name, namespace_uuid = self._enqueue_add_namespace(isolated=True)
        network = self._create_virtual_network(cluster_project)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_loadbalancer_with_isolated_namespace_with_no_cluster_defined(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace(isolated=True)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_loadbalancer_with_custom_isolated_namespace_with_no_cluster_defined(self):
        custom_network = 'custom_network'
        project = 'default'
        network = self._create_virtual_network(project, network=custom_network)
        namespace_name, namespace_uuid = self._enqueue_add_custom_isolated_namespace(project, custom_network)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_loadbalancer_with_custom_isolated_namespace_with_cluster_defined(self):
        project = self._set_cluster_project()
        custom_network = 'custom_network'
        cluster_network_config = {'virtual_network': 'custom_network',
                                  'domain': 'default-domain',
                                  'project': project,
                                  'name': custom_network}
        kube_config.VncKubernetesConfig.args().cluster_network = str(cluster_network_config)

        self.create_project(project)

        network = self._create_virtual_network(project, network=custom_network)
        namespace_name, namespace_uuid = self._enqueue_add_custom_isolated_namespace(project, custom_network)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_loadbalancer(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()
        self._delete_virtual_network(network)
        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_add_delete_kubernetes_service(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_kubernetes_service(namespace_name)
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)
        self._assert_link_local_service(ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def test_delete_add_service_after_kube_manager_is_killed(self):
        namespace_name, namespace_uuid = self._enqueue_add_namespace()
        ports, srv_meta, srv_type, _ = self._enqueue_add_service(namespace_name)
        self.wait_for_all_tasks_done()

        self.kill_kube_manager()

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        ports, srv_meta, srv_type, srv_uuid = self._enqueue_add_service(namespace_name)

        self.spawn_kube_manager()
        self.wait_for_all_tasks_done()

        self._assert_loadbalancer(srv_uuid, ports)

        self._enqueue_delete_service(ports, srv_meta, srv_type)
        self._enqueue_delete_namespace(namespace_name, namespace_uuid)
        self.wait_for_all_tasks_done()

        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNone(lb)

    def _assert_link_local_service(self, ports):
        proj_obj = self._vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config', 'default-global-vrouter-config'])
        expected_port = ports[0]['port']
        linklocal_service_port = \
            proj_obj._linklocal_services.linklocal_service_entry[0].linklocal_service_port
        self.assertEquals(expected_port, linklocal_service_port)

    def _assert_loadbalancer(self, srv_uuid, ports):
        lb = LoadbalancerKM.locate(srv_uuid)
        self.assertIsNotNone(lb)
        ll = LoadbalancerListenerKM.locate(list(lb.loadbalancer_listeners)[0])
        self.assertIsNotNone(ll)
        self.assertEquals(ports[0]['port'], ll.params['protocol_port'])

    def _set_cluster_project(self):
        cluster_project = 'cluster_project'
        kube_config.VncKubernetesConfig.args().cluster_project = "{'project':'" + cluster_project + "'}"
        return cluster_project

    def _set_default_kube_config(self):
        kube_config.VncKubernetesConfig.args().cluster_project = '{}'
        kube_config.VncKubernetesConfig.args().cluster_network = None

    def _enqueue_add_namespace(self, isolated=False):
        ns_uuid = str(uuid.uuid4())
        namespace_name = 'namespace_name'
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uuid)
        if isolated:
            annotations = {'opencontrail.org/isolation': 'true'}
            ns_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns_add_event)
        return namespace_name, ns_uuid

    def _enqueue_add_custom_isolated_namespace(self, project, network):
        annotations = {
            "opencontrail.org/network":
                '{"domain":"default-domain", "project": "' + project + '", "name":"' + network + '"}'
        }
        ns_uuid = str(uuid.uuid4())
        namespace_name = 'custom_isolated_namespace'
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uuid)
        ns_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns_add_event)
        return namespace_name, ns_uuid

    def _enqueue_delete_namespace(self, namespace_name, ns_uuid):
        ns_delete_event = self.create_delete_namespace_event(namespace_name, ns_uuid)
        self.enqueue_event(ns_delete_event)

    def _enqueue_add_kubernetes_service(self, namespace_name):
        return self._enqueue_add_service(namespace_name, srv_name='kubernetes')

    def _enqueue_add_loadbalancer(self, namespace_name):
        return self._enqueue_add_service(namespace_name, srv_type='LoadBalancer',
                                         additional_spec={'externalIPs': ['127.0.0.1']})

    def _enqueue_add_service(self,
                             namespace_name,
                             srv_name='test-service',
                             srv_type='ClusterIP',
                             additional_spec=None):
        srv_uuid = str(uuid.uuid4())
        srv_meta = {'name': srv_name, 'uid': srv_uuid, 'namespace': namespace_name}
        ports = [{'name': 'http', 'protocol': 'TCP', 'port': 80, 'targetPort': 9376}]
        srv_spec = dict({'type': srv_type, 'ports': ports}, **additional_spec or {})
        srv_add_event = self.create_event('Service', srv_spec, srv_meta, 'ADDED')
        self.enqueue_event(srv_add_event)
        return ports, srv_meta, srv_type, srv_uuid

    def _enqueue_delete_service(self, ports, srv_meta, srv_type):
        srv_spec = {'type': srv_type, 'ports': ports}
        srv_del_event = self.create_event('Service', srv_spec, srv_meta, 'DELETED')
        self.enqueue_event(srv_del_event)

    def _create_virtual_network(self, cluster_project, network='cluster-network'):
        proj_fq_name = ['default-domain', cluster_project]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        self.assertEquals(cluster_project, proj_obj.name)
        vn_obj = VirtualNetwork(name=network, parent_obj=proj_obj,
                                address_allocation_mode='user-defined-subnet-only')
        ipam_fq_name = ['default-domain', 'default-project', 'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        subnet_data = self._create_subnet_data('10.0.0.0/24')
        vn_obj.add_network_ipam(ipam_obj, subnet_data)
        return self._vnc_lib.virtual_network_create(vn_obj)

    def _create_subnet_data(self, vn_subnet):
        subnets = [vn_subnet] if isinstance(vn_subnet, basestring) else vn_subnet
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
