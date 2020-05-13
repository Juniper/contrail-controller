from __future__ import absolute_import
import sys
from vnc_api.gen.resource_client import *
try:
    from unittest.mock import MagicMock
except ImportError:
    from mock import MagicMock

sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
sys.modules['import_server'] = MagicMock()
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
sys.modules['contrail_command'] = MagicMock()
sys.path.append('../fabric-ansible/ansible-playbooks/module_utils')
from vnc_cfg_api_server.tests import test_case
import logging
from discover_os_computes import FilterModule

logger = logging.getLogger(__name__)


class TestPhysicalInterface(test_case.ApiServerTestCase):

    def test_check_node_type(self):
        cli_filter = FilterModule()
        description_1 = "node_type: OVS"
        node_type_1 = cli_filter.check_node_type(description_1)
        self.assertEqual("ovs-compute", node_type_1)
        description_2 = "node_type: sriov"
        node_type_2 = cli_filter.check_node_type(description_2)
        self.assertEqual("sriov-compute", node_type_2)
        description_3 = "node_type: fail_name"
        node_type_3 = cli_filter.check_node_type(description_3)
        self.assertEqual(None, node_type_3)
        description_4 = "fail_name"
        node_type_4 = cli_filter.check_node_type(description_4)
        self.assertEqual(None, node_type_4)

    def test_create_node_properties(self):
        device_neighbor_full_details = {u'lldp-remote-chassis-id': u'00:0c:29:8b:ef:1c',
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
                                        u'lldp-remote-port-id': u'00:0c:29:8b:ef:26'}
        node_type = 'ovs-compute'
        device_display_name = 'Router_1'
        cli_filter = FilterModule()
        node_properties_1 = cli_filter.create_node_properties(device_neighbor_full_details, node_type,
                                                              device_display_name)
        expected_output_1 = {'node_type': 'ovs-compute', 'name': 'node-4', 'ports': [
            {'mac_address': '00:0c:29:8b:ef:26', 'port_name': 'xe-0/0/3', 'switch_name': 'Router_1',
             'name': 'ens224'}]}
        self.assertDictEqual(expected_output_1, node_properties_1)

        device_neighbor_details_without_port_desc = {
            u'lldp-remote-system-name': u'node-4',
            u'lldp-local-interface': u'xe-0/0/3',
            u'lldp-remote-port-description': u'',
            u'lldp-remote-port-id': u'00:0c:29:8b:ef:26'}
        node_type = 'ovs-compute'
        device_display_name = 'Router_1'
        cli_filter = FilterModule()
        node_properties_2 = cli_filter.create_node_properties(device_neighbor_details_without_port_desc, node_type,
                                                              device_display_name)
        expected_output_2 = {'node_type': 'ovs-compute', 'name': 'node-4', 'ports': [
            {'mac_address': '00:0c:29:8b:ef:26', 'port_name': 'xe-0/0/3', 'switch_name': 'Router_1',
             'name': ''}]}
        self.assertDictEqual(node_properties_2, expected_output_2)
