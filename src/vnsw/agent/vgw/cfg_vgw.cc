/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/stat.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <vgw/cfg_vgw.h>

using namespace std;

bool VirtualGatewayConfigTable::VgwCfgItems(const string &input, string &vrf, 
        string &interface, Ip4Address &addr, int &plen) {
    vector<string> elements;
    if (!stringToIntegerList(input, "/", elements)) {
        return false;
    }
    if (elements.size() != 4) {
        return false;
    }
    vector<string>::const_iterator item = elements.begin();
    vrf = *item;
    ++item;
    interface = *item;
    ++item;
    string ip = *item;
    ++item;
    stringToInteger(*item, plen);
    boost::system::error_code ec;
    addr = Ip4Address::from_string(ip, ec);
    if (ec.value() != 0) {
        return false;
    }
    return true;
}

// Config init. Read the "gateway" node and add the configuration
void VirtualGatewayConfigTable::Init
    (const boost::program_options::variables_map &var_map) {
    string vrf, interface;
    Ip4Address addr;
    int plen;

    if (!var_map.count("GATEWAY.vn-interface-subnet")) {
        return;
    }
    const vector<string> &gw_subnet_list = 
        var_map["GATEWAY.vn-interface-subnet"].as< vector<std::string> >();
    vector<string>::const_iterator it = gw_subnet_list.begin();
    while (it != gw_subnet_list.end()) {
        string str = *it;
        ++it;
        if (!VgwCfgItems(str, vrf, interface, addr, plen)) {
            continue;
        }
        Table::iterator cfg_it = table_.find(interface);
        if (cfg_it != table_.end()) {
            VirtualGatewayConfig &entry = cfg_it->second;
            VirtualGatewayConfig::SubnetList &slist = entry.subnets();
            slist.push_back(VirtualGatewayConfig::Subnet(addr, plen));
        } else {
            VirtualGatewayConfig::SubnetList slist;
            slist.push_back(VirtualGatewayConfig::Subnet(addr, plen));
            table_.insert(VgwCfgPair(interface, VirtualGatewayConfig(interface, vrf, slist)));
        }

    }
    if (var_map.count("GATEWAY.vn-interface-route")) {
        const vector<string> &gw_route_list = 
            var_map["GATEWAY.vn-interface-route"].as< vector<std::string> >();
        it = gw_route_list.begin();
        while (it != gw_route_list.end()) {
            string str = *it;
            ++it;
            if (!VgwCfgItems(str, vrf, interface, addr, plen)) {
                continue;
            }
            Table::iterator cfg_it = table_.find(interface);
            if (cfg_it == table_.end()) {
                continue;
            }
            VirtualGatewayConfig &entry = cfg_it->second;
            VirtualGatewayConfig::SubnetList &rlist = entry.routes();
            rlist.push_back(VirtualGatewayConfig::Subnet(addr, plen));
        }
    }

}

void VirtualGatewayConfigTable::Shutdown() {
}
