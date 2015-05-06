import copy
import mock
import unittest

from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from svc_monitor import snat_agent
from vnc_api.vnc_api import *


ROUTER_1 = {
    'fq_name': ['default-domain', 'demo', 'router1'],
    'parent_uuid': 'a8d55cfc-c66a-4eeb-82f8-d144fe74c46b',
    'uuid': '8e9b4859-d4c2-4ed5-9468-4809b1a926f3',
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


class VirtualNetworkRouteTableMatcher(object):

    def __init__(self, net_name, rt_name):
        self._net_name = net_name
        self._rt_name = rt_name

    def __eq__(self, net_obj):
        if self._rt_name:
            return (net_obj.fq_name[-1] == self._net_name and
                    net_obj.get_route_table_refs()[0]['to'][-1] ==
                    self._rt_name)
        else:
            return (net_obj.fq_name[-1] == self._net_name and
                    not net_obj.get_route_table_refs())


class VirtualNetworkMatcher(object):

    def __init__(self, name, user_visible):
        self._name = name
        self._user_visible = user_visible

    def __eq__(self, net_obj):
        user_visible = net_obj.get_id_perms().get_user_visible()
        return (net_obj.get_fq_name_str() == self._name and
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

        return (si_obj.fq_name[-1] == self._name and
                mode == self._mode and
                left == self._left and
                right == self._right)


class LogicalRouterMatcher(object):

    def __init__(self, si_name):
        self._si_name = si_name

    def __eq__(self, rtr_obj):
        if self._si_name:
            return (rtr_obj.get_service_instance_refs()[0]['to'][-1] ==
                    self._si_name)
        else:
            return not rtr_obj.get_service_instance_refs()


class SnatAgentTest(unittest.TestCase):

    def setUp(self):
        self.vnc_lib = mock.Mock()
        self.vnc_lib.project_read = mock.Mock(
            return_value=Project())

        self.cassandra = mock.Mock()

        self.snat_agent = snat_agent.SNATAgent(None, self.vnc_lib, None)
        DBBase.init(self, None, self.cassandra)

    def tearDown(self):
        config_db.LogicalRouterSM.reset()
        config_db.VirtualNetworkSM.reset()
        config_db.VirtualMachineInterfaceSM.reset()

    def test_gateway_set(self):
        # register the public network
        config_db.VirtualNetworkSM.locate(
            ROUTER_1['virtual_network_refs'][0]['uuid'],
            {'fq_name': ROUTER_1['virtual_network_refs'][0]['to']})

        # register interfaces
        config_db.VirtualMachineInterfaceSM.locate(
            ROUTER_1['virtual_machine_interface_refs'][0]['uuid'],
            {'fq_name': ROUTER_1['virtual_machine_interface_refs'][0]['to'],
             'virtual_network_refs': [{'uuid': 'private1-uuid'}]})

        router_obj = LogicalRouter('rtr1-name')
        self.vnc_lib.logical_router_read = mock.Mock(return_value=router_obj)

        self.cassandra._cassandra_virtual_network_read = mock.Mock(
            return_value=(True, [{}]))
        self.vnc_lib.service_instance_read = mock.Mock(return_value=None)
        self.vnc_lib.route_table_read = mock.Mock(return_value=None)

        # used to force the snat service network creation
        self.vnc_lib.fq_name_to_id = mock.Mock(side_effect=NoIdError("xxx"))

        self.vnc_lib.route_table_create = mock.Mock()

        self.vnc_lib.virtual_network_read = mock.Mock(
            return_value=VirtualNetwork(name='private1-name'))
        self.vnc_lib.virtual_network_update = mock.Mock()

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'],
                                                  ROUTER_1)
        self.snat_agent.update_snat_instance(router)

        # check that the correct private network is read
        self.vnc_lib.virtual_network_read.assert_called_with(
            id='private1-uuid')

        # check that the snat service network is created
        left = ('default-domain:default-project:snat-si-left_si_' +
                ROUTER_1['uuid'])
        self.vnc_lib.virtual_network_create.assert_called_with(
            VirtualNetworkMatcher(left, False))

        # route table that is going to be set for each interface
        self.vnc_lib.route_table_create.assert_called_with(
            RouteTableMatcher('0.0.0.0/0'))

        # check that the route table is applied to the network
        rt_name = 'rt_' + ROUTER_1['uuid']
        self.vnc_lib.virtual_network_update.assert_called_with(
            VirtualNetworkRouteTableMatcher('private1-name', rt_name))

        # check that the SI is correctly set with the right interfaces
        right = ':'.join(ROUTER_1['virtual_network_refs'][0]['to'])
        self.vnc_lib.service_instance_create.assert_called_with(
            ServiceInstanceMatcher('si_' + ROUTER_1['uuid'],
                                   left, right, 'active-standby'))

        # check that the SI created is set to the logical router
        self.vnc_lib.logical_router_update.assert_called_with(
            LogicalRouterMatcher('si_' + ROUTER_1['uuid']))

    def _test_snat_delete(self, router_dict):
        self.test_gateway_set()

        # reset all calls
        self.vnc_lib.virtual_network_update.reset_mock()
        self.vnc_lib.service_instance_read.reset_mock()

        si_obj = ServiceInstance('si_' + ROUTER_1['uuid'])
        si_obj.set_uuid('si-uuid')
        self.vnc_lib.service_instance_read.return_value = si_obj

        # get the route table previously created
        rt_obj = self.vnc_lib.route_table_create.mock_calls[0][1][0]
        rt_obj.get_virtual_network_back_refs = mock.Mock(
            return_value=[{'uuid': 'private1-uuid'}])
        self.vnc_lib.route_table_read = mock.Mock(return_value=rt_obj)

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'])
        router.update(router_dict)
        self.snat_agent.update_snat_instance(router)

        self.vnc_lib.service_instance_read.assert_called_with(
            fq_name=router_dict['service_instance_refs'][0]['to'])

        # check that the route table is removed from the networks
        self.vnc_lib.virtual_network_update.assert_called_with(
            VirtualNetworkRouteTableMatcher('private1-name', None))

        # check that the route table is deleted
        self.vnc_lib.route_table_delete.assert_called_with(id=rt_obj.uuid)

        # check that the SI is removed from the logical router
        self.vnc_lib.logical_router_update.assert_called_with(
            LogicalRouterMatcher(None))

        # check that the SI is beeing to be deleted
        self.vnc_lib.service_instance_delete.assert_called_with(
            id=si_obj.get_uuid())

    def test_gateway_clear(self):

        # update the previous router by removing the ext net
        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_network_refs'] = []
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'default-project',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        self._test_snat_delete(router_dict)

    def test_no_more_interface(self):

        # update the previous router by removing all the interfaces
        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_machine_interface_refs'] = []
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'default-project',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        self._test_snat_delete(router_dict)

    def test_add_interface(self):
        self.test_gateway_set()

        # update the previous router by removing all the interfaces
        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_machine_interface_refs'].append(
            {'attr': None,
             'to': ['default-domain',
                    'demo',
                    'd0022578-5b16-4da8-bd4d-5760faf134dc'],
             'uuid': 'd0022578-5b16-4da8-bd4d-5760faf134dc'})
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'default-project',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        config_db.VirtualMachineInterfaceSM.locate(
            router_dict['virtual_machine_interface_refs'][1]['uuid'],
            {'fq_name': router_dict['virtual_machine_interface_refs'][1]['to'],
             'virtual_network_refs': [{'uuid': 'private2-uuid'}]})

        # reset all calls
        self.vnc_lib.virtual_network_update.reset_mock()
        self.vnc_lib.service_instance_read.reset_mock()
        self.vnc_lib.virtual_network_read.reset_mock()

        self.vnc_lib.virtual_network_read = mock.Mock(
            return_value=VirtualNetwork(name='private2-name'))

        # get the route table previously created
        rt_obj = self.vnc_lib.route_table_create.mock_calls[0][1][0]
        rt_obj.get_virtual_network_back_refs = mock.Mock(
            return_value=[{'uuid': 'private1-uuid'}])
        self.vnc_lib.route_table_read = mock.Mock(return_value=rt_obj)

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'])
        router.update(router_dict)
        self.snat_agent.update_snat_instance(router)

        # check that the correct private network is read
        self.vnc_lib.virtual_network_read.assert_called_with(
            id='private2-uuid')

        # check that the route table is applied to the network
        rt_name = 'rt_' + ROUTER_1['uuid']
        self.vnc_lib.virtual_network_update.assert_called_with(
            VirtualNetworkRouteTableMatcher('private2-name', rt_name))

    def test_del_interface(self):
        self.test_add_interface()

        # update the previous router by removing all the interfaces
        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_machine_interface_refs'] = [
            {'attr': None,
             'to': ['default-domain',
                    'demo',
                    'd0022578-5b16-4da8-bd4d-5760faf134dc'],
             'uuid': 'd0022578-5b16-4da8-bd4d-5760faf134dc'}]
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'default-project',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        # reset all calls
        self.vnc_lib.virtual_network_update.reset_mock()
        self.vnc_lib.service_instance_read.reset_mock()
        self.vnc_lib.virtual_network_read.reset_mock()

        self.vnc_lib.virtual_network_read = mock.Mock(
            return_value=VirtualNetwork(name='private1-name'))

        # get the route table previously created
        rt_obj = self.vnc_lib.route_table_create.mock_calls[0][1][0]
        rt_obj.get_virtual_network_back_refs = mock.Mock(
            return_value=[{'uuid': 'private1-uuid'},
                          {'uuid': 'private2-uuid'}])
        self.vnc_lib.route_table_read = mock.Mock(return_value=rt_obj)

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'])
        router.update(router_dict)
        self.snat_agent.update_snat_instance(router)

        # check that the correct private network is read
        self.vnc_lib.virtual_network_read.assert_called_with(
            id='private1-uuid')

        # check that the route table is applied to the network
        self.vnc_lib.virtual_network_update.assert_called_with(
            VirtualNetworkRouteTableMatcher('private1-name', None))
