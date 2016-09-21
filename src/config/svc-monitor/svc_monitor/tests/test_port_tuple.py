
import mock
from mock import patch
import unittest
from vnc_api.vnc_api import *
from svc_monitor.port_tuple import PortTupleAgent
from svc_monitor.config_db import *
import test_common_utils as test_utils

class PortTupleTest(unittest.TestCase):
    def setUp(self):
        InstanceIpSM._cassandra = mock.MagicMock()
        InstanceIpSM._cassandra.object_read = test_utils.iip_db_read
        ServiceInstanceSM._cassandra = mock.MagicMock()
        ServiceInstanceSM._cassandra.object_read = test_utils.si_db_read
        VirtualMachineInterfaceSM._cassandra = mock.MagicMock()
        VirtualMachineInterfaceSM._cassandra.object_read = test_utils.vmi_db_read
        InterfaceRouteTableSM._cassandra = mock.MagicMock()
        InterfaceRouteTableSM._cassandra.object_read = test_utils.irt_db_read

        self.mocked_vnc = mock.MagicMock()
        self.mocked_vnc.fq_name_to_id = test_utils.get_vn_id_for_fq_name
        self.mocked_vnc.instance_ip_create = test_utils.iip_create

        self.pt_agent = PortTupleAgent(
            svc_mon=mock.MagicMock(), vnc_lib=self.mocked_vnc,
            cassandra=mock.MagicMock(), config_section=mock.MagicMock(),
            logger=mock.MagicMock())

    def tearDown(self):
        ServiceTemplateSM.reset()
        ServiceInstanceSM.reset()
        InstanceIpSM.reset()
        del InstanceIpSM._cassandra
        ServiceInstanceSM.reset()
        del ServiceInstanceSM._cassandra
        VirtualMachineInterfaceSM.reset()
        del VirtualMachineInterfaceSM._cassandra
        InterfaceRouteTableSM.reset()
        del InterfaceRouteTableSM._cassandra

    def __create_test_si(self):
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
        si = test_utils.create_test_si(name='fake-si-uuid',
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

    def test_two_vm_port_tuple_create(self):
        """
        Verify multiple (two) Port Tuple creates for a service instance.

        """

        # Create a test Service Instance.
        si = self.__create_test_si()

        #Create first Port Tuple.
        pt = test_utils.create_test_port_tuple(
                      'fake-domain:fake-project:fake-si-uuid:fake-port-tuple1',
                      si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi1 = self.__create_test_vmi(pt, 'left', 'left1')
        rvmi1 = self.__create_test_vmi(pt, 'right', 'right1')

        # Update frist Port Tuple.
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple1')

        # Create second Port Tuple.
        pt = test_utils.create_test_port_tuple(
                      'fake-domain:fake-project:fake-si-uuid:fake-port-tuple2',
                      si.uuid)

        # Create the two virtual machines intefaces for the Port Tuple.
        lvmi2 = self.__create_test_vmi(pt, 'left', 'left2')
        rvmi2 = self.__create_test_vmi(pt, 'right', 'right2')

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
        self.pt_agent.update_port_tuple(lvmi)
        self.pt_agent.update_port_tuple(rvmi)

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
        iip = test_utils.create_test_iip('fake-domain:fake-project:liip')
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
