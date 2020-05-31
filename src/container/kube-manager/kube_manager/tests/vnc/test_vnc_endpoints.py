from builtins import str
import uuid

from gevent import monkey
from mock import MagicMock
monkey.patch_all()

from six import string_types
from netaddr import IPNetwork, IPAddress

from vnc_api.gen.resource_client import VirtualNetwork
from vnc_api.gen.resource_xsd import (
    IpamSubnetType, SubnetType, VnSubnetsType, KeyValuePair, KeyValuePairs
)
from kube_manager.common.kube_config_db import NamespaceKM, PodKM, ServiceKM
from kube_manager.vnc.config_db import (VirtualMachineInterfaceKM)
from kube_manager.tests.vnc import test_case
from kube_manager.tests.vnc.db_mock import DBBaseKM
from kube_manager.vnc.vnc_kubernetes import VncKubernetes
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config

TEST_NAMESPACE = 'test-namespace'
TEST_SERVICE_NAME = 'test-service'
TEST_SERVICE_SPEC = {
    'type': 'ClusterIP',
    'ports': [{
        'name': 'http',
        'protocol': 'TCP',
        'port': 80,
    }]
}


class VncEndpointsTestBase(test_case.KMTestCase):

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        super(VncEndpointsTestBase, cls).setUpClass(*args, **kwargs)
        cls.kube_mock = MagicMock()
        VncKubernetes._vnc_kubernetes.endpoints_mgr._kube = cls.kube_mock
        cls.virtual_router = cls.create_virtual_router('endpoints-test-router')

    @classmethod
    def tearDownClass(cls):
        for pod in list(PodKM):
            PodKM.delete(pod)
        for service in list(ServiceKM):
            ServiceKM.delete(service)
        for namespace in list(NamespaceKM):
            NamespaceKM.delete(namespace)

        super(VncEndpointsTestBase, cls).tearDownClass()

    def setUp(self, *args, **kwargs):
        super(VncEndpointsTestBase, self).setUp(*args, **kwargs)
        self._add_namespace(namespace_name=TEST_NAMESPACE)
        self.service_uid = self._add_service(
            namespace=TEST_NAMESPACE,
            srv_name=TEST_SERVICE_NAME,
            srv_spec=TEST_SERVICE_SPEC)
        self.wait_for_all_tasks_done()
        self._check_service()

    def _add_namespace(self, namespace_name, isolated=False):
        ns_uid = str(uuid.uuid4())
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uid)
        if isolated:
            ns_add_event['object']['metadata']['annotations'] = {
                'opencontrail.org/isolation': 'true'}
        NamespaceKM.locate(ns_uid, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        return namespace_name, ns_uid

    def _delete_cluster_network(self, vn_obj):
        return self._vnc_lib.virtual_network_delete(id=vn_obj.get_uuid())

    def _create_cluster_network(self):
        proj_fq_name = vnc_kube_config.cluster_default_project_fq_name()
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vn_obj = VirtualNetwork(
            name='cluster-network',
            parent_obj=proj_obj,
            address_allocation_mode='user-defined-subnet-only')
        ipam_fq_name = ['default-domain', 'default-project',
                        'default-network-ipam']
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        subnet_data = self._create_subnet_data('10.32.0.0/24')
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

    def _add_service(self, namespace, srv_name, srv_spec):
        srv_meta = {
            'name': srv_name,
            'uid': str(uuid.uuid4()),
            'namespace': namespace
        }
        srv_add_event = self.create_event(
            'Service', srv_spec, srv_meta, 'ADDED')
        ServiceKM.locate(srv_meta['uid'], srv_add_event['object'])
        self.enqueue_event(srv_add_event)
        self.kube_mock.get_resource.return_value = {'metadata': srv_meta}
        return srv_meta['uid']

    def _delete_service(self, srv_uid, srv_name, namespace):
        srv_meta = {
            'uid': srv_uid,
            'name': srv_name,
            'namespace': namespace
        }
        srv_delete_event = self.create_event('Service', {}, srv_meta, 'DELETED')
        ServiceKM.delete(srv_uid)
        self.enqueue_event(srv_delete_event)

    def _check_service(self):
        # Assert proper creation of loadbalancer, listener, and pool

        lb = self._vnc_lib.loadbalancer_read(
            id=self.service_uid, fields=('loadbalancer_listener_back_refs',))
        self.assertEqual(len(lb.loadbalancer_listener_back_refs), 1)
        self.listener_uid = lb.loadbalancer_listener_back_refs[0]['uuid']

        lb_listener = self._vnc_lib.loadbalancer_listener_read(
            id=self.listener_uid, fields=('loadbalancer_pool_back_refs',))
        self.assertEqual(len(lb_listener.loadbalancer_pool_back_refs), 1)
        self.pool_uid = lb_listener.loadbalancer_pool_back_refs[0]['uuid']

        lb_pool = self._vnc_lib.loadbalancer_pool_read(
            id=self.pool_uid, fields=('loadbalancer-pool-loadbalancer-member',))
        self.assertIsNone(lb_pool.get_loadbalancer_members())

    def _add_pod(self, pod_name, pod_namespace, pod_status):
        pod_uid = str(uuid.uuid4())
        pod_spec = {'nodeName': 'test-node'}
        pod_meta = {
            'name': pod_name,
            'uid': pod_uid,
            'namespace': pod_namespace,
            'labels': {}
        }
        VncKubernetes._vnc_kubernetes.pod_mgr._kube = MagicMock()
        pod_add_event = self.create_event('Pod', pod_spec, pod_meta, 'ADDED')
        pod_add_event['object']['status'] = pod_status
        PodKM.locate(pod_uid, pod_add_event['object'])
        self.enqueue_event(pod_add_event)
        return pod_uid

    def _add_endpoints(self, name, namespace, pod_uids=(), host_ips=(), iip={}):
        endpoint_uid = str(uuid.uuid4())
        event = self.create_event(
            kind='Endpoints',
            spec={},
            meta={
                'name': name,
                'namespace': namespace,
                'uid': endpoint_uid
            },
            event_type='ADDED'
        )

        if pod_uids:
            addresses = [{
                'targetRef': {
                    'kind': 'Pod',
                    'name': 'test-pod',
                    'namespace': namespace,
                    'uid': pod_uid
                },
                'ip': iip[pod_uid]
            } for pod_uid in pod_uids]
        else:
            addresses = [{
                'ip': ip
            } for ip in host_ips]

        event['object']['subsets'] = [{
            'ports': [{
                'name': 'http',
                'port': 80,
                'protocol': 'TCP'
            }],
            'addresses': addresses
        }]
        self.enqueue_event(event)
        return event['object']

    def _add_pod_to_endpoints(self, endpoints, namespace, pod_uid=None,
                              host_ip=None, iip={}):
        event = {
            'object': endpoints,
            'type': 'MODIFIED'
        }
        if pod_uid is not None:
            event['object']['subsets'][0]['addresses'].append({
                'targetRef': {
                    'kind': 'Pod',
                    'name': 'test-pod',
                    'namespace': namespace,
                    'uid': pod_uid
                },
                'ip': iip[pod_uid]
            })
        else:
            event['object']['subsets'][0]['addresses'].append({
                'ip': host_ip
            })

        self.enqueue_event(event)
        return event['object']

    def _delete_pod_from_endpoints(self, endpoints, pod_uid=None, host_ip=None):
        event = {
            'object': endpoints,
            'type': 'MODIFIED'
        }
        if pod_uid is not None:
            event['object']['subsets'][0]['addresses'] = [
                address for address in endpoints['subsets'][0]['addresses']
                if address['targetRef']['uid'] != pod_uid]
        else:
            event['object']['subsets'][0]['addresses'] = [
                address for address in endpoints['subsets'][0]['addresses']
                if address['ip'] != host_ip]

        self.enqueue_event(event)
        return event['object']

    def _replace_pod_in_endpoints(self, endpoints, old_pod_uid, new_pod_uid, iip={}):
        event = {
            'object': endpoints,
            'type': 'MODIFIED'
        }
        for address in event['object']['subsets'][0]['addresses']:
            if address['targetRef']['uid'] == old_pod_uid:
                address['targetRef']['uid'] = new_pod_uid
                address['ip'] = iip[new_pod_uid]
                break

        self.enqueue_event(event)
        return event['object']

    def _delete_endpoints(self, endpoints):
        self.enqueue_event({
            'object': endpoints,
            'type': 'DELETED'
        })

    def _get_iip(self, vmi_uuid):
        vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)
        vmi = VirtualMachineInterfaceKM.locate(vmi_uuid)
        iip_uuid = list(vmi.instance_ips)[0]
        iip_obj = self._vnc_lib.instance_ip_read(id=iip_uuid)
        iip_ip = iip_obj.get_instance_ip_address()
        return iip_ip

    def _get_vmi_uid(self, pod_uid):
        # Assert proper creation of pod and return id of vm interface
        vm = self._vnc_lib.virtual_machine_read(
            id=pod_uid, fields=('virtual_machine_interface_back_refs',))
        self.assertEqual(len(vm.virtual_machine_interface_back_refs), 1)
        vm_interface = self._vnc_lib.virtual_machine_interface_read(
            id=vm.virtual_machine_interface_back_refs[0]['uuid'])
        return vm_interface.uuid

    def _check_lb_members(self, *members):
        lb_pool = self._vnc_lib.loadbalancer_pool_read(
            id=self.pool_uid, fields=('loadbalancer-pool-loadbalancer-member',))
        lb_members = lb_pool.get_loadbalancer_members() or ()

        self.assertEqual(len(lb_members), len(members))
        lb_members = [
            self._vnc_lib.loadbalancer_member_read(id=member['uuid'])
            for member in lb_members]
        member_annotations = [member.annotations for member in lb_members]

        for vm_uid, vmi_uid in members:
            self.assertIn(
                KeyValuePairs([
                    KeyValuePair('vm', vm_uid),
                    KeyValuePair('vmi', vmi_uid)]),
                member_annotations)


class VncEndpointsTest(VncEndpointsTestBase):

    def test_endpoints_add(self):
        pod_uid = self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()
        vmi_uid = self._get_vmi_uid(pod_uid)
        iip = {}
        iip[pod_uid] = self._get_iip(vmi_uid)

        self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            pod_uids=[pod_uid],
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members((pod_uid, vmi_uid))

    def test_endpoints_modify_pod_added_to_service(self):
        pod1_uid = self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        pod2_uid = self._add_pod(
            pod_name='test-pod2',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.2',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()
        vmi1_uid = self._get_vmi_uid(pod1_uid)
        vmi2_uid = self._get_vmi_uid(pod2_uid)

        iip = {}
        iip[pod1_uid] = self._get_iip(vmi1_uid)
        iip[pod2_uid] = self._get_iip(vmi2_uid)

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            pod_uids=[pod1_uid],
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members((pod1_uid, vmi1_uid))

        self._add_pod_to_endpoints(
            endpoints=endpoints,
            namespace=TEST_NAMESPACE,
            pod_uid=pod2_uid,
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members(
            (pod1_uid, vmi1_uid),
            (pod2_uid, vmi2_uid))

    def test_endpoints_modify_pod_deleted_from_service(self):
        pod1_uid = self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        pod2_uid = self._add_pod(
            pod_name='test-pod2',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.2',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()
        vmi1_uid = self._get_vmi_uid(pod1_uid)
        vmi2_uid = self._get_vmi_uid(pod2_uid)

        iip = {}
        iip[pod1_uid] = self._get_iip(vmi1_uid)
        iip[pod2_uid] = self._get_iip(vmi2_uid)

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            pod_uids=(pod1_uid, pod2_uid),
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members(
            (pod1_uid, vmi1_uid),
            (pod2_uid, vmi2_uid))

        self._delete_pod_from_endpoints(
            endpoints=endpoints,
            pod_uid=pod2_uid)
        self.wait_for_all_tasks_done()

        self._check_lb_members((pod1_uid, vmi1_uid))

    def test_endpoints_modify_pods_added_deleted_from_service(self):
        pod1_uid = self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        pod2_uid = self._add_pod(
            pod_name='test-pod2',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.2',
                'phase': 'created'
            })
        pod3_uid = self._add_pod(
            pod_name='test-pod3',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.3',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()
        vmi1_uid = self._get_vmi_uid(pod1_uid)
        vmi2_uid = self._get_vmi_uid(pod2_uid)
        vmi3_uid = self._get_vmi_uid(pod3_uid)

        iip = {}
        iip[pod1_uid] = self._get_iip(vmi1_uid)
        iip[pod2_uid] = self._get_iip(vmi2_uid)
        iip[pod3_uid] = self._get_iip(vmi3_uid)

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            pod_uids=(pod1_uid, pod2_uid),
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members(
            (pod1_uid, vmi1_uid),
            (pod2_uid, vmi2_uid))

        self._replace_pod_in_endpoints(
            endpoints=endpoints,
            old_pod_uid=pod2_uid,
            new_pod_uid=pod3_uid,
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members(
            (pod1_uid, vmi1_uid),
            (pod3_uid, vmi3_uid))

    def test_endpoints_delete(self):
        pod_uid = self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()
        vmi_uid = self._get_vmi_uid(pod_uid)

        iip = {}
        iip[pod_uid] = self._get_iip(vmi_uid)

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            pod_uids=[pod_uid],
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members((pod_uid, vmi_uid))

        self._delete_endpoints(endpoints)
        self.wait_for_all_tasks_done()

        self._check_lb_members()

    def test_endpoints_add_before_service_add(self):
        pod_uid = self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()
        lb_members = self._vnc_lib.loadbalancer_members_list()

        iip = {}
        iip[pod_uid] = '192.168.0.1'

        self._add_endpoints(
            name='some-unexisting-service',
            namespace=TEST_NAMESPACE,
            pod_uids=[pod_uid],
            iip=iip)
        self.wait_for_all_tasks_done()
        # Assert no new loadbalancer member was created
        self.assertEqual(lb_members, self._vnc_lib.loadbalancer_members_list())

    def test_endpoints_delete_after_service_delete(self):
        pod_uid = self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()
        vmi_uid = self._get_vmi_uid(pod_uid)

        iip = {}
        iip[pod_uid] = self._get_iip(vmi_uid)

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            pod_uids=[pod_uid],
            iip=iip)
        self.wait_for_all_tasks_done()

        self._check_lb_members((pod_uid, vmi_uid))
        self._delete_service(
            srv_uid=self.service_uid,
            srv_name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE)
        self.wait_for_all_tasks_done()
        self._delete_endpoints(endpoints)
        # No assertion here. It should just pass without error.


class VncEndpointsNestedTest(VncEndpointsTestBase):

    @classmethod
    def setUpClass(cls, extra_config_knobs=None, kube_args=None):
        super(VncEndpointsNestedTest, cls).setUpClass(
            extra_config_knobs=extra_config_knobs,
            kube_args=(('DEFAULTS', 'nested_mode', '1'),))

    @classmethod
    def tearDownClass(cls):
        super(VncEndpointsNestedTest, cls).tearDownClass()
        DBBaseKM.set_nested(False)

    def setUp(self, *args, **kwargs):
        super(VncEndpointsNestedTest, self).setUp(*args, **kwargs)
        vn_uuid = self._create_cluster_network()
        self.default_vn = self._vnc_lib.virtual_network_read(id=vn_uuid)

    def tearDown(self, *args, **kwargs):
        for vm in self._vnc_lib.virtual_machines_list()['virtual-machines']:
            self.delete_virtual_machine(vm_id=vm['uuid'])
        self._delete_cluster_network(self.default_vn)
        super(VncEndpointsNestedTest, self).tearDown()

    def test_endpoints_add(self):
        vm, vmi, _ = self.create_virtual_machine(
            'test-vm', self.default_vn, '10.32.0.1')

        self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '10.32.0.1',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()

        self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            host_ips=['10.32.0.1'])
        self.wait_for_all_tasks_done()

        self._check_lb_members((vm.uuid, vmi.uuid))

    def test_endpoints_modify_pod_added_to_service(self):
        vm1, vmi1, _ = self.create_virtual_machine(
            'test-vm1', self.default_vn, '10.32.0.1')
        vm2, vmi2, _ = self.create_virtual_machine(
            'test-vm2', self.default_vn, '10.32.0.2')

        for i in ('1', '2'):
            self._add_pod(
                pod_name='test-pod' + i,
                pod_namespace=TEST_NAMESPACE,
                pod_status={
                    'hostIP': '10.32.0.' + i,
                    'phase': 'created'
                })
        self.wait_for_all_tasks_done()

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            host_ips=['10.32.0.1'])
        self.wait_for_all_tasks_done()

        self._check_lb_members((vm1.uuid, vmi1.uuid))

        self._add_pod_to_endpoints(
            endpoints=endpoints,
            namespace=TEST_NAMESPACE,
            host_ip='10.32.0.2')
        self.wait_for_all_tasks_done()

        self._check_lb_members(
            (vm1.uuid, vmi1.uuid),
            (vm2.uuid, vmi2.uuid))

    def test_endpoints_modify_pod_deleted_from_service(self):
        vm1, vmi1, _ = self.create_virtual_machine(
            'test-vm1', self.default_vn, '10.32.0.1')
        vm2, vmi2, _ = self.create_virtual_machine(
            'test-vm2', self.default_vn, '10.32.0.2')

        for i in ('1', '2'):
            self._add_pod(
                pod_name='test-pod' + i,
                pod_namespace=TEST_NAMESPACE,
                pod_status={
                    'hostIP': '10.32.0.' + i,
                    'phase': 'created'
                })
        self.wait_for_all_tasks_done()

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            host_ips=['10.32.0.1', '10.32.0.2'])
        self.wait_for_all_tasks_done()

        self._check_lb_members(
            (vm1.uuid, vmi1.uuid),
            (vm2.uuid, vmi2.uuid))

        self._delete_pod_from_endpoints(
            endpoints=endpoints,
            host_ip='10.32.0.2')
        self.wait_for_all_tasks_done()

        self._check_lb_members((vm1.uuid, vmi1.uuid))

    def test_endpoints_delete(self):
        vm, vmi, _ = self.create_virtual_machine(
            'test-vm', self.default_vn, '10.32.0.1')

        self._add_pod(
            pod_name='test-pod',
            pod_namespace=TEST_NAMESPACE,
            pod_status={
                'hostIP': '10.32.0.1',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()

        endpoints = self._add_endpoints(
            name=TEST_SERVICE_NAME,
            namespace=TEST_NAMESPACE,
            host_ips=['10.32.0.1'])
        self.wait_for_all_tasks_done()

        self._check_lb_members((vm.uuid, vmi.uuid))

        self._delete_endpoints(endpoints)
        self.wait_for_all_tasks_done()

        self._check_lb_members()


class VncEndpointsTestScaling(VncEndpointsTestBase):
    def test_endpoints_add_scaling(self):
        scale = 50
        endpoint_list = []

        for i in range(scale):
            pod_uid = self._add_pod(
                pod_name='test-pod' + str(i),
                pod_namespace=TEST_NAMESPACE,
                pod_status={
                    'hostIP': '192.168.0.1',
                    'phase': 'created'
                })
            self.wait_for_all_tasks_done()
            vmi_uid = self._get_vmi_uid(pod_uid)

            iip = {}
            iip[pod_uid] = self._get_iip(vmi_uid)

            endpoints = self._add_endpoints(
                name=TEST_SERVICE_NAME,
                namespace=TEST_NAMESPACE,
                pod_uids=[pod_uid],
                iip=iip)
            self.wait_for_all_tasks_done()

            self._check_lb_members((pod_uid, vmi_uid))
            endpoint_list.append(endpoints)

        for i in range(scale):
            self._delete_endpoints(endpoint_list[i])
            self.wait_for_all_tasks_done()

        self._check_lb_members()
