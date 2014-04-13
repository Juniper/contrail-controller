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
#include <vgw/vgw.h>

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

        if (vrf == "" || interface == "" || subnets.size() == 0) {
            return;
        }

        std::sort(subnets.begin(), subnets.end());
        std::sort(routes.begin(), routes.end());
        table_.insert(VirtualGatewayConfig(interface, vrf, subnets, routes));
    }
    return;
}

void VirtualGatewayConfigTable::Shutdown() {
}

// Add / modify a virtual gateway
void VirtualGatewayConfigTable::AddVgw(
                               const std::string &interface,
                               const std::string &vrf,
                               VirtualGatewayConfig::SubnetList &subnets,
                               VirtualGatewayConfig::SubnetList &routes) {
    std::sort(subnets.begin(), subnets.end());
    std::sort(routes.begin(), routes.end());
    Table::iterator it = table_.find(interface);
    if (it == table_.end()) {
        // Add new gateway
        table_.insert(VirtualGatewayConfig(interface, vrf, subnets, routes));
        agent_->vgw()->CreateVrf(vrf);
        agent_->vgw()->CreateInterface(interface, vrf);
    } else {
        // modify existing gateway
        if (vrf != it->vrf()) {
            LOG(DEBUG, "Gateway : change of vrf is not allowed; " <<
                "Interface : " << interface << " Old VRF : " << it->vrf() <<
                " New VRF : " << vrf);
            return;
        }
        VirtualGatewayConfig::SubnetList add_list, del_list;
        if (FindChange(it->subnets(), subnets, add_list, del_list)) {
            agent_->vgw()->SubnetUpdate(*it, add_list, del_list);
            it->set_subnets(subnets);
        }

        add_list.clear();
        del_list.clear();
        if (FindChange(it->routes(), routes, add_list, del_list)) {
            agent_->vgw()->RouteUpdate(*it, routes, add_list, del_list, true);
            it->set_routes(routes);
        }
    }
}

// Delete a virtual gateway
void VirtualGatewayConfigTable::DeleteVgw(const std::string &interface) {
    Table::iterator it = table_.find(interface);
    if (it != table_.end()) {
        VirtualGatewayConfig::SubnetList empty_list;
        agent_->vgw()->SubnetUpdate(*it, empty_list, it->subnets());
        agent_->vgw()->RouteUpdate(*it, empty_list, empty_list,
                                   it->routes(), false);
        agent_->vgw()->DeleteInterface(interface);
        agent_->vgw()->DeleteVrf(it->vrf());
        table_.erase(it);
    }
}

bool VirtualGatewayConfigTable::FindChange(
                                const VirtualGatewayConfig::SubnetList &old_subnets,
                                const VirtualGatewayConfig::SubnetList &new_subnets,
                                VirtualGatewayConfig::SubnetList &add_list,
                                VirtualGatewayConfig::SubnetList &del_list) {
    bool change = false;
    VirtualGatewayConfig::SubnetList::const_iterator it_old = old_subnets.begin();
    VirtualGatewayConfig::SubnetList::const_iterator it_new = new_subnets.begin();
    while (it_old != old_subnets.end() && it_new != new_subnets.end()) {
        if (*it_old < *it_new) {
            // old entry is deleted
            del_list.push_back(*it_old);
            change = true;
            it_old++;
        } else if (*it_new < *it_old) {
            // new entry
            add_list.push_back(*it_new);
            change = true;
            it_new++;
        } else {
            // no change in entry
            it_old++;
            it_new++;
        }   
    }   

    // delete remaining old entries
    for (; it_old != old_subnets.end(); ++it_old) {
        del_list.push_back(*it_old);
        change = true;
    }

    // add remaining new entries
    for (; it_new != new_subnets.end(); ++it_new) {
        add_list.push_back(*it_new);
        change = true;
    }

    return change;
}
