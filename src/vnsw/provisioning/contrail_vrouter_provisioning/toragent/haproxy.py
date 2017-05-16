#!/usr/bin/python
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import json
import ConfigParser

from contrail_vrouter_provisioning.base import ContrailSetup

class TorAgentHaproxyConfig(ContrailSetup):
    def __init__(self, args_str = None):
        super(TorAgentHaproxyConfig, self).__init__()
        self.parse_args(args_str)
    # end __init__

    def parse_args(self, args_str):
        '''
        Eg. contrail-haproxy-toragent --tor_agent_dict dict
        '''
        #self._args = None
        #parser = argparse.ArgumentParser()
        #parser.add_argument("--tor_agent_config", type=str,
        #                    help = "json string having tor agent dictionary, "
        #                           "as defined in inventory")
        #self._args = parser.parse_args(args_str)

        self.tor_agent_dict = None
        if not args_str:
            args_str = ' '.join(sys.argv[2:])
        if sys.argv[1] == "--tor_agent_config" and args_str:
            self.tor_agent_dict = eval(args_str)
        else:
            print >> sys.stderr, "Invalid Arguments"
    # end parse_args

    # Get a list of nodes on which tor agents are running
    def get_toragent_nodes(self):
        node_list = []
        for host in self.tor_agent_dict:
            node_list.append(host)
        return node_list
    #end get_toragent_nodes

    # Given a host_string and tor_name, return the standby tor-agent info
    # identified by index and host-string of tor-agent
    def get_standby_info(self, skip_host, match_tor_name):
        tor_agent_host_list = self.get_toragent_nodes()
        for host in tor_agent_host_list:
            if host == skip_host:
                continue
            for i in range(len(self.tor_agent_dict[host])):
                tor_name= self.tor_agent_dict[host][i]['tor_name']
                if tor_name == match_tor_name:
                    return (i, host)
        return (-1, None)
    #end get_standby_info

    def make_key(self, tsn1, tsn2):
        if tsn1 < tsn2:
            return tsn1 + "-" + tsn2
        return tsn2 + "-" + tsn1

    # Get HA proxy configuration for a TOR agent
    def get_tor_agent_haproxy_config(self, proxy_name, key, ha_dict):
        tor_agent_ha_config = '\n'
        port_list = ha_dict[key]
        ha_dict_len = len(port_list)
        if ha_dict_len == 0:
            return tor_agent_ha_config
        ip2 = None
        if "-" in key:
            ip1 = key.split('-')[0]
            ip2 = key.split('-')[1]
        else:
            ip1 = key
        tor_agent_ha_config = tor_agent_ha_config + 'listen %s\n' %(proxy_name)
        tor_agent_ha_config = tor_agent_ha_config + '    option tcpka\n'
        tor_agent_ha_config = tor_agent_ha_config + '    mode tcp\n'
        tor_agent_ha_config = tor_agent_ha_config + '    bind :%s' %(port_list[0])
        for i in range(1, ha_dict_len):
            tor_agent_ha_config = tor_agent_ha_config + ',:%s' %(port_list[i])
        tor_agent_ha_config = tor_agent_ha_config + '\n'
        tor_agent_ha_config = tor_agent_ha_config + '    server %s %s check inter 2000\n' %(ip1, ip1)
        if ip2 != None:
            tor_agent_ha_config = tor_agent_ha_config + '    server %s %s check inter 2000\n' %(ip2, ip2)
        tor_agent_ha_config = tor_agent_ha_config + '\n'
        return tor_agent_ha_config
    #end get_tor_agent_haproxy_config

    # Get HA proxy configuration for all TOR agents
    def get_all_tor_agent_haproxy_config(self):
        master_standby_dict = {}
        tor_agent_host_list = self.get_toragent_nodes()
        for host in tor_agent_host_list:
            for i in range(len(self.tor_agent_dict[host])):
                tor_name= self.tor_agent_dict[host][i]['tor_name']
                tsn1 = self.tor_agent_dict[host][i]['tor_tsn_ip']
                port1 = self.tor_agent_dict[host][i]['tor_ovs_port']
                standby_tor_idx, standby_host = self.get_standby_info(host, tor_name)
                key = tsn1
                if (standby_tor_idx != -1 and standby_host != None):
                    tsn2 = self.tor_agent_dict[standby_host][standby_tor_idx]['tor_tsn_ip']
                    port2 = self.tor_agent_dict[standby_host][standby_tor_idx]['tor_ovs_port']
                    if port1 == port2:
                        key = self.make_key(tsn1, tsn2)
                    else:
                        print >> sys.stderr, "Tor Agents (%s, %d) and (%s, %d) \
                                 are configured as redundant agents but don't  \
                                 have same ovs_port" \
                                 %(host, i, standby_host, standby_tor_idx)
                if not key in master_standby_dict:
                    master_standby_dict[key] = []
                if not port1 in master_standby_dict[key]:
                    master_standby_dict[key].append(port1)
        i = 1
        cfg_str = ""
        for key in master_standby_dict.keys():
            proxy_name = "contrail-tor-agent-" + str(i)
            i = i + 1
            cfg_str = cfg_str + self.get_tor_agent_haproxy_config(proxy_name, key, master_standby_dict)
        return cfg_str

def main(args_str=None):
    tor_agent_haproxy = TorAgentHaproxyConfig(args_str)
    haproxy_config = tor_agent_haproxy.get_all_tor_agent_haproxy_config()
    print haproxy_config

if __name__ == "__main__":
    main()
