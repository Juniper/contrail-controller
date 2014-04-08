/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/stat.h>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/foreach.hpp>
#include <vector>
#include <pugixml/pugixml.hpp>
#include <base/logging.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <vgw/cfg_vgw.h>

using namespace std;
using namespace boost::property_tree;
using namespace boost::uuids;
using boost::optional;

// Config init. Read the "gateway" node and add the configuration
// Handle only one gateway config for now
void VirtualGatewayConfigTable::Init(const boost::property_tree::ptree pt) {
    const std::string gw_str = "GATEWAY";

    BOOST_FOREACH(const ptree::value_type &section, pt) {
        if (section.first.compare(0, gw_str.size(), gw_str) != 0) {
            continue;
        }
        string vrf = "";
        string interface = "";
        VirtualGatewayConfig::SubnetList subnets;
        VirtualGatewayConfig::SubnetList routes;
        BOOST_FOREACH(const ptree::value_type &key, section.second) {
            if (key.first.compare("routing_instance") == 0) {
                vrf = key.second.get_value<string>();
            }
            if (key.first.compare("interface") == 0) {
                interface = key.second.get_value<string>();
            }
            if (key.first.compare("ip_blocks") == 0) {
                BuildSubnetList(key.second.get_value<string>(), subnets);
            }
            if (key.first.compare("routes") == 0) {
                BuildSubnetList(key.second.get_value<string>(), routes);
            }
        }
        if (vrf == "" || interface == "" || subnets.size() == 0) {
            return;
        }

        table_.insert(VirtualGatewayConfig(interface, vrf, subnets, routes));
    }
    return;
}

void VirtualGatewayConfigTable::BuildSubnetList
    (const string &subnets, VirtualGatewayConfig::SubnetList &results) {
    Ip4Address addr;
    int plen;
    boost::system::error_code ec;
    results.clear();
    if (!subnets.empty()) {
        vector<string> tokens;
        boost::split(tokens, subnets, boost::is_any_of(","));
        vector<string>::iterator it = tokens.begin();
        while (it != tokens.end()) {
            std::string str = *it;
            boost::algorithm::trim(str);
            ++it;
            ec = Ip4PrefixParse(str, &addr, &plen);
            if (ec != 0 || plen >= 32) {
                continue;
            }
            results.push_back(VirtualGatewayConfig::Subnet(addr, plen));
        }
    }
}

void VirtualGatewayConfigTable::Shutdown() {
}
