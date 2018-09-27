#!/usr/bin/python

import re


class FilterModule(object):
    def filters(self):
        return {
            'junos_interface_info': self.junos_rt_intf_filter,
            'junos_conf_interface_info': self.junos_cf_intf_filter,
        }

    def junos_rt_intf_filter(self, interface_list_new,
                             prouter_name, regex_str=".*"):

        """This filter takes the inputs as the prouter_name
         and the list of runtime interfaces obtained by running
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
         "phy_interfaces_payload": [{"parent_type": "physical-router",
                                     "physical_interface_port_id": 533,
                                     "fq_name": [
                                         "default-global-system-config",
                                         prouter_name,
                                         phy_interface_name
                                     ],
                                     "display_name": physical_intf_name},
                                      {<another payload>}],
         "log_interfaces_payload": [{"parent_type": "physical-interface",
                                     "fq_name": [
                                         "default-global-system-config",
                                         prouter_name,
                                         phy_interface_name,
                                         log_interface_name
                                     ],
                                     "display_name": logical_intf_name,
                                     "logical_interface_type": "l3"},
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
            physical_intf_name = phy_interface['name']
            if re.search(regex_str, physical_intf_name):
                phy_interface_name = physical_intf_name.replace(':', '_')
                phy_interface_payload = {
                    "parent_type": "physical-router",
                    "fq_name": [
                        "default-global-system-config",
                        prouter_name,
                        phy_interface_name
                    ],
                    "physical_interface_port_id": phy_interface.get('snmp-index'),
                    "display_name": physical_intf_name}
                phy_int_mac_address = phy_interface.get(
                    'current-physical-address')
                if phy_int_mac_address:
                    phy_interface_payload['physical_interface_mac_addresses']\
                        = {"mac_address": [phy_int_mac_address]}
                phy_interfaces_payloads.append(phy_interface_payload)

                log_intfs = phy_interface.get('logical-interface')
                if log_intfs:
                    if isinstance(log_intfs, dict):
                        log_units = [log_intfs]
                    else:
                        log_units = log_intfs

                    for log_unit in log_units:
                        log_interface_payload = {
                            "parent_type": "physical-interface",
                            "fq_name": ["default-global-system-config",
                                        prouter_name,
                                        phy_interface_name,
                                        log_unit['name'].replace(':', '_')],
                            "display_name": log_unit['name']}

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
                             prouter_name, regex_str=".*"):
        """
        This filter takes the inputs as the prouter_name and
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
         "phy_interfaces_payload": [{"parent_type": "physical-router",
                                     "fq_name": [
                                         "default-global-system-config",
                                         prouter_name,
                                         phy_interface_name
                                     ],
                                     "display_name": physical_intf_name},
                                     {<another payload>}],
         "log_interfaces_payload": [{"parent_type": "physical-interface",
                                     "fq_name": [
                                         "default-global-system-config",
                                         prouter_name,
                                         phy_interface_name,
                                         log_interface_name
                                     ],
                                     "display_name": logical_intf_name,
                                     "logical_interface_type": "l3",
                                     "logical_interface_vlan_tag": "11"},
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
            phy_interface_name = phy_interface['name'].replace(':', '_')
            regex_matched = re.search(regex_str, phy_interface['name'])
            do_not_add_in_payload = False

            if regex_matched or 'lo0' in phy_interface['name']:
                if not regex_matched:
                    # lo0 interface, but not in user's regex input
                    do_not_add_in_payload = True
                else:
                    phy_interface_payload = {
                        "parent_type": "physical-router",
                        "fq_name": [
                            "default-global-system-config",
                            prouter_name,
                            phy_interface_name
                        ],
                        "display_name": phy_interface['name']}
                    phy_interfaces_payloads.append(phy_interface_payload)

                log_intf_unit = phy_interface.get('unit')
                if log_intf_unit:
                    if isinstance(log_intf_unit, dict):
                        log_units = [log_intf_unit]
                    else:
                        log_units = log_intf_unit
                    for log_unit in log_units:
                        log_interface_payload = {
                            "parent_type": "physical-interface",
                            "fq_name": ["default-global-system-config",
                                        prouter_name,
                                        phy_interface_name,
                                        phy_interface_name +
                                        "." + log_unit['name']],
                            "display_name":
                                phy_interface['name'] +
                                "." + log_unit['name']}

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
                        phy_intf_native_vlan_id = phy_interface.get('native-vlan-id')
                        if vlan_id:
                            add_vlan_to_payload = self.chk_vlan_id(
                                vlan_id, phy_intf_native_vlan_id)
                            if add_vlan_to_payload:
                                log_interface_payload[
                                    'logical_interface_vlan_tag']\
                                    = vlan_id
                        if not do_not_add_in_payload:
                            log_interfaces_payloads.append(
                                log_interface_payload)

        return {"phy_interfaces_payload": phy_interfaces_payloads,
                "log_interfaces_payload": log_interfaces_payloads,
                "lo_interface_ip": lo0_ip_add}

    def chk_vlan_id(self, vlan_id, phy_intf_native_vlan_id):
        try:
            if int(vlan_id) < 1 or int(vlan_id) > 4094:
                return False

            if vlan_id == phy_intf_native_vlan_id:
                return False

        except ValueError:
            return False

        return True

