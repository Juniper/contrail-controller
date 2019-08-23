from __future__ import absolute_import

from builtins import range
import mock
from mock import patch
import unittest
import svc_monitor
from vnc_api.vnc_api import *
from svc_monitor.port_tuple import PortTupleAgent
from svc_monitor.config_db import *
from . import test_common_utils as test_utils
from svc_monitor.module_logger import ServiceMonitorModuleLogger
from svc_monitor.sandesh.port_tuple import ttypes

class PortTupleTest(unittest.TestCase):
    def setUp(self):
        InstanceIpSM._object_db = mock.MagicMock()
        InstanceIpSM._object_db.object_read = test_utils.iip_db_read
        ServiceInstanceSM._object_db = mock.MagicMock()
        ServiceInstanceSM._object_db.object_read = test_utils.si_db_read
        VirtualMachineInterfaceSM._object_db = mock.MagicMock()
        VirtualMachineInterfaceSM._object_db.object_read = test_utils.vmi_db_read
        InterfaceRouteTableSM._object_db = mock.MagicMock()
        InterfaceRouteTableSM._object_db.object_read = test_utils.irt_db_read

        self.mocked_vnc = mock.MagicMock()
        self.mocked_vnc.fq_name_to_id = test_utils.get_vn_id_for_fq_name
        self.mocked_vnc.instance_ip_create = test_utils.iip_create

        # Mock service module logger.
        self.logger = mock.MagicMock()
        self.module_logger = ServiceMonitorModuleLogger(self.logger)

        self.pt_agent = PortTupleAgent(
            svc_mon=mock.MagicMock(), vnc_lib=self.mocked_vnc,
            object_db=mock.MagicMock(), config_section=mock.MagicMock(),
            logger = self.module_logger)

    def tearDown(self):
        ServiceTemplateSM.reset()
        ServiceInstanceSM.reset()
        InstanceIpSM.reset()
        del InstanceIpSM._object_db
        ServiceInstanceSM.reset()
        del ServiceInstanceSM._object_db
        VirtualMachineInterfaceSM.reset()
        del VirtualMachineInterfaceSM._object_db
        InterfaceRouteTableSM.reset()
        del InterfaceRouteTableSM._object_db

    def __create_test_si(self, name=None):
        """
        Create and return a fake/test service-instance(SI).

        The SI is created per schema hierarchy which involves
        creation of test domain, project and service-template.

        """

        # Create test project.
        test_utils.create_test_project('fake-domain:fake-project')

        # Create two VN's for the SI.
        test_utils.create_test_virtual_network(
                             'fake-domain:fake-project:public-vn')
        test_utils.create_test_virtual_network(
                          'fake-domain:fake-project:fake-vn-uuid')

        # Create a service-template.
        st = test_utils.create_test_st(name='fake-st-uuid',
                                       intf_list=[['right', True],
                                                  ['left', True]],
                                       version='2')

        # Create test service-instance.
        if not name:
            name = 'fake-si-uuid'
        si = test_utils.create_test_si(name=name,
                                       count=1,
                                       intf_list=['public-vn',
                                                  'fake-vn-uuid'])

        # Add service-template reference in service-instance.
        si.service_template = 'fake-st-uuid'

        # Mock the update call on this service instance.
        #
        # This being a test service instance, we do not want this
        # SI being updated from config db.
        si.update = mock.MagicMock()

        return si

    def __get_service_template(self, si):
        """
        Given a service-instance, this its servive-template uuid.

        Parameters:
            si - service instance object

        """
        return ServiceTemplateSM.get(si.service_template)

    def __create_test_vmi(self, pt, interface_type, name = None):
        """
        Create a test virtual-machine-interface (VMI).

        Parameters:
            pt             - Port Tuple object.
            interface_type - Type of the created VMI.
            name           - Custom name to the inteface.

        """

        if name:
            vmi_name = 'fake-domain:fake-project:fake-vmi-uuid-' + name
        else:
            vmi_name = 'fake-domain:fake-project:fake-vmi-uuid-' + \
                       interface_type

        # Create VMI.
        vmi = test_utils.create_test_vmi(vmi_name, pt)
        vmi.if_type = interface_type
        vmi.params = {}

        # Populate service interface info.
        vmi.params['service_interface_type'] = interface_type

        # Mock the update call on this virtual-machine-instance.
        #
        # This being a test vmi, we do not want this being updated
        # from config db.
        vmi.update = mock.MagicMock()

        return vmi

    def __create_interface_route_table(self, si = None, intf_type = None):
        """
        Create a test interface route table object.

        Parameters:
            si        - service instance object
            intf_type - interface type this route table object is
                        associated with

        """
        # Locate / create a interface-route-table object.
        irt_obj = InterfaceRouteTableSM.locate('fake-irt-uuid')

        # Update service-intstance of this irt object, if requested
        if si:
            si.interface_route_tables[irt_obj.uuid]= {
                                                     'interface_type':intf_type
                                                      }

        return irt_obj

    def __get_project(self):
        return ProjectSM.get('fake-domain:fake-project')

    def test_single_vm_port_tuple_create(self):
        """
        Verify Port Tuple create for a service instance.

        """

        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create a Port Tuple.
        pt = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                       si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi = self.__create_test_vmi(pt, 'left')
        rvmi = self.__create_test_vmi(pt, 'right')

        # Update Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        # Validate the expected API invocations.
        self.mocked_vnc.ref_update.assert_any_call(
                                     'instance-ip', 'fake-iip-uuid',
                                     'virtual-machine-interface', lvmi.uuid,
                                     None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                     'instance-ip', 'fake-iip-uuid',
                                     'virtual-machine-interface', rvmi.uuid,
                                     None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                     'service-instance', si.uuid,
                                     'instance-ip', 'fake-iip-uuid',
                                     None, 'ADD',
                                     ServiceInterfaceTag('left'))

        self.mocked_vnc.ref_update.assert_any_call(
                                     'service-instance', si.uuid,
                                     'instance-ip', 'fake-iip-uuid',
                                     None, 'ADD',
                                     ServiceInterfaceTag('right'))
        test_utils.delete_test_port_tuple(pt)

    def test_two_vm_port_tuple_create(self):
        """
        Verify multiple (two) Port Tuple creates for a service instance.

        """

        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create first Port Tuple.
        pt1 = test_utils.create_test_port_tuple(
                      'fake-domain:fake-project:fake-si-uuid:fake-port-tuple1',
                      si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi1 = self.__create_test_vmi(pt1, 'left', 'left1')
        rvmi1 = self.__create_test_vmi(pt1, 'right', 'right1')

        # Update frist Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple1')

        # Create second Port Tuple.
        pt2 = test_utils.create_test_port_tuple(
                      'fake-domain:fake-project:fake-si-uuid:fake-port-tuple2',
                      si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi2 = self.__create_test_vmi(pt2, 'left', 'left2')
        rvmi2 = self.__create_test_vmi(pt2, 'right', 'right2')

        # Update second Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple2')

        # Validate the expected API invocations.
        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', lvmi1.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', rvmi1.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', lvmi2.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', rvmi2.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'service-instance', si.uuid,
                                    'instance-ip', 'fake-iip-uuid',
                                    None, 'ADD',
                                    ServiceInterfaceTag('left'))

        self.mocked_vnc.ref_update.assert_any_call(
                                    'service-instance', si.uuid,
                                    'instance-ip', 'fake-iip-uuid',
                                    None, 'ADD',
                                    ServiceInterfaceTag('right'))
        test_utils.delete_test_port_tuple(pt1)
        test_utils.delete_test_port_tuple(pt2)

    def test_port_tuple_update_invalid_si(self):
        """
        Port Tuple update failure scenario: Service instance not found.

        """

        # Create a Port Tuple without a valid service instance.
        pt = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                       'invalid-si')

        # Invoke port tuple update. Update should fail with error.
        self.pt_agent.update_port_tuple(pt_id = pt.uuid)

        # Verify failure is logged.
        self.logger.debug.assert_called_with('Service Instance invalid-si not found', \
                               svc_monitor.sandesh.port_tuple.ttypes.PortTupleDebugLog)

        # Remove the failed port tuple from DB.
        test_utils.delete_test_port_tuple(pt)

    def test_port_tuple_update_invalid_port_config(self):
        """
        Port Tuple update failure scenario: Port config construction failure.

        """
        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create first Port Tuple.
        pt = test_utils.create_test_port_tuple(
                      'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                      si.uuid)

        # Stash orginal port config construction function.
        self.orig_port_config_fn = self.pt_agent.get_port_config

        # Mock port config construction to return None.
        self.pt_agent.get_port_config = mock.MagicMock(return_value=None)

        # Invoke port tuple update. Update should fail with error.
        self.pt_agent.update_port_tuple(pt_id = pt.uuid)

        # Verify failure is logged.
        self.logger.debug.assert_called_with( \
             'Failed to construct port config for Port Tuple fake-port-tuple', \
             svc_monitor.sandesh.port_tuple.ttypes.PortTupleDebugLog)

        # Restore the port config construction function.
        self.pt_agent.get_port_config = self.orig_port_config_fn

        # Remove the failed port tuple from DB.
        test_utils.delete_test_port_tuple(pt)

    def test_port_tuple_update_invalid_vmi(self):
        """
        Port Tuple update failure scenario: Invalid VMI.

        """
        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create a Port Tuple.
        pt = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                       si.uuid)

        # Create test virtual machines inteface for the Port Tuple.
        lvmi = self.__create_test_vmi(pt, 'left')

        # Delete the VMI from VMI db, so a lookup for that will fail.
        test_utils.delete_test_vmi(lvmi)
        pt.virtual_machine_interfaces.add(lvmi.uuid)

        # Update Port Tuple. This will fail due to VMI lookup failure.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        # Verify failure is logged.
        self.logger.debug.assert_called_with( \
             'VMI fake-vmi-uuid-left not found for Port Tuple fake-port-tuple', \
             svc_monitor.sandesh.port_tuple.ttypes.PortTupleDebugLog)

        # Remove the failed port tuple from DB.
        test_utils.delete_test_port_tuple(pt)

    def test_port_tuple_update_invalid_vmi_params(self):
        """
        Port Tuple update failure scenarios: Invalid VMI parameters.

        """
        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create a Port Tuple.
        pt = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                       si.uuid)

        # Create test virtual machines inteface for the Port Tuple.
        lvmi = self.__create_test_vmi(pt, 'left')
        lvmi.params = {}

        # Update Port Tuple. Update should fail due to invalid VMI params.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        # Verify failure is logged.
        self.logger.debug.assert_called_with( \
            'VMI fake-vmi-uuid-left has invalid params for Port Tuple fake-port-tuple', \
            svc_monitor.sandesh.port_tuple.ttypes.PortTupleDebugLog)

        # Remove the failed port tuple from DB.
        test_utils.delete_test_port_tuple(pt)

    def test_two_vm_port_tuple_create(self):
        """
        Verify multiple (two) Port Tuple creates for a service instance.

        """

        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create first Port Tuple.
        pt1 = test_utils.create_test_port_tuple(
                      'fake-domain:fake-project:fake-si-uuid:fake-port-tuple1',
                      si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi1 = self.__create_test_vmi(pt1, 'left', 'left1')
        rvmi1 = self.__create_test_vmi(pt1, 'right', 'right1')

        # Update frist Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple1')

        # Create second Port Tuple.
        pt2 = test_utils.create_test_port_tuple(
                      'fake-domain:fake-project:fake-si-uuid:fake-port-tuple2',
                      si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi2 = self.__create_test_vmi(pt2, 'left', 'left2')
        rvmi2 = self.__create_test_vmi(pt2, 'right', 'right2')

        # Update second Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple2')

        # Validate the expected API invocations.
        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', lvmi1.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', rvmi1.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', lvmi2.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', rvmi2.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'service-instance', si.uuid,
                                    'instance-ip', 'fake-iip-uuid',
                                    None, 'ADD',
                                    ServiceInterfaceTag('left'))

        self.mocked_vnc.ref_update.assert_any_call(
                                    'service-instance', si.uuid,
                                    'instance-ip', 'fake-iip-uuid',
                                    None, 'ADD',
                                    ServiceInterfaceTag('right'))
        test_utils.delete_test_port_tuple(pt1)
        test_utils.delete_test_port_tuple(pt2)

    def test_service_type_val_verify(self):
        """
        Verify that the service type name returned by PT agent is
        as expected.
        """
        service_type_name = self.pt_agent.handle_service_type()
        self.assertEqual(service_type_name, "port-tuple")

    def test_port_tuple_vmi_update(self):
        """
        Validate that updating Port Tuple with VMI, results in
        VNC api invocation with expected parameters.

        """
        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create a Port Tuple.
        pt = test_utils.create_test_port_tuple(
            'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
            si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi = self.__create_test_vmi(pt, 'left')
        rvmi = self.__create_test_vmi(pt, 'right')

        # Invoke Port Tuple update with created vmi's
        self.pt_agent.update_vmi_port_tuples(lvmi)
        self.pt_agent.update_vmi_port_tuples(rvmi)

        # Validate the expected API invocations.
        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', lvmi.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'instance-ip', 'fake-iip-uuid',
                                    'virtual-machine-interface', rvmi.uuid,
                                    None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                                    'service-instance', si.uuid,
                                    'instance-ip', 'fake-iip-uuid',
                                    None, 'ADD',
                                    ServiceInterfaceTag('left'))

        self.mocked_vnc.ref_update.assert_any_call(
                                    'service-instance', si.uuid,
                                    'instance-ip', 'fake-iip-uuid',
                                    None, 'ADD',
                                    ServiceInterfaceTag('right'))
        test_utils.delete_test_port_tuple(pt)

    def test_get_port_config(self):
        """
        Validate port config that is constructed for a service-instance.

        """
        # Create a test Service Instance.
        si = self.__create_test_si()
        st = self.__get_service_template(si)

        # Construct port config for this service-instance.
        port_config = self.pt_agent.get_port_config(st, si)

        # Declare possible interface types.
        intfs = "right", "left"

        # Verify that port config has expected number of ports
        self.assertEqual(len(intfs), len(port_config))

        # Verify port config info for each interface type.
        for index in range(0, len(intfs)):

            # Verify that ports for requested intefaces exists in port config
            self.assertTrue(intfs[index] in port_config)

            # Verify that port and inteface properties are in sync
            self.assertEqual(port_config[intfs[index]].get("shared-ip"), True)

    def test_update_port_static_route(self):
        """
        Verify that we are able to add / delete static routes on a port
        through Port Tuple update.

        """

        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create a Port Tuple.
        pt = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                       si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi = self.__create_test_vmi(pt, 'left')
        rvmi = self.__create_test_vmi(pt, 'right')

        #
        # Test ADD of interface route table
        #

        # Create the interface-route-table object.
        irt_obj = self.__create_interface_route_table(si, 'left')

        # Update Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        # Validate the expected ADD API invocations.
        self.mocked_vnc.ref_update.assert_any_call(
                            'virtual-machine-interface', lvmi.uuid,
                            'interface-route-table', irt_obj.uuid,
                            None, 'ADD')

        #
        # Test DELETE of interface route table
        #

        # Remove the irt from service-instance.
        del si.interface_route_tables[irt_obj.uuid]

        # Simulate the case where irt is attached to the vmi.
        lvmi.interface_route_tables = {irt_obj.uuid}

        # Update Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        # Validate the expected DELETE API invocation.
        self.mocked_vnc.ref_update.assert_any_call(
                            'virtual-machine-interface', lvmi.uuid,
                            'interface-route-table', irt_obj.uuid,
                            None, 'DELETE')
        test_utils.delete_test_port_tuple(pt)

    def test_vmi_add_service_health_check(self):
        """
        Verify that we are able to add service health check on an
        interface.

        """

        # Create a test Service Instance.
        si = self.__create_test_si()
        st = self.__get_service_template(si)

        #Create a Port Tuple.
        pt = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                       si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi = self.__create_test_vmi(pt, 'left')
        rvmi = self.__create_test_vmi(pt, 'right')

        # Create ServiceHealthCheck object.
        test_utils.create_test_service_health_check(
                                              'fake-domain:fake-project:lhiip',
                                              si.uuid)

        # Enable ServiceHealthCheck on test SI.
        si.service_health_checks['lhiip'] = {'interface_type':'left'}

        # Update Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        # Validate the expected ADD API invocations.
        self.mocked_vnc.ref_update.assert_any_call(
                            'virtual-machine-interface', lvmi.uuid,
                            'service-health-check', 'lhiip',
                            None, 'ADD')

        self.mocked_vnc.ref_update.assert_any_call(
                            'instance-ip', 'fake-iip-uuid',
                            'virtual-machine-interface', lvmi.uuid,
                            None, 'ADD')
        test_utils.delete_test_port_tuple(pt)

    def test_multiple_si_service_health_check_common_vmi(self):
        """
        Verify that we are able to add service health check on an
        interface.

        """
        # Create a test Service Instance.
        si1 = self.__create_test_si(name='fake-si1-uuid')
        si2 = self.__create_test_si(name='fake-si2-uuid')
        si3 = self.__create_test_si(name='fake-si3-uuid')

        # Create ServiceHealthCheck object.
        test_utils.create_test_service_health_check(
                                              'fake-domain:fake-project:rhiip1',
                                              si1.uuid)
        test_utils.create_test_service_health_check(
                                              'fake-domain:fake-project:rhiip2',
                                              si2.uuid)
        test_utils.create_test_service_health_check(
                                              'fake-domain:fake-project:rhiip3',
                                              si3.uuid)

        # Enable ServiceHealthCheck on test SI.
        si1.service_health_checks['rhiip1'] = {'interface_type':'right'}
        si2.service_health_checks['rhiip2'] = {'interface_type':'right'}
        si3.service_health_checks['rhiip3'] = {'interface_type':'right'}

        #Create a Port Tuple.
        pt1 = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si1-uuid:fake-port-tuple1',
                       si1.uuid)
        l1vmi = self.__create_test_vmi(pt1, 'left', 'left1')
        rvmi = self.__create_test_vmi(pt1, 'right', 'right1')

        pt2 = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si2-uuid:fake-port-tuple2',
                       si2.uuid)
        l2vmi = self.__create_test_vmi(pt2, 'left', 'left2')
        pt2.virtual_machine_interfaces.add(rvmi.uuid)
        rvmi.port_tuples.add(pt2.uuid)

        pt3 = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si3-uuid:fake-port-tuple3',
                       si3.uuid)
        l3vmi = self.__create_test_vmi(pt3, 'left', 'left3')
        pt3.virtual_machine_interfaces.add(rvmi.uuid)
        rvmi.port_tuples.add(pt3.uuid)

        # Update PortTuple
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple1')
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple2')
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple3')

        # Validate the expected ADD API invocations.
        self.mocked_vnc.ref_update.assert_any_call(
                            'virtual-machine-interface', rvmi.uuid,
                            'service-health-check', 'rhiip1',
                            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call(
                            'virtual-machine-interface', rvmi.uuid,
                            'service-health-check', 'rhiip2',
                            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call(
                            'virtual-machine-interface', rvmi.uuid,
                            'service-health-check', 'rhiip3',
                            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call(
                            'instance-ip', 'fake-iip-uuid',
                            'virtual-machine-interface', rvmi.uuid,
                            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call(
                            'instance-ip', 'fake-iip-uuid',
                            'virtual-machine-interface', rvmi.uuid,
                            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call(
                            'instance-ip', 'fake-iip-uuid',
                            'virtual-machine-interface', rvmi.uuid,
                            None, 'ADD')
        test_utils.delete_test_port_tuple(pt1)
        test_utils.delete_test_port_tuple(pt2)
        test_utils.delete_test_port_tuple(pt3)

    def test_vmi_delete_service_health_check(self):
        """
        Verify that we are able to delete service health check from an
        interface.

        """

        # Create a test Service Instance.
        si = self.__create_test_si()
        st = self.__get_service_template(si)

        #Create a Port Tuple.
        pt = test_utils.create_test_port_tuple(
                       'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
                       si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi = self.__create_test_vmi(pt, 'left')
        rvmi = self.__create_test_vmi(pt, 'right')


        # Create ServiceHealthCheck object.
        test_utils.create_test_service_health_check(
                                              'fake-domain:fake-project:lhiip',
                                              si.uuid)

        # Create and attach a service-health-check iip on the vmi under test.
        iip_name  = si.uuid + '-' + 'left-v4-health-check-' + 'llip'
        iip_fq_name = 'fake-domain:fake-project:' + iip_name
        iip = test_utils.create_test_iip(iip_fq_name)
        iip.virtual_machine_interfaces = {lvmi.uuid}
        iip.service_health_check_ip = True
        lvmi.service_health_checks = ['lhiip']
        lvmi.instance_ips = [iip.uuid]

        # Update Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        # Validate the expected DELETE API invocations.

        self.mocked_vnc.ref_update.assert_any_call(
                                'virtual-machine-interface', lvmi.uuid,
                                'service-health-check', 'lhiip',
                                None, 'DELETE')

        self.mocked_vnc.ref_update.assert_any_call(
                                'instance-ip', 'fake-iip-uuid',
                                'virtual-machine-interface', lvmi.uuid,
                                None, 'DELETE')
        test_utils.delete_test_port_tuple(pt)

    def test_port_tuple_logging(self):
        # Test various logging invocations.

        # Case 1: Invalid message identifier.
        # Defaults to Service Monitor log functions.
        self.module_logger.error('Test log message with invalid log identifier', \
                                 id = 'some_invalid_log_fun_identifier')

        self.logger.error.assert_called_with( \
             'Test log message with invalid log identifier', \
             None)

        # Case 2: Custom message function
        # Invokes the provided log function.
        self.module_logger.error('Test log message with custom msg function', \
                                 msg_func = self.test_port_tuple_logging)

        self.logger.error.assert_called_with( \
             'Test log message with custom msg function', \
             self.test_port_tuple_logging)

        # Case 3: Registered error message
        # Invokes the pre-registered logging function.
        self.module_logger.error('Test log message with registered error func')

        self.logger.error.assert_called_with( \
             'Test log message with registered error func', \
             svc_monitor.sandesh.port_tuple.ttypes.PortTupleErrorLog)

        # Case 4: Registered info message
        # Invokes the pre-registered logging function.
        self.module_logger.info('Test log message with registered info func')

        self.logger.info.assert_called_with( \
             'Test log message with registered info func', \
             svc_monitor.sandesh.port_tuple.ttypes.PortTupleInfoLog)

        # Case 5: Registered debug message
        # Invokes the pre-registered logging function.
        self.module_logger.debug('Test log message with registered debug func')

        self.logger.debug.assert_called_with( \
             'Test log message with registered debug func', \
             svc_monitor.sandesh.port_tuple.ttypes.PortTupleDebugLog)

        # Case 6: Unregistered emergency message
        # Defaults to Service Monitor log functions.
        self.module_logger.emergency('Test log message with unregistered emergency func')

        self.logger.emergency.assert_called_with( \
             'Test log message with unregistered emergency func', \
             None)

        # Case 7: Unregistered alert message
        # Defaults to Service Monitor log functions.
        self.module_logger.alert('Test log message with unregistered alert func')

        self.logger.alert.assert_called_with( \
             'Test log message with unregistered alert func', \
             None)

        # Case 8: Unregistered critical message
        # Defaults to Service Monitor log functions.
        self.module_logger.critical('Test log message with unregistered critical func')

        self.logger.critical.assert_called_with( \
             'Test log message with unregistered critical func', \
             None)

        # Case 9: Unregistered warning message
        # Defaults to Service Monitor log functions.
        self.module_logger.warning('Test log message with unregistered warning func')

        self.logger.warning.assert_called_with( \
             'Test log message with unregistered warning func', \
             None)

        # Case 10: Unregistered notice message
        # Defaults to Service Monitor log functions.
        self.module_logger.notice('Test log message with unregistered notice func')

        self.logger.notice.assert_called_with( \
             'Test log message with unregistered notice func', \
             None)

        # Case 11: New message registration
        # Invokes the just registered logging function.
        new_log_funcs = {'test-custom' : self.test_port_tuple_logging}

        # Register the new function.
        self.module_logger.add_messages(**new_log_funcs)

        self.module_logger.error('Test log message with custom func', id = 'test-custom')

        self.logger.error.assert_called_with( \
                                'Test log message with custom func', \
                                self.test_port_tuple_logging)
