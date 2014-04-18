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
        table_.insert(VirtualGatewayConfig(interface, vrf,
                                           subnets, routes, (uint32_t) -1));
    }
    return;
}

void VirtualGatewayConfigTable::Shutdown() {
    work_queue_.Shutdown();
}

void VirtualGatewayConfigTable::Enqueue(
                                boost::shared_ptr<VirtualGatewayData> request) {
    work_queue_.Enqueue(request);
}

bool VirtualGatewayConfigTable::ProcessRequest(
                                boost::shared_ptr<VirtualGatewayData> request) {
    switch(request->message_type_) {
        case VirtualGatewayData::Add:
            BOOST_FOREACH(VirtualGatewayInfo &vgw, request->vgw_list_) {
                AddVgw(vgw, request->version_);
            }
            break;

        case VirtualGatewayData::Delete:
            BOOST_FOREACH(VirtualGatewayInfo &vgw, request->vgw_list_) {
                DeleteVgw(vgw.interface_name_);
            }
            break;

        case VirtualGatewayData::Audit:
            DeleteAllOldVersionVgw(request->version_);
            break;

        default:
            assert(0);
    }
    return true;
}

// Add / modify a virtual gateway
bool VirtualGatewayConfigTable::AddVgw(VirtualGatewayInfo &vgw, uint32_t version) {
    std::sort(vgw.subnets_.begin(), vgw.subnets_.end());
    std::sort(vgw.routes_.begin(), vgw.routes_.end());
    Table::iterator it = table_.find(vgw.interface_name_);
    if (it == table_.end()) {
        // Add new gateway
        table_.insert(VirtualGatewayConfig(vgw.interface_name_, vgw.vrf_name_,
                                           vgw.subnets_, vgw.routes_, version));
        agent_->vgw()->CreateVrf(vgw.vrf_name_);
        agent_->vgw()->CreateInterface(vgw.interface_name_, vgw.vrf_name_);
    } else {
        // modify existing gateway
        if (vgw.vrf_name_ != it->vrf_name()) {
            LOG(DEBUG, "Virtual Gateway : change of vrf is not allowed; " <<
                "Interface : " << vgw.interface_name_ << " Old VRF : " <<
                it->vrf_name() << " New VRF : " << vgw.vrf_name_);
            return false;
        }
        VirtualGatewayConfig::SubnetList add_list, del_list;
        if (FindChange(it->subnets(), vgw.subnets_, add_list, del_list)) {
            agent_->vgw()->SubnetUpdate(*it, add_list, del_list);
            it->set_subnets(vgw.subnets_);
        }

        add_list.clear();
        del_list.clear();
        if (FindChange(it->routes(), vgw.routes_, add_list, del_list)) {
            agent_->vgw()->RouteUpdate(*it, vgw.routes_, add_list, del_list, true);
            it->set_routes(vgw.routes_);
        }
        it->set_version(version);
    }
    return true;
}

// Delete a virtual gateway
bool VirtualGatewayConfigTable::DeleteVgw(const std::string &interface_name) {
    Table::iterator it = table_.find(interface_name);
    if (it != table_.end()) {
        DeleteVgw(it);
        return true;
    }

    LOG(DEBUG, "Virtual Gateway delete : interface not present; " <<
        "Interface : " << interface_name);
    return false;
}

void VirtualGatewayConfigTable::DeleteVgw(Table::iterator it) {
    VirtualGatewayConfig::SubnetList empty_list;
    agent_->vgw()->SubnetUpdate(*it, empty_list, it->subnets());
    agent_->vgw()->RouteUpdate(*it, empty_list, empty_list,
                               it->routes(), false);
    agent_->vgw()->DeleteInterface(it->interface_name());
    agent_->vgw()->DeleteVrf(it->vrf_name());
    table_.erase(it);
}

// delete all entries from previous version
void VirtualGatewayConfigTable::DeleteAllOldVersionVgw(uint32_t version) {
    for (Table::iterator it = table_.begin(); it != table_.end();) {
        if (it->version() < version) {
            DeleteVgw(it++);
        } else {
            it++;
        }
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
