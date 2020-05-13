#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import logging
import sys
try:
    from unittest.mock import MagicMock
except ImportError:
    from mock import MagicMock
from vnc_cfg_api_server.tests import test_case

sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
sys.path.append('../fabric-ansible/ansible-playbooks/module_utils')
sys.modules['import_server'] = MagicMock()
sys.modules['contrail_command'] = MagicMock()
from discover_os_computes import FilterModule


logger = logging.getLogger(__name__)


class TestDiscoverOsComputes(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.cli_filter = FilterModule
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestDiscoverOsComputes, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestDiscoverOsComputes, cls).tearDownClass(*args, **kwargs)

    def generate_test_node_type_general(self, description, expected):
        node_type = self.cli_filter.get_node_type(description)
        self.assertEqual(expected, node_type)

    def test_get_node_type_ovs(self):
        description = "node_type: OVS Linux Kernel"
        expected = "ovs-compute"
        self.generate_test_node_type_general(description, expected)

    def test_get_node_type_sriov(self):
        description = "Linux Kernel node_type: sriov Linux Kernel"
        expected = "sriov-compute"
        self.generate_test_node_type_general(description, expected)

    def test_get_node_type_fail_name(self):
        description = "node_type: fail_name"
        expected = None
        self.generate_test_node_type_general(description, expected)

    def test_get_node_type_fail_regex(self):
        description = "fail_name"
        expected = None
        self.generate_test_node_type_general(description, expected)

    def generate_test_create_node_properties(self, device_neighbor_data, node_type, device_display_name,
                                             expected_output):
        node_properties = self.cli_filter.create_node_properties(device_neighbor_data, node_type, device_display_name)
        self.assertDictEqual(expected_output, node_properties)

    def test_create_node_properties_with_full_input(self):
        device_neighbor_full_details = {
            u'lldp-remote-chassis-id': u'00:0c:29:8b:ef:1c',
            u'lldp-remote-management-address': u'10.7.64.124',
            u'lldp-remote-system-capabilities-enabled': u'Bridge Router',
            u'lldp-local-parent-interface-name': u'ae3',
            u'lldp-remote-management-addr-oid': u'', u'lldp-remote-system-name': u'node-4',
            u'lldp-remote-chassis-id-subtype': u'Mac address',
            u'lldp-remote-system-capabilities-supported': u'Bridge WLAN Access Point '
                                                          u'Router Station Only',
            u'lldp-index': u'1', u'lldp-remote-port-id-subtype': u'Mac address',
            u'lldp-timemark': u'Sun May 24 21:35:11 2020',
            u'lldp-remote-management-address-type': u'IPv4(1)',
            u'lldp-remote-management-address-interface-subtype': u'ifIndex(2)',
            u'lldp-local-port-id': u'522',
            u'lldp-system-description': {
                u'lldp-remote-system-description': u'node_type: OVS CentOS Linux 7 (Core)'},
            u'lldp-ttl': u'120', u'lldp-local-port-ageout-count': u'0',
            u'lldp-local-interface': u'xe-0/0/3',
            u'lldp-remote-management-address-port-id': u'2',
            u'lldp-remote-port-description': u'ens224', u'lldp-age': u'10',
            u'lldp-remote-port-id': u'00:0c:29:8b:ef:26'
        }
        node_type = 'ovs-compute'
        device_display_name = 'Router_1'
        expected_output = {'node_type': 'ovs-compute', 'name': 'node-4', 'ports': [
            {'mac_address': '00:0c:29:8b:ef:26', 'port_name': 'xe-0/0/3', 'switch_name': 'Router_1',
             'name': 'ens224'}]}

        self.generate_test_create_node_properties(device_neighbor_full_details, node_type, device_display_name,
                                                  expected_output)

    def test_create_node_properties_missing_one_value(self):
        device_neighbor_details_without_port_desc = {
            u'lldp-remote-system-name': u'node-4',
            u'lldp-local-interface': u'xe-0/0/3',
            u'lldp-remote-port-description': u'',
            u'lldp-remote-port-id': u'00:0c:29:8b:ef:26'}
        node_type = 'ovs-compute'
        device_display_name = 'Router_1'
        expected_output = {'node_type': 'ovs-compute', 'name': 'node-4', 'ports': [
            {'mac_address': '00:0c:29:8b:ef:26', 'port_name': 'xe-0/0/3', 'switch_name': 'Router_1',
             'name': ''}]}
        self.generate_test_create_node_properties(device_neighbor_details_without_port_desc, node_type,
                                                  device_display_name, expected_output)
