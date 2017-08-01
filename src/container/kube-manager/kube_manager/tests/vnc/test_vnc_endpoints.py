import uuid

from gevent import monkey
from mock import MagicMock, patch
monkey.patch_all()

from kube_manager.common.kube_config_db import NamespaceKM, PodKM
from kube_manager.tests.vnc import test_case
from test_common import launch_kube_manager
from vnc_api.vnc_api import KeyValuePair, KeyValuePairs


class VncEndpointsTest(test_case.KMTestCase):

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        cls.kube_mock = MagicMock()
        super(VncEndpointsTest, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)

    @classmethod
    def launch_kube_manager(cls, *args, **kwargs):
        with patch('kube_manager.vnc.vnc_endpoints.vnc_kube_config.kube',
                   return_value=cls.kube_mock):
            launch_kube_manager(*args, **kwargs)

    @classmethod
    def spawn_kube_manager(cls):
        with patch('test_common.launch_kube_manager',
                   side_effect=cls.launch_kube_manager):
            super(VncEndpointsTest, cls).spawn_kube_manager()

    def _add_namespace(self, namespace_name='test-namespace', isolated=False):
        ns_uid = str(uuid.uuid4())
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uid)
        if isolated:
            ns_add_event['object']['metadata']['annotations'] = {
                'opencontrail.org/isolation': 'true'}
        NamespaceKM.locate(ns_uid, ns_add_event['object'])
        self.enqueue_event(ns_add_event)
        return namespace_name, ns_uid

    def _add_service(
            self, namespace, srv_name='test-service', srv_type='ClusterIP'):
        srv_meta = {
            'name': srv_name,
            'uid': str(uuid.uuid4()),
            'namespace': namespace
        }
        srv_spec = {
            'type': srv_type,
            'ports': [{
                'name': 'http',
                'protocol': 'TCP',
                'port': 80,
            }]
        }
        srv_add_event = self.create_event(
            'Service', srv_spec, srv_meta, 'ADDED')
        self.enqueue_event(srv_add_event)
        return srv_spec, srv_meta

    def _add_pod(self, pod_name, pod_namespace, pod_status):
        pod_uid = str(uuid.uuid4())
        pod_spec = {'nodeName': 'test-node'}
        pod_meta = {
            'name': pod_name,
            'uid': pod_uid,
            'namespace': pod_namespace,
            'labels': {}
        }
        pod_add_event = self.create_event('Pod', pod_spec, pod_meta, 'ADDED')
        pod_add_event['object']['status'] = pod_status
        PodKM.locate(pod_uid, pod_add_event['object'])
        self.enqueue_event(pod_add_event)
        return pod_spec, pod_meta

    def _add_endpoints(self, name, namespace, pod_uid):
        endpoint_uid = str(uuid.uuid4())
        event = self.create_event(
            kind='Endpoints',
            spec={},
            meta={
                'name': name,
                'namespace': namespace,
                'uid': endpoint_uid
            },
            type='ADDED',

        )
        event['object']['subsets'] = [{
            'ports': [{
                'name': 'http',
                'port': 80,
                'protocol': 'TCP'
            }],
            'addresses': [{
                'targetRef': {
                    'kind': 'Pod',
                    'name': 'test-pod',
                    'namespace': namespace,
                    'uid': pod_uid
                }
            }]
        }]
        self.enqueue_event(event)
        return event['object']

    def _add_pod_to_endpoints(self, endpoints, namespace, pod_uid):
        event = {
            'object': endpoints,
            'type': 'MODIFIED'
        }
        event['object']['subsets'][0]['addresses'].append({
            'targetRef': {
                'kind': 'Pod',
                'name': 'test-pod',
                'namespace': namespace,
                'uid': pod_uid
            }
        })
        self.enqueue_event(event)
        return event['object']

    def test_endpoints_add(self):
        # Create namespace, service, and pod
        namespace, _ = self._add_namespace()
        _, srv_meta = self._add_service(namespace)
        _, pod_meta = self._add_pod(
            pod_name='test-pod',
            pod_namespace=namespace,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()

        # Assert proper creation of VM & VMI (pod)
        virtual_machine = self._vnc_lib.virtual_machine_read(
            id=pod_meta['uid'],
            fields=('virtual_machine_interface_back_refs',))
        self.assertEqual(
            len(virtual_machine.virtual_machine_interface_back_refs), 1)
        vm_interface = self._vnc_lib.virtual_machine_interface_read(
            id=virtual_machine.virtual_machine_interface_back_refs[0]['uuid'])

        # Assert proper creation of LB, LBL & LBP (service)
        loadbalancer = self._vnc_lib.loadbalancer_read(
            id=srv_meta['uid'],
            fields=('loadbalancer_listener_back_refs',))
        self.assertEqual(len(loadbalancer.loadbalancer_listener_back_refs), 1)
        loadbalancer_listener = self._vnc_lib.loadbalancer_listener_read(
            id=loadbalancer.loadbalancer_listener_back_refs[0]['uuid'],
            fields=('loadbalancer_pool_back_refs',))
        loadbalancer_pool = self._vnc_lib.loadbalancer_pool_read(
            id=loadbalancer_listener.loadbalancer_pool_back_refs[0]['uuid'],
            fields=('loadbalancer-pool-loadbalancer-member',))
        # Loadbalancer has no members before adding endpoints
        self.assertIsNone(loadbalancer_pool.get_loadbalancer_members())

        # Create endpoints referencing the existing pod
        self.kube_mock.get_resource.return_value = {'metadata': srv_meta}
        self._add_endpoints(
            name=srv_meta['name'],
            namespace=srv_meta['namespace'],
            pod_uid=pod_meta['uid'])
        self.wait_for_all_tasks_done()

        # Assert proper creation of loadbalancer member (endpoints)
        loadbalancer_pool = self._vnc_lib.loadbalancer_pool_read(
            id=loadbalancer_listener.loadbalancer_pool_back_refs[0]['uuid'],
            fields=('loadbalancer-pool-loadbalancer-member',))
        self.assertEqual(len(loadbalancer_pool.get_loadbalancer_members()), 1)
        loadbalancer_member = self._vnc_lib.loadbalancer_member_read(
            id=loadbalancer_pool.loadbalancer_members[0]['uuid'])
        self.assertEqual(loadbalancer_member.annotations, KeyValuePairs([
            KeyValuePair('vm', virtual_machine.uuid),
            KeyValuePair('vmi', vm_interface.uuid)]))

    def test_endpoints_modify_pod_added_to_service(self):
        # Create namespace, service, and pod
        namespace, _ = self._add_namespace()
        _, srv_meta = self._add_service(namespace)
        _, pod1_meta = self._add_pod(
            pod_name='test-pod',
            pod_namespace=namespace,
            pod_status={
                'hostIP': '192.168.0.1',
                'phase': 'created'
            })
        _, pod2_meta = self._add_pod(
            pod_name='test-pod2',
            pod_namespace=namespace,
            pod_status={
                'hostIP': '192.168.0.2',
                'phase': 'created'
            })
        self.wait_for_all_tasks_done()

        # Assert proper creation of VMs & VMIs (pods)
        virtual_machine1 = self._vnc_lib.virtual_machine_read(
            id=pod1_meta['uid'],
            fields=('virtual_machine_interface_back_refs',))
        self.assertEqual(
            len(virtual_machine1.virtual_machine_interface_back_refs), 1)
        vm1_interface = self._vnc_lib.virtual_machine_interface_read(
            id=virtual_machine1.virtual_machine_interface_back_refs[0]['uuid'])

        virtual_machine2 = self._vnc_lib.virtual_machine_read(
            id=pod2_meta['uid'],
            fields=('virtual_machine_interface_back_refs',))
        self.assertEqual(
            len(virtual_machine2.virtual_machine_interface_back_refs), 1)
        vm2_interface = self._vnc_lib.virtual_machine_interface_read(
            id=virtual_machine2.virtual_machine_interface_back_refs[0]['uuid'])

        # Assert proper creation of LB, LBL & LBP (service)
        loadbalancer = self._vnc_lib.loadbalancer_read(
            id=srv_meta['uid'],
            fields=('loadbalancer_listener_back_refs',))
        self.assertEqual(len(loadbalancer.loadbalancer_listener_back_refs), 1)
        loadbalancer_listener = self._vnc_lib.loadbalancer_listener_read(
            id=loadbalancer.loadbalancer_listener_back_refs[0]['uuid'],
            fields=('loadbalancer_pool_back_refs',))
        loadbalancer_pool = self._vnc_lib.loadbalancer_pool_read(
            id=loadbalancer_listener.loadbalancer_pool_back_refs[0]['uuid'],
            fields=('loadbalancer-pool-loadbalancer-member',))
        # Loadbalancer has no members before adding endpoints
        self.assertIsNone(loadbalancer_pool.get_loadbalancer_members())

        # Create endpoints referencing the first pod
        self.kube_mock.get_resource.return_value = {'metadata': srv_meta}
        endpoints = self._add_endpoints(
            name=srv_meta['name'],
            namespace=srv_meta['namespace'],
            pod_uid=pod1_meta['uid'])
        # Modify endpoints by adding the second pod
        self._add_pod_to_endpoints(
            endpoints=endpoints,
            namespace=namespace,
            pod_uid=pod2_meta['uid'])

        self.wait_for_all_tasks_done()

        # Assert proper creation of loadbalancer members
        loadbalancer_pool = self._vnc_lib.loadbalancer_pool_read(
            id=loadbalancer_listener.loadbalancer_pool_back_refs[0]['uuid'],
            fields=('loadbalancer-pool-loadbalancer-member',))
        self.assertEqual(len(loadbalancer_pool.get_loadbalancer_members()), 2)
        loadbalancer_members = [
            self._vnc_lib.loadbalancer_member_read(id=member['uuid'])
            for member in loadbalancer_pool.loadbalancer_members]
        member_annotations = [
            member.annotations for member in loadbalancer_members]
        self.assertIn(
            KeyValuePairs([
                KeyValuePair('vm', virtual_machine1.uuid),
                KeyValuePair('vmi', vm1_interface.uuid)]),
            member_annotations)
        self.assertIn(
            KeyValuePairs([
                KeyValuePair('vm', virtual_machine2.uuid),
                KeyValuePair('vmi', vm2_interface.uuid)]),
            member_annotations)

