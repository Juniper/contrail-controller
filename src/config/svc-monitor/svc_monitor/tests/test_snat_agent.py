import copy
import json
import mock
import unittest
import uuid

from cfgm_common.vnc_db import DBBase
from svc_monitor import config_db
from svc_monitor import instance_manager
from svc_monitor import snat_agent
from svc_monitor import sandesh
from vnc_api.vnc_api import *
from svc_monitor.module_logger import ServiceMonitorModuleLogger
from svc_monitor.sandesh.port_tuple import ttypes
from cfgm_common import exceptions as vnc_exc

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

class StringMatcher(object):
    """
        This is a utility class that stores subsstrings of interest.
        This class provides an overriden __eq__ implementation that checks
        if an input string contains all substrings that are registered with this
        class.
    """
    def __init__(self, *args):
        self.args = args

    def __eq__(self, input_string):
        """
            Return true if input string contains all substrings that have been
            registered with this instance.
        """
        for str in self.args:
            if str not in input_string:
                return False
        return True

def _raise_no_id_exception(*args, **kwargs):
    """
        Method that can be registered to be invoked as a side effect with
        unittest mock framework.
        This is useful when simulating scenarios where a called function
        should raise an 'ID not found' exception.
    """
    raise NoIdError("ID_NOT_FOUND")

class SnatAgentTest(unittest.TestCase):

    def setUp(self):
        self.vnc_lib = mock.Mock()

        self.cassandra = mock.Mock()
        self.logger = mock.Mock()

        self.svc = mock.Mock()
        self.svc.netns_manager = instance_manager.NetworkNamespaceManager(
            self.vnc_lib, self.cassandra, self.logger, None, None, None)

        # Mock service module logger.
        self.logger = mock.MagicMock()
        self.module_logger = ServiceMonitorModuleLogger(self.logger)

        self.snat_agent = snat_agent.SNATAgent(self.svc, self.vnc_lib,
                                               self.cassandra, None,
                                               logger = self.module_logger)
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

        def db_read_side_effect(obj_type, uuids, **kwargs):
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
        def db_read_side_effect(obj_type, uuids, **kwargs):
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

        # Verify that SNAT VN was not found in the db and it was logged.
        # This will trigger the creation of the VN.
        self.logger.debug.assert_any_call( \
                          StringMatcher( \
                                        'Virtual Network', \
                                        'not found', \
                                        'Creating One'), \
                          sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # check that the snat service network is created
        left = ('default-domain:demo:snat-si-left_snat_' +
                ROUTER_1['uuid'])
        self.vnc_lib.virtual_network_create.assert_called_with(
            VirtualNetworkMatcher(left, False))

        # Route table was originally not found and was created.
        self.logger.debug.assert_any_call( \
                                  StringMatcher( \
                                                'Route Table', \
                                                'not found'), \
                                  sandesh.snat_agent.ttypes.SNATAgentDebugLog)

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

        def db_read_side_effect(obj_type, uuids, **kwargs):
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

        def db_read_side_effect(obj_type, uuids, **kwargs):
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


    def test_add_snat_errors(self):

        """
            This testcase triggers and validate error cases related to
            creation of an SNAT service instance.
        """

        # Create a logical router.
        router_obj = LogicalRouter('rtr1-name')

        # Setup required mock vnc library calls.
        self.vnc_lib.logical_router_read = mock.Mock(return_value=router_obj)
        self.vnc_lib.logical_router_update = mock.Mock()
        self.vnc_lib.route_table_create = mock.Mock()
        self.vnc_lib.route_table_read = mock.Mock(return_value=None)
        self.vnc_lib.virtual_network_create = mock.Mock()
        self.vnc_lib.virtual_network_update = mock.Mock()

        # Setup mock return values for virtual network related
        # reads from database.
        #
        # Return failure/false for all other reads.
        def db_read_side_effect(obj_type, uuids, **kwargs):
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

        self.cassandra.object_read = mock.Mock(side_effect=db_read_side_effect)

        # Add logical router to service monitor database.
        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'], ROUTER_1)

        #
        # Testcase 1 : Logical router lookup failure.
        #

        # Clear hangovers from prior testcases.
        self.logger.debug.reset()

        # Mock logical router read.
        original_mock = self.vnc_lib.logical_router_read
        self.vnc_lib.logical_router_read = mock.Mock(
                                           side_effect = _raise_no_id_exception)

        # Attempt to create SNAT instance.
        self.snat_agent.update_snat_instance(router)

        # Verify that SNAT create failure due to logical router not found error.
        self.logger.debug.assert_any_call(
                              StringMatcher("Unable to find logical router",
                                            "Add SNAT instance failed"),
                              sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset logical router read mock.
        self.vnc_lib.logical_router_read = original_mock


        #
        # Testcase 2 : Service Template lookup failure.
        #

        # Clear hangovers from prior testcases.
        self.logger.debug.reset()

        # Mock service template read.
        original_mock = self.vnc_lib.service_template_read
        self.vnc_lib.service_template_read = mock.Mock(side_effect=
                                                         _raise_no_id_exception)

        # Attempt to create SNAT instance.
        self.snat_agent.update_snat_instance(router)

        # Verify that SNAT VN was not found in the db and it was logged.
        # This will trigger the creation of the VN.
        self.logger.debug.assert_any_call(
                            StringMatcher(
                                "Unable to read service template",
                                "Add SNAT instance failed"),
                            sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset servie template read mock.
        self.vnc_lib.service_template_read = original_mock


        #
        # Testcase 3 : Service Instance lookup failure.
        #

        # Clear hangovers from prior tests.
        self.logger.debug.reset()

        # Mock Service Instance read.
        original_mock = self.vnc_lib.service_instance_read
        self.vnc_lib.service_instance_read = mock.Mock(
                                             side_effect=_raise_no_id_exception)
        # Populate service instance in logical router to simulat case where
        # a service instance reference exists but lookup fails.
        router.service_instance = 'dummy'

        # Attempt to create SNAT instance.
        self.snat_agent._add_snat_instance(router)

        # Verify that SNAT VN was not found in the db and it was logged.
        # This will trigger the creation of the VN.
        self.logger.debug.assert_any_call(
                            StringMatcher("Service instance","not found"),
                            sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset servie instance read mock.
        self.vnc_lib.service_instance_read = original_mock
        router.service_instance = None

    def test_delete_snat_errors(self):
        """
            This testcase triggers and validate error cases related to
            deletion of an SNAT instance.
        """

        # First, create the SNAT that is to be deleted.
        self.test_gateway_set()

        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_network_refs'] = []
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'demo',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'])
        router.update(router_dict)

        # Mock deletion of VN.
        #
        # The focus of this testcase is not deletion itself. Rather the focus
        # here is on different error scenarios when delete is requested.
        self.snat_agent.delete_snat_vn = mock.MagicMock()

        #
        # Testcase 1 : Logical router lookup failure.
        #

        # Simulate logical router lookup failure.
        original_mock = self.vnc_lib.logical_router_read
        self.vnc_lib.logical_router_read = mock.Mock(side_effect=
                                                 _raise_no_id_exception)

        # Attempt to delete SNAT instance.
        self.snat_agent.update_snat_instance(router)

        # Verify that SNAT create failure due to logical router not found error.
        self.logger.debug.assert_any_call( \
                                    StringMatcher( \
                                            'Unable to find logical router'), \
                                    sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset simulation of logical router lookup failure.
        self.vnc_lib.logical_router_read = mock.Mock(side_effect=None)
        self.vnc_lib.logical_router_read = original_mock


        #
        # Testcase 2 : Service Instance lookup failure.
        #

        # Mock service instance read call to raise an id not found exception
        # when invoked.
        original_mock = self.vnc_lib.service_instance_read
        self.vnc_lib.service_instance_read = mock.Mock(side_effect=
                                                          _raise_no_id_exception)

        # Attempt to delete SNAT instance.
        self.snat_agent.update_snat_instance(router)

        # Verify that service instance not found error is logged.
        self.logger.debug.assert_any_call( \
                                    StringMatcher( \
                                        'Unable to find service instance'), \
                                    sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset simulation service instance read.
        self.vnc_lib.service_instance_read = mock.Mock(side_effect=None)
        self.vnc_lib.service_instance_read = original_mock

        #
        # Testcase 3 : Logical router update failure.
        #

        # Mock logical router update method to raise an id not found exception
        # when invoked.
        original_mock = self.vnc_lib.logical_router_update
        self.vnc_lib.logical_router_update = mock.Mock(side_effect=
                                                          _raise_no_id_exception)

        # Attempt to delete SNAT instance.
        self.snat_agent.update_snat_instance(router)

        # Verify that logical router update failure error is logged.
        self.logger.debug.assert_any_call( \
                                    StringMatcher( \
                                        'Update of vnc lib for logical router',\
                                        'failed'), \
                                    sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset simulation of logical router lookup failure.
        self.vnc_lib.logical_router_update = mock.Mock(side_effect=None)
        self.vnc_lib.logical_router_update = original_mock

    def test_delete_snat_vn_errors(self):
        """
            This testcase triggers and validates virtual network related error
            cases during deletion of an SNAT instance.
        """

        # First, create the SNAT that is to be deleted.
        self.test_gateway_set()

        router_dict = copy.deepcopy(ROUTER_1)
        router_dict['virtual_network_refs'] = []
        router_dict['service_instance_refs'] = [
            {'to': ['default-domain',
                    'demo',
                    'si_' + ROUTER_1['uuid']],
             'uuid': 'si-uuid'}]

        router = config_db.LogicalRouterSM.locate(ROUTER_1['uuid'])
        router.update(router_dict)

        #  Mock service instance read method to return a custom service
        # instance object.
        si_obj = ServiceInstance('si_' + ROUTER_1['uuid'])
        si_obj.set_uuid('si-uuid')
        orig_si_read_mock = self.vnc_lib.service_instance_read
        self.vnc_lib.service_instance_read = mock.Mock(return_value=si_obj)

        # Mock virtual netwok read method to raise an exception when
        # invoked.
        original_mock = self.vnc_lib.virtual_network_read
        self.vnc_lib.virtual_network_read = mock.Mock(side_effect=
                                                        _raise_no_id_exception)

        # Attempt to delete SNAT instance.
        self.snat_agent.update_snat_instance(router)

        # Verify that failure to read virtual network is handled and logged.
        self.logger.debug.assert_any_call( \
                                    StringMatcher( \
                                        'Unable to find virtual network',\
                                        'Delete of SNAT instance',\
                                        'failed'), \
                                    sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset simulations.
        self.vnc_lib.virtual_network_read = mock.Mock(side_effect=None)
        self.vnc_lib.virtual_network_read = original_mock
        self.vnc_lib.service_instance_read = orig_si_read_mock

    def test_cleanup_snat_instances_errors(self):
        """
            This testcase triggers and validates snat cleanup related error
            scenarios.
        """

        # First, create the SNAT that is to be deleted.
        self.test_gateway_set()

        # Get and use the route table created as a part of SNAT create.
        si_obj = self.vnc_lib.service_instance_create.mock_calls[0][1][0]
        si_obj.set_uuid('si-uuid')

        # Setup mocks for service instance accessors.
        orig_si_delete_mock = self.vnc_lib.service_instance_delete
        self.vnc_lib.service_instance_delete = mock.Mock()

        #
        # Testcase 1 : Service instance not found error.
        #

        # Simulate service instance read to return not found exception.
        original_si_read_mock = self.vnc_lib.service_instance_read
        self.vnc_lib.service_instance_read = mock.Mock(side_effect=
                                                        _raise_no_id_exception)

        # Attempt to delete SNAT instance.
        self.snat_agent.cleanup_snat_instance(ROUTER_1['uuid'], si_obj.uuid)

        # Verify that service instance is not found in the db and it was logged.
        self.logger.debug.assert_any_call( \
              StringMatcher( \
                'Service instace si-uuid not found. SNAT cleanup failed.'),\
                sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset simulation of logical router lookup failure.
        self.vnc_lib.service_instance_read = mock.Mock(side_effect=None)
        self.vnc_lib.service_instance_read = original_si_read_mock

        #
        # Testcase 2 : Route table delete failure.
        #

        # Mock service instance read to return our custom si object.
        original_si_read_mock = self.vnc_lib.service_instance_read
        self.vnc_lib.service_instance_read = mock.Mock(return_value=si_obj)

        # Mock route table delete method to return id not found error.
        ref_original_mock = self.vnc_lib.ref_update
        rt_delete_original_mock = self.vnc_lib.route_table_delete
        self.vnc_lib.ref_update = mock.Mock(side_effect=_raise_no_id_exception)
        self.vnc_lib.route_table_delete = mock.Mock(side_effect=
                                                        _raise_no_id_exception)

        # Attempt to delete SNAT instance.
        self.snat_agent.cleanup_snat_instance(ROUTER_1['uuid'], si_obj.uuid)

        # Verify that delete of SNAT instance returns and logs failure.
        self.logger.debug.assert_any_call( \
              StringMatcher( \
                  'Update of logical router %s failed' % ROUTER_1['uuid']),\
                sandesh.snat_agent.ttypes.SNATAgentDebugLog)
        self.logger.debug.assert_any_call( \
                  StringMatcher('Route table','delete failed'),\
                            sandesh.snat_agent.ttypes.SNATAgentDebugLog)

        # Reset simulation of logical router lookup failure.
        self.vnc_lib.ref_update = ref_original_mock
        self.vnc_lib.route_table_delete = rt_delete_original_mock
        self.vnc_lib.service_instance_delete = orig_si_delete_mock
        self.vnc_lib.service_instance_read = original_si_read_mock

