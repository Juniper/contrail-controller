/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/stat.h>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/foreach.hpp>

#include <pugixml/pugixml.hpp>
#include <base/logging.h>

#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <vgw/cfg_vgw.h>

using namespace std;
using namespace boost::property_tree;
using namespace boost::uuids;
using boost::optional;

static string FileRead(const char *init_file) {
    ifstream ifs(init_file);
    string content ((istreambuf_iterator<char>(ifs) ),
                    (istreambuf_iterator<char>() ));
    return content;
}

// Config init. Read the "gateway" node and add the configuration
// Handle only one gateway config for now
void VirtualGatewayConfigTable::Init(const char *init_file) {
    boost::system::error_code ec;

    string str = FileRead(init_file);
    istringstream sstream(str);
    ptree tree;

    try {
        read_xml(sstream, tree, xml_parser::trim_whitespace);
    } catch (exception &e) {
        LOG(ERROR, "Error reading config file <" << init_file 
            << ">. XML format error??? <" << e.what() << ">");
        exit(EINVAL);
    } 

    ptree config;
    try {
        config = tree.get_child("config");
    } catch (exception &e) {
        LOG(ERROR, "Error reading \"config\" node in config file <" 
            << init_file << ">. Error <" << e.what() << ">");
        exit(EINVAL);
    }

    ptree agent;
    try {
        agent = config.get_child("agent");
    } catch (exception &e) {
        LOG(ERROR, init_file << " : Error reading \"agent\" node in config "
            "file. Error <" << e.what() << ">");
        exit(EINVAL);
    }

    for (ptree::const_iterator iter = agent.begin(); iter != agent.end();
         ++iter) {
        if (iter->first != "gateway") {
            continue;
        }

        optional<string> opt_str;
        ptree node = iter->second;
        
        string vrf = "";
        string interface = "";
        try {
            opt_str = iter->second.get<string>("<xmlattr>.virtual-network");
            if (!opt_str) {
                opt_str = iter->second.get<string>
                    ("<xmlattr>.routing-instance");
            }
            if (!opt_str) {
                continue;
            }
            vrf = opt_str.get();

            opt_str = node.get<string>("interface");
            if (!opt_str) {
                continue;
            }
            interface = opt_str.get();
        } catch (exception &e) {
            LOG(ERROR, "Error reading \"gateway\" node in config file <"
                << init_file << ">. Error <" << e.what() << ">");
            continue;
        }

        // Iterate thru all subnets in gateway
        VirtualGatewayConfig::SubnetList subnets;
        VirtualGatewayConfig::SubnetList routes;
        Ip4Address addr;
        int plen;
        BOOST_FOREACH(ptree::value_type &v, node) {
            optional<string> subnet_str;
            try {
                if (v.first == "subnet" || v.first == "route") {
                    if (subnet_str = v.second.get<string>("")) {
                        ec = Ip4PrefixParse(subnet_str.get(), &addr, &plen);
                        if (ec != 0 || plen >= 32) {
                            continue;
                        }

                        if (v.first == "subnet")
                            subnets.push_back(VirtualGatewayConfig::Subnet
                                              (addr, plen));
                        else
                            routes.push_back(VirtualGatewayConfig::Subnet
                                              (addr, plen));
                    }
                }
            } catch (exception &e) {
                LOG(ERROR, "Error reading \"gateway\" or \"route\" node in "
                    "config file <" << init_file << ">. Error <" << e.what() 
                    << ">");
                continue;
            }
        }

        if (vrf == "" || interface == "" ) {
            return;
        }

        table_.insert(VirtualGatewayConfig(interface, vrf, subnets, routes));
    }
    return;
}

void VirtualGatewayConfigTable::Shutdown() {
}
