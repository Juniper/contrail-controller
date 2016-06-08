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

    def test_single_vm_port_tuple_create(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_virtual_network('fake-domain:fake-project:public-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:fake-vn-uuid')

        st = test_utils.create_test_st(name='fake-st-uuid',
            intf_list=[['right', True], ['left', True]], version='2')
        si = test_utils.create_test_si(name='fake-si-uuid', count=1,
            intf_list=['public-vn', 'fake-vn-uuid'])
        si.service_template = 'fake-st-uuid'

        pt = test_utils.create_test_port_tuple(
            'fake-domain:fake-project:fake-si-uuid:fake-port-tuple',
            'fake-si-uuid')
        vmi = test_utils.create_test_vmi('fake-domain:fake-project:fake-vmi-uuid-left', pt)
        vmi.params = {}
        vmi.params['service_interface_type'] = 'left'
        vmi = test_utils.create_test_vmi('fake-domain:fake-project:fake-vmi-uuid-right', pt)
        vmi.params = {}
        vmi.params['service_interface_type'] = 'right'

        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple')

        self.mocked_vnc.ref_update.assert_any_call('instance-ip',
            'fake-iip-uuid', 'virtual-machine-interface', 'fake-vmi-uuid-left',
            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call('instance-ip',
            'fake-iip-uuid', 'virtual-machine-interface', 'fake-vmi-uuid-right',
            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call('service-instance',
            'fake-si-uuid', 'instance-ip', 'fake-iip-uuid', None, 'ADD',
            ServiceInterfaceTag('left'))
        self.mocked_vnc.ref_update.assert_any_call('service-instance',
            'fake-si-uuid', 'instance-ip', 'fake-iip-uuid', None, 'ADD',
            ServiceInterfaceTag('right'))

    def test_two_vm_port_tuple_create(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_virtual_network('fake-domain:fake-project:public-vn')
        test_utils.create_test_virtual_network('fake-domain:fake-project:fake-vn-uuid')

        st = test_utils.create_test_st(name='fake-st-uuid',
            intf_list=[['right', True], ['left', True]], version='2')
        si = test_utils.create_test_si(name='fake-si-uuid', count=1,
            intf_list=['public-vn', 'fake-vn-uuid'])

        si.service_template = 'fake-st-uuid'
        pt = test_utils.create_test_port_tuple(
            'fake-domain:fake-project:fake-si-uuid:fake-port-tuple1',
            'fake-si-uuid')
        vmi = test_utils.create_test_vmi('fake-domain:fake-project:fake-vmi-uuid-left1', pt)
        vmi.params = {}
        vmi.params['service_interface_type'] = 'left'
        vmi = test_utils.create_test_vmi('fake-domain:fake-project:fake-vmi-uuid-right1', pt)
        vmi.params = {}
        vmi.params['service_interface_type'] = 'right'
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple1')

        si.service_template = 'fake-st-uuid'
        pt = test_utils.create_test_port_tuple(
            'fake-domain:fake-project:fake-si-uuid:fake-port-tuple2',
            'fake-si-uuid')
        vmi = test_utils.create_test_vmi('fake-domain:fake-project:fake-vmi-uuid-left2', pt)
        vmi.params = {}
        vmi.params['service_interface_type'] = 'left'
        vmi = test_utils.create_test_vmi('fake-domain:fake-project:fake-vmi-uuid-right2', pt)
        vmi.params = {}
        vmi.params['service_interface_type'] = 'right'
        self.pt_agent.update_port_tuple(pt_id='fake-port-tuple2')

        self.mocked_vnc.ref_update.assert_any_call('instance-ip',
            'fake-iip-uuid', 'virtual-machine-interface', 'fake-vmi-uuid-left1',
            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call('instance-ip',
            'fake-iip-uuid', 'virtual-machine-interface', 'fake-vmi-uuid-right1',
            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call('instance-ip',
            'fake-iip-uuid', 'virtual-machine-interface', 'fake-vmi-uuid-left2',
            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call('instance-ip',
            'fake-iip-uuid', 'virtual-machine-interface', 'fake-vmi-uuid-right2',
            None, 'ADD')
        self.mocked_vnc.ref_update.assert_any_call('service-instance',
            'fake-si-uuid', 'instance-ip', 'fake-iip-uuid', None, 'ADD',
            ServiceInterfaceTag('left'))
        self.mocked_vnc.ref_update.assert_any_call('service-instance',
            'fake-si-uuid', 'instance-ip', 'fake-iip-uuid', None, 'ADD',
            ServiceInterfaceTag('right'))
