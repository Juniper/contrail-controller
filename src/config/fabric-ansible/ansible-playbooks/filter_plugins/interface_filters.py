#!/usr/bin/python

import re

class FilterModule(object):
    def filters(self):
        return {
            'junos_interface_info': self.junos_rt_intf_filter,
            'junos_conf_interface_info': self.junos_cf_intf_filter,
        }

    def junos_rt_intf_filter(self, interface_list_new, prouter_name, regex_str=".*"):
        if not regex_str:
            regex_str = ".*"

        phy_interfaces_payloads = []
        log_interfaces_payloads = []

        for phy_interface in interface_list_new:
            physical_intf_name = phy_interface['name']
            if re.search(regex_str, physical_intf_name):
                phy_interface_name = physical_intf_name.replace(':', '_')
                phy_interface_payload = {"parent_type": "physical-router",
                                         "fq_name": ["default-global-system-config",
                                                     prouter_name,
                                                     phy_interface_name
                                                    ],
                                        "display_name": physical_intf_name}

                phy_interfaces_payloads.append(phy_interface_payload)

                log_intfs = phy_interface.get('logical-interface')
                if log_intfs:
                    if isinstance(log_intfs, dict):
                        log_units = [log_intfs]
                    else:
                        log_units = log_intfs

                    for log_unit in log_units:
                        log_interface_payload = {"parent_type":"physical-interface",
                                                 "fq_name": ["default-global-system-config",
                                                             prouter_name,
                                                             phy_interface_name,
                                                             log_unit['name'].replace(':', '_')
                                                            ],
                                                 "port_id": phy_interface['snmp-index'],
                                                 "display_name": log_unit['name']}

                        add_fmly = log_unit.get('address-family')
                        if add_fmly:
                            log_type = "l3"
                            if isinstance(add_fmly, dict):
                                address_fmly_list = [add_fmly]
                            else:
                                address_fmly_list = add_fmly
                            for address_fmly in address_fmly_list:
                                if address_fmly['address-family-name'] == 'eth-switch':
                                    log_type = "l2"
                            log_interface_payload['logical_interface_type'] = log_type
                        log_interfaces_payloads.append(log_interface_payload)

        return {"phy_interfaces_payload": phy_interfaces_payloads,
                "log_interfaces_payload": log_interfaces_payloads}

    def junos_cf_intf_filter(self, interface_list, prouter_name, regex_str=".*"):
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

            if (regex_matched or 'lo0' in phy_interface['name']):
                if not regex_matched:
                    # lo0 interface, but not in user's regex input
                    do_not_add_in_payload = True
                else:
                    phy_interface_payload = {"parent_type": "physical-router",
                                             "fq_name": ["default-global-system-config",
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
                        log_interface_payload = {"parent_type":"physical-interface",
                                                 "fq_name": ["default-global-system-config",
                                                             prouter_name,
                                                             phy_interface_name,
                                                             phy_interface_name+"."+log_unit['name']
                                                            ],
                                                 "display_name": phy_interface['name']+
                                                                 "."+log_unit['name']}

                        log_unit_fmly = log_unit.get('family')
                        if log_unit_fmly:
                            if log_unit_fmly.get('ethernet-switching'):
                                log_type = "l2"
                            else:
                                log_type = "l3"
                                if log_unit_fmly.get('inet') and \
                                        log_unit_fmly['inet'].get('address') \
                                        and phy_interface_name == 'lo0':
                                    if isinstance(log_unit_fmly['inet']['address'], dict):
                                        lo0_add_list = [log_unit_fmly['inet']['address']]
                                    else:
                                        lo0_add_list = log_unit_fmly['inet']['address']
                                    for lo0_add in lo0_add_list:
                                        if lo0_add['name'] != '127.0.0.1/32' and not lo0_found:
                                            lo0_ip_add = lo0_add['name'].split('/')[0]
                                            lo0_found = True
                            log_interface_payload['logical_interface_type'] = log_type

                        vlan_id = log_unit.get('vlan-id')
                        if vlan_id:
                            log_interface_payload['logical_interface_vlan_tag'] = vlan_id
                        if not do_not_add_in_payload:
                            log_interfaces_payloads.append(log_interface_payload)

        return {"phy_interfaces_payload": phy_interfaces_payloads,
                "log_interfaces_payload": log_interfaces_payloads,
                "lo_interface_ip": lo0_ip_add}
