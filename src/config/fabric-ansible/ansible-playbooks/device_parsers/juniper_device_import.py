#!/usr/bin/python

import re

import sys
sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")

from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log


class ParserModule(object):

    def parse_juniper_junos_qfx(self, device_data, regex_str):
        return self.parse_juniper_device_data(device_data, regex_str)

    def parse_juniper_junos(self, device_data, regex_str):
        return self.parse_juniper_device_data(device_data, regex_str)

    def parse_juniper_device_data(self, device_data, regex_str):
        cf_interfaces_list = device_data['output'][0]['rpc-reply']['configuration']['interfaces']['interface']
        rt_interfaces_list = device_data['output'][1]['rpc-reply']['interface-information']['physical-interface']
        _task_log("parsing runtime interfaces")
        rt_interfaces = self.junos_rt_intf_filter(rt_interfaces_list, regex_str)
        _task_log("parsing configured interfaces")
        cf_interfaces = self.junos_cf_intf_filter(cf_interfaces_list, regex_str)

        phy_int_payload = rt_interfaces['phy_interfaces_payload'] + cf_interfaces['phy_interfaces_payload']
        log_int_payload = rt_interfaces['log_interfaces_payload'] + cf_interfaces['log_interfaces_payload']

        dataplane_ip = cf_interfaces['lo_interface_ip']

        return {
            "physical_interfaces_list": phy_int_payload,
            "logical_interfaces_list": log_int_payload,
            "dataplane_ip": dataplane_ip
        }
    def junos_rt_intf_filter(self, interface_list_new,
                             regex_str):

        """This filter takes the inputs as the
         list of runtime interfaces obtained by running
         the show interfaces junos command.

        interface_list_new:
          [
            {
                "admin-status": "up",
                "current-physical-address": "00:26:88:db:38:00",
                "hardware-physical-address": "00:26:88:db:38:00",
                "if-auto-negotiation": "enabled",
                "if-config-flags": {
                    "iff-hardware-down": "",
                    "iff-snmp-traps": "",
                    "internal-flags": "0x4000"
                },
                "if-device-flags": {
                    "ifdf-down": "",
                    "ifdf-present": "",
                    "ifdf-running": ""
                },
                "if-flow-control": "enabled",
                "if-media-flags": {
                    "ifmf-none": ""
                },
                "if-remote-fault": "online",
                "interface-flapped":
                    "2018-03-25 02:05:36 PDT (1w4d 08:33 ago)",
                "interface-transmit-statistics": "Disabled",
                "l2pt-error": "none",
                "ld-pdu-error": "none",
                "link-level-type": "Ethernet",
                "local-index": "214",
                "loopback": "disabled",
                "mru": "1522",
                "mtu": "1514",
                "name": "ge-0/0/0",
                "oper-status": "down",
                "pad-to-minimum-frame-size": "Disabled",
                "physical-interface-cos-information": {
                    "physical-interface-cos-hw-max-queues": "8",
                    "physical-interface-cos-use-max-queues": "8"
                },
                "snmp-index": "533",
                "sonet-mode": "LAN-PHY",
                "source-filtering": "disabled",
                "speed": "1000mbps",
                "traffic-statistics": {
                    "input-bps": "0",
                    "input-pps": "0",
                    "output-bps": "0",
                    "output-pps": "0"
                }
            }
          ]

        and returns:
        {
         "phy_interfaces_payload": [{
                                     "physical_interface_port_id": 533,
                                     "physical_interface_mac_address": "00:11:22:33:44:55",
                                     "physical_interface_name": physical_intf_name},
                                      {<another payload>}],
         "log_interfaces_payload": [{
                                     "logical_interface_name": logical_intf_name,
                                     "logical_interface_type": "l3",
                                     "logical_interface_vlan_tag": 2 (optional),
                                     "physical_interface_name": physical_intf_name
                                    },
                                     {<another payload>}]
        }

        """
        if isinstance(interface_list_new, dict):
            interface_list_new = [interface_list_new]

        if not regex_str:
            regex_str = ".*"

        phy_interfaces_payloads = []
        log_interfaces_payloads = []

        for phy_interface in interface_list_new:
            physical_interface_name = phy_interface['name']
            if re.search(regex_str, physical_interface_name):
                phy_interface_payload = {"physical_interface_name": physical_interface_name}

                physical_interface_port_id = phy_interface.get('snmp-index')
                if physical_interface_port_id:
                     phy_interface_payload['physical_interface_port_id'] = physical_interface_port_id

                phy_int_mac_address = phy_interface.get(
                    'current-physical-address')
                if phy_int_mac_address:
                    phy_interface_payload['physical_interface_mac_address'] = phy_int_mac_address

                phy_interfaces_payloads.append(phy_interface_payload)

                log_intfs = phy_interface.get('logical-interface')
                if log_intfs:
                    if isinstance(log_intfs, dict):
                        log_units = [log_intfs]
                    else:
                        log_units = log_intfs

                    for log_unit in log_units:
                        log_interface_payload = {
                            "physical_interface_name": physical_interface_name, 
                            "logical_interface_name": log_unit['name']}

                        add_fmly = log_unit.get('address-family')
                        if add_fmly:
                            log_type = "l3"
                            if isinstance(add_fmly, dict):
                                address_fmly_list = [add_fmly]
                            else:
                                address_fmly_list = add_fmly
                            for address_fmly in address_fmly_list:
                                if address_fmly['address-family-name']\
                                        == 'eth-switch':
                                    log_type = "l2"
                            log_interface_payload[
                                'logical_interface_type'] = log_type
                        log_interfaces_payloads.append(log_interface_payload)

        return {"phy_interfaces_payload": phy_interfaces_payloads,
                "log_interfaces_payload": log_interfaces_payloads}

    def junos_cf_intf_filter(self, interface_list,
                             regex_str):
        """
        This filter takes the inputs as
        the list of configured interfaces obtained by running
        the show configuration interfaces junos command.

        interface_list:
          [
            {
                "name": "ge-0/1/0",
                "unit": [
                    {
                        "family": {
                            "inet": {
                                "address": {
                                    "name": "172.16.11.1/30"
                                }
                            }
                        },
                        "name": "1211",
                        "vlan-id": "11"
                    },
                    {
                        "family": {
                            "inet": {
                                "address": {
                                    "name": "172.16.12.1/30"
                                }
                            }
                        },
                        "name": "1212",
                        "vlan-id": "12"
                    },
                    {
                        "family": {
                            "inet": {
                                "address": {
                                    "name": "172.16.13.1/30"
                                }
                            }
                        },
                        "name": "1213",
                        "vlan-id": "13"
                    }
                ],
                "vlan-tagging": ""
            },
            {
                "name": "ge-0/1/1",
                "vlan-tagging": ""
            },
            {
                "name": "ge-0/1/2",
                "vlan-tagging": ""
            }
          ]

        and returns:
        {
         "phy_interfaces_payload": [{
                                     "physical_interface_port_id": 533 (optional),
                                     "physical_interface_mac_address": "00:11:22:33:44:55"(optional),
                                     "physical_interface_name": physical_intf_name},
                                      {<another payload>}],
         "log_interfaces_payload": [{
                                     "logical_interface_name": logical_intf_name,
                                     "logical_interface_type": "l3",
                                     "logical_interface_vlan_tag": 2,
                                     "physical_interface_name": physical_intf_name
                                    },
                                     {<another payload>}]
        }

        """
        if isinstance(interface_list, dict):
            interface_list = [interface_list]

        if not regex_str:
            regex_str = ".*"

        lo0_found = False
        lo0_ip_add = ''
        phy_interfaces_payloads = []
        log_interfaces_payloads = []

        for phy_interface in interface_list:
            phy_interface_name = phy_interface['name']
            regex_matched = re.search(regex_str, phy_interface_name)
            do_not_add_in_payload = False

            if (regex_matched or 'lo0' in phy_interface_name):
                if not regex_matched:
                    # lo0 interface, but not in user's regex input
                    do_not_add_in_payload = True
                else:
                    phy_interface_payload = {
                        "physical_interface_name": phy_interface_name}
                    phy_interfaces_payloads.append(phy_interface_payload)

                log_intf_unit = phy_interface.get('unit')
                if log_intf_unit:
                    if isinstance(log_intf_unit, dict):
                        log_units = [log_intf_unit]
                    else:
                        log_units = log_intf_unit
                    for log_unit in log_units:
                        log_interface_payload = {
                            "physical_interface_name": phy_interface_name,
                            "logical_interface_name": log_unit['name']}

                        log_unit_fmly = log_unit.get('family')
                        if log_unit_fmly:
                            if log_unit_fmly.get('ethernet-switching'):
                                log_type = "l2"
                            else:
                                log_type = "l3"
                                if log_unit_fmly.get('inet') and \
                                        log_unit_fmly['inet'].get('address') \
                                        and phy_interface_name == 'lo0':
                                    if isinstance(
                                            log_unit_fmly['inet']['address'],
                                            dict):
                                        lo0_add_list =\
                                            [log_unit_fmly['inet']['address']]
                                    else:
                                        lo0_add_list = \
                                            log_unit_fmly['inet']['address']
                                    for lo0_add in lo0_add_list:
                                        if lo0_add['name'] != '127.0.0.1/32'\
                                                and not lo0_found:
                                            lo0_ip_add = \
                                                lo0_add['name'].split('/')[0]
                                            lo0_found = True
                            log_interface_payload['logical_interface_type']\
                                = log_type

                        vlan_id = log_unit.get('vlan-id')
                        if vlan_id:
                            log_interface_payload[
                                'logical_interface_vlan_tag']\
                                = vlan_id
                        if not do_not_add_in_payload:
                            log_interfaces_payloads.append(
                                log_interface_payload)

        return {"phy_interfaces_payload": phy_interfaces_payloads,
                "log_interfaces_payload": log_interfaces_payloads,
                "lo_interface_ip": lo0_ip_add}

