import copy
import json
import mock
import unittest
import uuid

from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from svc_monitor import instance_manager
from svc_monitor import snat_agent
from vnc_api.vnc_api import *


ROUTER_1 = {
    'fq_name': ['default-domain', 'demo', 'router1'],
    'parent_uuid': 'a8d55cfc-c66a-4eeb-82f8-d144fe74c46b',
    'uuid': '8e9b4859-d4c2-4ed5-9468-4809b1a926f3',
    'parent_type': 'project',
    'virtual_machine_interface_refs': [
        {'attr': None,
         'to': ['default-domain',
                'demo',
                'ccc56e1c-a749-4147-9f2f-a04b32bd658d'],
         'uuid': 'ccc56e1c-a749-4147-9f2f-a04b32bd658d'}],
    'virtual_network_refs': [
        {'attr': None,
         'to': ['default-domain', 'demo', 'public'],
         'uuid': '4f60e029-6029-4039-ad3a-4e4eb0e89407'}]
}


class RouteTableMatcher(object):

    def __init__(self, prefix):
        self._prefix = prefix

    def __eq__(self, rt_obj):
        prefix = rt_obj.get_routes().route[0].prefix
        return self._prefix == prefix


class VirtualNetworkMatcher(object):

    def __init__(self, name, user_visible):
        self._name = name
        self._user_visible = user_visible

    def __eq__(self, net_obj):
        user_visible = net_obj.get_id_perms().get_user_visible()
        return (net_obj.get_fq_name_str().startswith(self._name) and
                user_visible == self._user_visible)

class ServiceInstanceMatcher(object):

    def __init__(self, name, left, right, mode):
        self._name = name
        self._left = left
        self._right = right
        self._mode = mode

    def __eq__(self, si_obj):
        si_props = si_obj.get_service_instance_properties()
        mode = si_props.get_ha_mode()
        right = si_props.get_interface_list()[0].get_virtual_network()
        left = si_props.get_interface_list()[1].get_virtual_network()

        return (si_obj.fq_name[-1].startswith(self._name) and
                mode == self._mode and
                left.startswith(self._left) and
                right == self._right)


class LogicalRouterMatcher(object):

    def __init__(self, si_name=None, rt_name=None):
        self._si_name = si_name
        self._rt_name = rt_name

    def __eq__(self, rtr_obj):
        if self._si_name:
            return rtr_obj.get_service_instance_refs()[0]['to'][-1].startswith(
                self._si_name)
        if self._rt_name:
            return (rtr_obj.get_route_table_refs()[0]['to'][-1] ==
                    self._rt_name)
        if self._si_name == '':
            return not rtr_obj.get_service_instance_refs()
        if self._rt_name == '':
            return not rtr_obj.get_route_table_refs()


class SnatAgentTest(unittest.TestCase):

    def setUp(self):
        self.vnc_lib = mock.Mock()

        self.cassandra = mock.Mock()
        self.logger = mock.Mock()

        self.svc = mock.Mock()
        self.svc.netns_manager = instance_manager.NetworkNamespaceManager(
            self.vnc_lib, self.cassandra, self.logger, None, None, None)

        self.snat_agent = snat_agent.SNATAgent(self.svc, self.vnc_lib,
                                               self.cassandra, None)
        DBBase.init(self, self.logger, self.cassandra)

        # register the project
        proj_fq_name = ['default-domain', 'demo']
        config_db.ProjectSM.locate(
            ROUTER_1['parent_uuid'],
            {'fq_name': proj_fq_name})

        project = Project(name=proj_fq_name[-1])
        self.vnc_lib.project_read = mock.Mock(
            return_value=project)

        # register the public network
        config_db.VirtualNetworkSM.locate(
            ROUTER_1['virtual_network_refs'][0]['uuid'],
            {'fq_name': ROUTER_1['virtual_network_refs'][0]['to'], 'parent_type': 'project'})

        # register interfaces
        config_db.VirtualMachineInterfaceSM.locate(
            ROUTER_1['virtual_machine_interface_refs'][0]['uuid'],
            {'fq_name': ROUTER_1['virtual_machine_interface_refs'][0]['to'],
             'virtual_network_refs': [{'uuid': 'private1-uuid'}], 'parent_type': 'project'})

    def tearDown(self):
        config_db.LogicalRouterSM.reset()
        config_db.VirtualNetworkSM.reset()
        config_db.VirtualMachineInterfaceSM.reset()
        self.cassandra.reset_mock()

    def obj_to_dict(self, obj):
        def to_json(obj):
            if hasattr(obj, 'serialize_to_json'):
                return obj.serialize_to_json(obj.get_pending_updates())
            else:
                return dict((k, v) for k, v in obj.__dict__.iteritems())

        return json.loads(json.dumps(obj, default=to_json))

    def test_upgrade(self):
        #create lr
        router_obj = LogicalRouter('router1')
        router_obj.fq_name = ROUTER_1['fq_name']
        router_obj.uuid = ROUTER_1['uuid']
        router_obj.parent_type = 'project'
        self.vnc_lib.logical_router_read = mock.Mock(return_value=router_obj)
        #create rt
        rt_name = 'rt1_' + ROUTER_1['uuid']
        rt_uuid = 'rt1-a04b32bd658d'
        rt_fq_name = ['default-domain', 'demo', 'rt1_' + ROUTER_1['uuid']]
        rt_obj = RouteTable(rt_name)
        rt_obj.fq_name = rt_fq_name
        rt_obj.uuid = rt_uuid
        rt_obj.parent_type = 'project'
        self.vnc_lib.route_table_read = mock.Mock(return_value=rt_obj)

        def db_read_side_effect(obj_type, uuids):
            if obj_type != 'virtual_network' and obj_type != 'route_table':
                return (False, None)

            if obj_type == 'route_table':
                return (True, [{'fq_name': rt_fq_name,
                                'parent_type': 'project',
                                'uuid': rt_uuid,
                                'virtual_network_back_refs': [{
                                    'to': ['default-domain', 'demo', 'private1-name'],
                                    'uuid': 'private1-uuid'
                                 }]
                               }])
            if 'private1-uuid' in uuids:
                return (True, [{'fq_name': ['default-domain',
                                            'demo',
                                            'private1-name'],
                                'uuid': 'private1-uuid',
                                'route_table_refs': [ {
                                    'to': rt_fq_name,
                                    'uuid': rt_uuid
                                                      }
                                                    ]
                                }])
            return (False, None)

        self.cassandra.object_read = mock.Mock(
            side_effect=db_read_side_effect)

        self.vnc_lib.virtual_network_create = mock.Mock()

        def no_id_side_effect(type, fq_name):
            if type == 'network-ipam':
                return 'fake-uuid'
            if type == 'route_table':
                return rt_uuid
            raise NoIdError("xxx")

        self.cassandra.fq_name_to_uuid = mock.Mock(
            side_effect=no_id_side_effect)

        self.vnc_lib.virtual_network_update = mock.Mock()
        self.vnc_lib.logical_router_update = mock.Mock()
        self.vnc_lib.route_table_update = mock.Mock()
        self.vnc_lib.ref_update = mock.Mock()

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'], ROUTER_1)
        rt_obj = self.vnc_lib.route_table_read(id=rt_uuid)

        self.snat_agent.upgrade(router)

        # check that the route table link deleted from network
        self.vnc_lib.ref_update.assert_any_call(
            'virtual-network', 'private1-uuid', 'route-table',
            None, [u'default-domain', u'demo', rt_name], 'DELETE', None)

        # check that the route table link added to logical router
        self.vnc_lib.ref_update.assert_any_call(
            'logical-router', ROUTER_1['uuid'], 'route-table',
            None, [u'default-domain', u'demo', rt_name], 'ADD', None)


    def test_gateway_set(self):
        router_obj = LogicalRouter('rtr1-name')
        self.vnc_lib.logical_router_read = mock.Mock(return_value=router_obj)

        # will return the private virtual network, and will return
        # an error when trying to read the service snat VN
        def db_read_side_effect(obj_type, uuids):
            if obj_type != 'virtual_network':
                return (False, None)
            if 'private1-uuid' in uuids:
                return (True, [{'fq_name': ['default-domain',
                                            'demo',
                                            'private1-name'],
                                'uuid': 'private1-uuid'}])
            elif 'snat-vn-uuid' in uuids:
                return (True, [{'fq_name':
                                ['default-domain',
                                 'demo',
                                 'snat-si-left_si_' + ROUTER_1['uuid']],
                                'uuid': 'snat-vn-uuid'}])
            return (False, None)

        self.cassandra.object_read = mock.Mock(
            side_effect=db_read_side_effect)

        def vn_create_side_effect(vn_obj):
            if vn_obj.name == ('snat-si-left_si_' + ROUTER_1['uuid']):
                vn_obj.uuid = 'snat-vn-uuid'

        self.vnc_lib.virtual_network_create = mock.Mock(
            side_effect=vn_create_side_effect)

        self.vnc_lib.route_table_read = mock.Mock(return_value=None)

        def no_id_side_effect(type, fq_name):
            if type == 'network-ipam':
                return 'fake-uuid'

            raise NoIdError("xxx")

        self.cassandra.fq_name_to_uuid = mock.Mock(
            side_effect=no_id_side_effect)

        self.vnc_lib.route_table_create = mock.Mock()
        self.vnc_lib.virtual_network_update = mock.Mock()
        self.vnc_lib.logical_router_update = mock.Mock()

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'],
                                                  ROUTER_1)
        self.snat_agent.update_snat_instance(router)

        # check that the snat service network is created
        left = ('default-domain:demo:snat-si-left_snat_' +
                ROUTER_1['uuid'])
        self.vnc_lib.virtual_network_create.assert_called_with(
            VirtualNetworkMatcher(left, False))

        # route table that is going to be set for each interface
        self.vnc_lib.route_table_create.assert_called_with(
            RouteTableMatcher('0.0.0.0/0'))

        # check that the route table is applied to the network
        rt_name = 'rt_' + ROUTER_1['uuid']
        self.vnc_lib.logical_router_update.assert_called_with(
            LogicalRouterMatcher(rt_name=rt_name))

        # check that the SI is correctly set with the right interfaces
        right = ':'.join(ROUTER_1['virtual_network_refs'][0]['to'])
        self.vnc_lib.service_instance_create.assert_called_with(
            ServiceInstanceMatcher('snat_' + ROUTER_1['uuid'],
                                   left, right, 'active-standby'))

        # check that the SI created is set to the logical router
        self.vnc_lib.logical_router_update.assert_called_with(
            LogicalRouterMatcher(si_name='snat_' + ROUTER_1['uuid']))

    def _test_snat_delete(self, router_dict):
        self.test_gateway_set()

        # reset all calls
        self.vnc_lib.logical_router_update.reset_mock()
        self.vnc_lib.service_instance_read.reset_mock()

        # now we have a route table
        def no_id_side_effect(type, fq_name):
            if type == 'route_table':
                return 'fake-uuid'

            raise NoIdError("xxx")

        self.cassandra.fq_name_to_uuid = mock.Mock(
            side_effect=no_id_side_effect)

        si_obj = ServiceInstance('si_' + ROUTER_1['uuid'])
        si_obj.set_uuid('si-uuid')
        self.vnc_lib.service_instance_read.return_value = si_obj

        # get and use the route table previously created
        rt_obj = self.vnc_lib.route_table_create.mock_calls[0][1][0]
        rt_dict = self.obj_to_dict(rt_obj)
        rt_dict['logical_router_back_refs'] = [{'uuid': '8e9b4859-d4c2-4ed5-9468-4809b1a926f3'}]

        def db_read_side_effect(obj_type, uuids):
            if obj_type == 'route_table':
                return (True, [rt_dict])
            if 'private1-uuid' in uuids:
                return (True, [{'fq_name': ['default-domain',
                                            'demo',
                                            'private1-name'],
                                'uuid': 'private1-uuid'}])
            return (False, None)

        self.cassandra.object_read = mock.Mock(
            side_effect=db_read_side_effect)

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'])
        router.update(router_dict)
        self.snat_agent.update_snat_instance(router)

        self.vnc_lib.service_instance_read.assert_called_with(
            id='si-uuid')

        # check that the route table is removed from the networks
        self.vnc_lib.logical_router_update.assert_called_with(
            LogicalRouterMatcher(rt_name=''))

        # check that the route table is deleted
        self.vnc_lib.route_table_delete.assert_called_with(id=rt_obj.uuid)

        # check that the SI is removed from the logical router
        self.vnc_lib.logical_router_update.assert_called_with(
            LogicalRouterMatcher(si_name=''))

        # check that the SI is beeing to be deleted
        self.vnc_lib.service_instance_delete.assert_called_with(
            id=si_obj.get_uuid())

    # Test cleanup of snat SI created without internat interfaces
    def test_snat_delete_without_rt_net_back_refs(self):
        self.test_gateway_set()

        # reset all calls
        self.vnc_lib.logical_router_update.reset_mock()
        self.vnc_lib.service_instance_read.reset_mock()

        # now we have a route table
        def no_id_side_effect(type, fq_name):
            if type == 'route_table':
                return 'fake-uuid'

            raise NoIdError("xxx")

        self.cassandra.fq_name_to_uuid = mock.Mock(
            side_effect=no_id_side_effect)

        si_obj = ServiceInstance('si_' + ROUTER_1['uuid'])
        si_obj.set_uuid('si-uuid')
        self.vnc_lib.service_instance_read.return_value = si_obj

        # get and use the route table previously created, but without
        # any back_refs to virtual networks
        rt_obj = self.vnc_lib.route_table_create.mock_calls[0][1][0]
        rt_dict = self.obj_to_dict(rt_obj)

        def db_read_side_effect(obj_type, uuids):
            if obj_type == 'route_table':
                return (True, [rt_dict])
            if 'private2-uuid' in uuids:
                return (True, [{'fq_name': ['default-domain',
                                            'demo',
                                            'private2-name'],
                                'uuid': 'private2-uuid'}])
            return (False, None)

        self.cassandra.object_read = mock.Mock(
            side_effect=db_read_side_effect)

        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_machine_interface_refs'] = []
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'demo',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'])
        router.update(router_dict)
        self.snat_agent.update_snat_instance(router)

        self.vnc_lib.service_instance_read.assert_called_with(
            id='si-uuid')

        # check that the route table is deleted
        self.vnc_lib.route_table_delete.assert_called_with(id=rt_obj.uuid)

        # check that the SI is removed from the logical router
        self.vnc_lib.logical_router_update.assert_called_with(
            LogicalRouterMatcher(si_name=''))

        # check that the SI is beeing to be deleted
        self.vnc_lib.service_instance_delete.assert_called_with(
            id=si_obj.get_uuid())

    def test_gateway_clear(self):

        # update the previous router by removing the ext net
        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_network_refs'] = []
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'demo',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        self._test_snat_delete(router_dict)

    def test_no_more_interface(self):

        # update the previous router by removing all the interfaces
        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_machine_interface_refs'] = []
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'demo',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        self._test_snat_delete(router_dict)
