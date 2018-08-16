#!/usr/bin/python

import sys
sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")

from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log


class ParserModule(object):

    def parse_juniper_junos_qfx(self, device_data):
        return self.parse_juniper_device_data(device_data)

    def parse_juniper_junos(self, device_data):
        return self.parse_juniper_device_data(device_data)

    def parse_juniper_device_data(self, device_data):

        neighbor_info_list = []
        err_msg_list = []

        lldp_neighbors = device_data['parsed_output']['lldp-neighbors-information']
        if not lldp_neighbors:
            return {
                     "do_more_parsing": False,
                     "neighbor_info_list": neighbor_info_list,
                     "err_msg_list": err_msg_list
                   }
        lldp_neighbor_info = lldp_neighbors.get('lldp-neighbor-information')
        if isinstance(lldp_neighbor_info, dict):
            lldp_neighbor_info = [lldp_neighbor_info]

        needs_parsing = True
        # pilot sequence to determine what the structure of
        # each lldp_neighbor_info is like
        if 'lldp-remote-port-description' in lldp_neighbor_info[0]:
            needs_parsing = False

        for lldp_neighbor in lldp_neighbor_info:
            try:
                if lldp_neighbor.get('lldp-remote-system-name') is None:
                    continue
                remote_prouter_name = lldp_neighbor.get(
                                          'lldp-remote-system-name')
                local_phy_int = lldp_neighbor.get('lldp-local-port-id') \
                    or lldp_neighbor.get('lldp-local-interface')

                if needs_parsing:
                    # ensure local interface is not a logical interface,
                    # remote interface need not be checked as port no
                    # won't be imported
                    if '.' not in local_phy_int:
                        neighbor_pair_info = {
                                               "local_physical_interface_name": local_phy_int,
                                               "remote_device_name": remote_prouter_name,
                                               "remote_physical_interface_name": lldp_neighbor.get(
                                                                                     'lldp-remote-port-id')
                                             }
                        neighbor_info_list.append(neighbor_pair_info)
                else:
                    lldp_remote_neighbor_port_desc = lldp_neighbor.get(
                        'lldp-remote-port-description').split(
                        "interface")[-1].strip()
                    # ensure local interface and remote interfaces are
                    # not logical interfaces
                    if '.' not in local_phy_int\
                            and '.' not in lldp_remote_neighbor_port_desc:
                        neighbor_pair_info = {
                                               "local_physical_interface_name": local_phy_int,
                                               "remote_device_name": remote_prouter_name,
                                               "remote_physical_interface_name": lldp_remote_neighbor_port_desc
                                             }
                        neighbor_info_list.append(neighbor_pair_info)

            except Exception as ex:
                _task_error_log(str(ex))
                err_msg_list.append(str(ex))

        return {
                "neighbor_info_list": neighbor_info_list,
                "do_more_parsing": needs_parsing,
                "err_msg_list": err_msg_list}

