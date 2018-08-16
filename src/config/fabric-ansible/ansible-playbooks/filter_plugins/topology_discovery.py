#!/usr/bin/python

from job_manager.topology_discovery_common import TopologyDiscoveryBasePlugin
from job_manager.filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log


class FilterModule(TopologyDiscoveryBasePlugin):
    def filters(self):
        return {
            'topology_discovery': self.topology_discovery,
        }

    def __init__(self):
        self.register_parser_method("juniper", "junos", self.parse_juniper_data)
        self.register_parser_method("juniper", "junos-qfx", self.parse_juniper_data)
        self.register_parser_method("cisco", "ios-fmly", self.parse_cisco_ios_data)
        self.register_parser_method("arista", "eos-fmly", self.parse_arista_eos_data)

    def parse_juniper_data(self, device_data, prouter_fqname):

        neighbor_info_list = []
        err_msg_list = []

        lldp_neighbors = device_data['output'][0]['rpc-reply']['lldp-neighbors-information']
        lldp_neighbor_info = lldp_neighbors.get('lldp-neighbor-information')
        if lldp_neighbor_info is None:
            return {
                     "do_more_parsing": False,
                     "neighbor_info_list": neighbor_info_list,
                     "err_msg_list": err_msg_list
                   }
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
                remote_prouter_fqname = ["default-global-system-config",
                                         lldp_neighbor.get(
                                             'lldp-remote-system-name')]
                phy_int_fqname = []
                phy_int_fqname.extend(prouter_fqname)
                local_phy_int = lldp_neighbor.get('lldp-local-port-id') \
                    or lldp_neighbor.get('lldp-local-interface')
                phy_int_fqname.append(local_phy_int.replace(":", "_"))

                if needs_parsing:
                    # ensure local interface is not a logical interface,
                    # remote interface need not be checked as port no
                    # won't be imported
                    if '.' not in phy_int_fqname[-1]:
                        neighbor_pair = (phy_int_fqname,
                                         remote_prouter_fqname +
                                         ":" + lldp_neighbor.get(
                                             'lldp-remote-port-id')
                                        )
                else:
                    lldp_remote_neighbor_port_desc = lldp_neighbor.get(
                        'lldp-remote-port-description').split(
                        "interface")[-1].strip()
                    lldp_neighbor_fqname = []
                    lldp_neighbor_fqname.extend(remote_prouter_fqname)
                    lldp_neighbor_fqname.append(
                        lldp_remote_neighbor_port_desc.replace(":", "_"))
                    # ensure local interface and remote interfaces are
                    # not logical interfaces
                    if '.' not in phy_int_fqname[-1]\
                            and '.' not in lldp_neighbor_fqname[-1]:
                        neighbor_pair = (phy_int_fqname, lldp_neighbor_fqname)
                        neighbor_info_list.append(neighbor_pair)

            except Exception as ex:
                err_msg_list.append(str(ex))

        return {
                "neighbor_info_list": neighbor_info_list,
                "do_more_parsing": needs_parsing,
                "err_msg_list": err_msg_list}

    def parse_cisco_ios_data(self, device_data, prouter_fqname):
        _task_log("parsed cisco ios with: "+device_data)

    def parse_arista_eos_data(self, device_data, prouter_fqname):
        _task_log("parsed arista eos with: "+device_data)
