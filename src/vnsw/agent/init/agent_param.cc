/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * Agent parameters are derived from 3 entities in increasing priority,
 * - System information
 * - Configuration file
 * - Parameters
 */

#include "base/os.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#include <iostream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>

#include <base/logging.h>
#include <base/misc_utils.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <vgw/cfg_vgw.h>

using namespace std;
using namespace boost::property_tree;
using boost::optional;
namespace opt = boost::program_options;

template <typename ValueType>
bool AgentParam::GetOptValue
    (const boost::program_options::variables_map &var_map, ValueType &var,
     const std::string &val) {
    return GetOptValueImpl(var_map, var, val,
        static_cast<ValueType *>(0));
}

template <typename ValueType>
bool AgentParam::GetOptValueImpl
    (const boost::program_options::variables_map &var_map,
     ValueType &var, const std::string &val, ValueType*) {
    // Check if the value is present.
    if (var_map.count(val) && !var_map[val].defaulted()) {
        var = var_map[val].as<ValueType>();
        return true;
    }
    return false;
}

template <typename ElementType>
bool AgentParam::GetOptValueImpl(
    const boost::program_options::variables_map &var_map,
    std::vector<ElementType> &var, const std::string &val,
    std::vector<ElementType>*) {
    // Check if the value is present.
    if (var_map.count(val) && !var_map[val].defaulted()) {
        std::vector<ElementType> tmp(
            var_map[val].as<std::vector<ElementType> >());
        // Now split the individual elements
        for (typename std::vector<ElementType>::const_iterator it = 
                 tmp.begin();
             it != tmp.end(); it++) {
            std::stringstream ss(*it);
            std::copy(istream_iterator<ElementType>(ss),
                istream_iterator<ElementType>(),
                std::back_inserter(var));
        }
        return true;
    }
    return false;
}

template <typename ValueType>
bool AgentParam::GetValueFromTree(ValueType &var, const std::string &val) {

    optional<ValueType> opt;

    if (opt = tree_.get_optional<ValueType>(val)) {
        var = opt.get();
        return true;
    } 
    return false;
}

bool AgentParam::GetIpAddress(const string &str, Ip4Address *addr) {
    boost::system::error_code ec;
    Ip4Address tmp = Ip4Address::from_string(str, ec);
    if (ec.value() != 0) {
        return false;
    }
    *addr = tmp;
    return true;
}

bool AgentParam::ParseIp(const string &key, Ip4Address *server) {
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>(key)) {
        Ip4Address addr;
        if (GetIpAddress(opt_str.get(), &addr) == false) {
            LOG(ERROR, "Error in config file <" << config_file_ 
                    << ">. Error parsing IP address from <" 
                    << opt_str.get() << ">");
            return false;

        } else {
            *server = addr;
        }
    }
    return true;
}

bool AgentParam::ParseServerList(const string &key, Ip4Address *server1,
                                 Ip4Address *server2) {
    optional<string> opt_str;
    Ip4Address addr;
    vector<string> tokens;
    if (opt_str = tree_.get_optional<string>(key)) {
        boost::split(tokens, opt_str.get(), boost::is_any_of(" \t"));
        if (tokens.size() > 2) {
            LOG(ERROR, "Error in config file <" << config_file_ 
                    << ">. Cannot have more than 2 servers <" 
                    << opt_str.get() << ">");
            return false;
        }
        vector<string>::iterator it = tokens.begin();
        if (it != tokens.end()) {
            if (!GetIpAddress(*it, server1)) {
                return false;
            }
            ++it;
            if (it != tokens.end()) {
                if (!GetIpAddress(*it, server2)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Parse address string in the form <ip>:<port>
bool AgentParam::ParseAddress(const std::string &addr_string,
                              Ip4Address *server, uint16_t *port) {
    vector<string> tokens;
    boost::split(tokens, addr_string, boost::is_any_of(":"));
    if (tokens.size() > 2) {
        cout << "Error in config file <" << config_file_
             << ">. Improper server address <" << addr_string << ">\n";
        return false;
    }
    vector<string>::iterator it = tokens.begin();
    if (!GetIpAddress(*it, server)) {
        cout << "Error in config file <" << config_file_
             << ">. Improper server address <" << addr_string << ">\n";
        return false;
    }
    ++it;
    if (it != tokens.end()) {
        stringToInteger(*it, *port);
    }

    return true;
}

// Parse list of servers in the <ip1>:<port1> <ip2>:<port2> format
bool AgentParam::ParseServerList(const std::string &key,
                                 Ip4Address *server1, uint16_t *port1,
                                 Ip4Address *server2, uint16_t *port2) {
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>(key)) {
        vector<string> tokens;
        boost::split(tokens, opt_str.get(), boost::is_any_of(" \t"));
        if (tokens.size() > 2) {
            cout << "Error in config file <" << config_file_
                 << ">. Cannot have more than 2 DNS servers <"
                 << opt_str.get() << ">\n";
            return false;
        }
        vector<string>::iterator it = tokens.begin();
        if (it != tokens.end()) {
            if (!ParseAddress(*it, server1, port1))
                return false;
            ++it;
            if (it != tokens.end()) {
                return ParseAddress(*it, server2, port2);
            }
        }
    }
    return true;
}

void AgentParam::ParseIpArgument
    (const boost::program_options::variables_map &var_map, Ip4Address &server,
     const string &key) {

    if (var_map.count(key)) {
        Ip4Address addr;
        if (GetIpAddress(var_map[key].as<string>(), &addr)) {
            server = addr;
        }
    }
}

bool AgentParam::ParseServerListArguments
    (const boost::program_options::variables_map &var_map, Ip4Address &server1,
     Ip4Address &server2, const string &key) {

    if (var_map.count(key)) {
        vector<string> value = var_map[key].as<vector<string> >();
        if (value.size() > 2) {
            cout << "Error in Arguments. Cannot have more than 2 servers for "
                 << key << "\n";
            return false;
        }
        vector<string>::iterator it = value.begin();
        Ip4Address addr;
        if (it !=  value.end()) {
            if (GetIpAddress(*it, &addr)) {
                server1 = addr;
            }
            ++it;
            if (it != value.end()) {
                if (GetIpAddress(*it, &addr)) {
                    server2 = addr;
                }
            }
        }
    }
    return true;
}

bool AgentParam::ParseServerListArguments
    (const boost::program_options::variables_map &var_map, Ip4Address *server1,
     uint16_t *port1, Ip4Address *server2, uint16_t *port2,
     const std::string &key) {

    if (var_map.count(key)) {
        vector<string> value = var_map[key].as<vector<string> >();
        if (value.size() > 2) {
            LOG(ERROR, "Error in Arguments. Cannot have more than 2 servers "
                    "for " << key );
            return false;
        }
        vector<string>::iterator it = value.begin();
        if (it != value.end()) {
            if (!ParseAddress(*it, server1, port1))
                return false;
            ++it;
            if (it != value.end()) {
                return ParseAddress(*it, server2, port2);
            }
        }
    }
    return true;
}

void AgentParam::ParseCollector() { 
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>("DEFAULT.collectors")) {
        boost::split(collector_server_list_, opt_str.get(),
                     boost::is_any_of(" "));
    }
}

void AgentParam::ParseVirtualHost() { 
    boost::system::error_code ec;
    optional<string> opt_str;

    GetValueFromTree<string>(vhost_.name_, "VIRTUAL-HOST-INTERFACE.name");

    if (opt_str = tree_.get_optional<string>("VIRTUAL-HOST-INTERFACE.ip")) {
        ec = Ip4PrefixParse(opt_str.get(), &vhost_.addr_, &vhost_.plen_);
        if (ec != 0 || vhost_.plen_ >= 32) {
            cout << "Error in config file <" << config_file_ 
                    << ">. Error parsing vhost ip-address from <" 
                    << opt_str.get() << ">\n";
        }
    }

    if (opt_str = tree_.get_optional<string>("VIRTUAL-HOST-INTERFACE.gateway")) {
        if (GetIpAddress(opt_str.get(), &vhost_.gw_) == false) {
            cout << "Error in config file <" << config_file_ 
                    << ">. Error parsing vhost gateway address from <" 
                    << opt_str.get() << ">\n";
        }
    }

    GetValueFromTree<string>(eth_port_, 
                             "VIRTUAL-HOST-INTERFACE.physical_interface");
}

void AgentParam::ParseDiscovery() {
    ParseIp("DISCOVERY.server", &dss_server_);
    GetValueFromTree<uint16_t>(xmpp_instance_count_, 
                               "DISCOVERY.max_control_nodes");
}

void AgentParam::ParseNetworks() {
    ParseIp("NETWORKS.control_network_ip", &mgmt_ip_);
}

void AgentParam::ParseHypervisor() {
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>("HYPERVISOR.type")) {
        // Initialize mode to KVM. Will be overwritten for XEN later
        mode_ = AgentParam::MODE_KVM;

        if (opt_str.get() == "xen") {
            mode_ = AgentParam::MODE_XEN;
            GetValueFromTree<string>(xen_ll_.name_, 
                                     "HYPERVISOR.xen_ll_interface");

            boost::system::error_code ec;
            if (opt_str = tree_.get_optional<string>
                    ("HYPERVISOR.xen_ll_ip")) {
                ec = Ip4PrefixParse(opt_str.get(), &xen_ll_.addr_,
                                    &xen_ll_.plen_);
                if (ec != 0 || xen_ll_.plen_ >= 32) {
                    cout << "Error in config file <" << config_file_ 
                            << ">. Error parsing Xen Link-local ip-address from <" 
                            << opt_str.get() << ">\n";
                    return;
                }
            }
        } else if (opt_str.get() == "vmware") {
            mode_ = AgentParam::MODE_VMWARE;
            GetValueFromTree<string>(vmware_physical_port_, 
                                     "HYPERVISOR.vmware_physical_interface");
        } else {
            mode_ = AgentParam::MODE_KVM;
        }
    }

    if (opt_str = tree_.get_optional<string>("HYPERVISOR.vmware_mode")) {
        if (opt_str.get() == "vcenter") {
            vmware_mode_ = VCENTER;
        } else if (opt_str.get() == "esxi_neutron") {
            vmware_mode_ = ESXI_NEUTRON;
        } else {
            cout << "Error in config file <" << config_file_ <<
                ">. Error parsing vmware_mode from <" 
                << opt_str.get() << ">\n";
            return;
        }
    }
}

void AgentParam::ParseDefaultSection() { 
    optional<string> opt_str;
    optional<unsigned int> opt_uint;

    if (!GetValueFromTree<uint16_t>(http_server_port_, 
                                    "DEFAULT.http_server_port")) {
        http_server_port_ = ContrailPorts::HttpPortAgent();
    }

    GetValueFromTree<string>(tunnel_type_, "DEFAULT.tunnel_type");
    if ((tunnel_type_ != "MPLSoUDP") && (tunnel_type_ != "VXLAN"))
        tunnel_type_ = "MPLSoGRE";

    if (!GetValueFromTree<uint16_t>(flow_cache_timeout_, 
                                    "DEFAULT.flow_cache_timeout")) {
        flow_cache_timeout_ = Agent::kDefaultFlowCacheTimeout;
    }
    
    GetValueFromTree<string>(host_name_, "DEFAULT.hostname");

    if (!GetValueFromTree<string>(log_level_, "DEFAULT.log_level")) {
        log_level_ = "SYS_DEBUG";
    }
    if (!GetValueFromTree<string>(log_file_, "DEFAULT.log_file")) {
        log_file_ = Agent::GetInstance()->log_file();
    }
    if (!GetValueFromTree<int>(log_files_count_, "DEFAULT.log_files_count")) {
        log_files_count_ = 10;
    }
    if (!GetValueFromTree<long>(log_file_size_, "DEFAULT.log_file_size")) {
        log_file_size_ = 1024*1024;
    }

    if (!GetValueFromTree<string>(log_category_, "DEFAULT.log_category")) {
        log_category_ = "*";
    }

    if (optional<bool> log_local_opt =
        tree_.get_optional<bool>("DEFAULT.log_local")) {
        log_local_ = true;
    } else {
        log_local_ = false;
    }

    if (optional<bool> debug_opt =
        tree_.get_optional<bool>("DEFAULT.debug")) {
        debug_ = true;
    } else {
        debug_ = false;
    }

    GetValueFromTree<bool>(use_syslog_, "DEFAULT.use_syslog");
    if (!GetValueFromTree<string>(syslog_facility_, "DEFAULT.syslog_facility")) {
        syslog_facility_ = "LOG_LOCAL0";
    }

    if (optional<bool> log_flow_opt =
        tree_.get_optional<bool>("DEFAULT.log_flow")) {
        log_flow_ = true;
    } else {
        log_flow_ = false;
    }
}

void AgentParam::ParseMetadataProxy() { 
    GetValueFromTree<string>(metadata_shared_secret_, 
                             "METADATA.metadata_proxy_secret");
}

void AgentParam::ParseFlows() {
    if (!GetValueFromTree<float>(max_vm_flows_, "FLOWS.max_vm_flows")) {
        max_vm_flows_ = (float) 100;
    }
    if (!GetValueFromTree<uint16_t>(linklocal_system_flows_, 
        "FLOWS.max_system_linklocal_flows")) {
        linklocal_system_flows_ = Agent::kDefaultMaxLinkLocalOpenFds;
    }
    if (!GetValueFromTree<uint16_t>(linklocal_vm_flows_, 
        "FLOWS.max_vm_linklocal_flows")) {
        linklocal_vm_flows_ = Agent::kDefaultMaxLinkLocalOpenFds;
    }
}

void AgentParam::ParseHeadlessMode() {
    if (!GetValueFromTree<bool>(headless_mode_, "DEFAULT.headless_mode")) {
        headless_mode_ = false;
    }
}

void AgentParam::ParseSimulateEvpnTor() {
    if (!GetValueFromTree<bool>(simulate_evpn_tor_,
                                "DEFAULT.simulate_evpn_tor")) {
        simulate_evpn_tor_ = false;
    }
}

void AgentParam::ParseServiceInstance() {
    GetValueFromTree<string>(si_netns_command_,
                             "SERVICE-INSTANCE.netns_command");
    GetValueFromTree<int>(si_netns_workers_,
                          "SERVICE-INSTANCE.netns_workers");
    GetValueFromTree<int>(si_netns_timeout_,
                          "SERVICE-INSTANCE.netns_timeout");
    GetValueFromTree<string>(si_haproxy_ssl_cert_path_,
                         "SERVICE-INSTANCE.haproxy_ssl_cert_path");

}

void AgentParam::ParseCollectorArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue< vector<string> >(var_map, collector_server_list_,
                                      "DEFAULT.collectors");
}

void AgentParam::ParseVirtualHostArguments
    (const boost::program_options::variables_map &var_map) {
    boost::system::error_code ec;

    GetOptValue<string>(var_map, vhost_.name_, "VIRTUAL-HOST-INTERFACE.name");
    string ip;
    if (GetOptValue<string>(var_map, ip, "VIRTUAL-HOST-INTERFACE.ip")) {
        ec = Ip4PrefixParse(ip, &vhost_.addr_, &vhost_.plen_);
        if (ec != 0 || vhost_.plen_ >= 32) {
            cout << "Error parsing vhost ip argument from <" << ip << ">\n";
        }
    }
    ParseIpArgument(var_map, vhost_.gw_, "VIRTUAL-HOST-INTERFACE.gateway");
    GetOptValue<string>(var_map, eth_port_,
                        "VIRTUAL-HOST-INTERFACE.physical_interface");
}

void AgentParam::ParseDiscoveryArguments
    (const boost::program_options::variables_map &var_map) {
    ParseIpArgument(var_map, dss_server_, "DISCOVERY.server");
    GetOptValue<uint16_t>(var_map, xmpp_instance_count_, 
                          "DISCOVERY.max_control_nodes");
}

void AgentParam::ParseNetworksArguments
    (const boost::program_options::variables_map &var_map) {
    ParseIpArgument(var_map, mgmt_ip_, "NETWORKS.control_network_ip");
}

void AgentParam::ParseHypervisorArguments
    (const boost::program_options::variables_map &var_map) {
    boost::system::error_code ec;
    if (var_map.count("HYPERVISOR.type") && 
        !var_map["HYPERVISOR.type"].defaulted()) {
        if (var_map["HYPERVISOR.type"].as<string>() == "xen") {
            mode_ = AgentParam::MODE_XEN;
            GetOptValue<string>(var_map, xen_ll_.name_, 
                                "HYPERVISOR.xen_ll_interface");

            if (var_map.count("HYPERVISOR.xen_ll_ip")) {
                string ip = var_map["HYPERVISOR.xen_ll_ip"].as<string>();
                ec = Ip4PrefixParse(ip, &xen_ll_.addr_, &xen_ll_.plen_);
                if (ec != 0 || xen_ll_.plen_ >= 32) {
                    cout << "Error in argument <" << config_file_ 
                            << ">. Error parsing Xen Link-local ip-address from <" 
                            << ip << ">\n";
                    exit(EINVAL);
                }
            }
        } else if (var_map["HYPERVISOR.type"].as<string>() == "vmware") {
            mode_ = AgentParam::MODE_VMWARE;
            GetOptValue<string>(var_map, vmware_physical_port_, 
                                "HYPERVISOR.vmware_physical_interface");
        } else {
            mode_ = AgentParam::MODE_KVM;
        }
    }

    if (var_map.count("HYPERVISOR.vmware_mode") && 
        !var_map["HYPERVISOR.vmware_mode"].defaulted()) {
        cout << " vmware_mode is " << var_map["HYPERVISOR.vmware_mode"].as<string>() << endl;
        if (var_map["HYPERVISOR.vmware_mode"].as<string>() == "vcenter") {
            vmware_mode_ = VCENTER;
        } else if (var_map["HYPERVISOR.vmware_mode"].as<string>() ==
                   "esxi_neutron") {
            vmware_mode_ = ESXI_NEUTRON;
        } else {
            cout << "Error in parsing arguement for HYPERVISOR.vmware_mode <" 
                << var_map["HYPERVISOR.vmware_mode"].as<string>() << endl;
            return;
        }
    }
}

void AgentParam::ParseDefaultSectionArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint16_t>(var_map, flow_cache_timeout_, 
                          "DEFAULT.flow_cache_timeout");
    GetOptValue<string>(var_map, host_name_, "DEFAULT.hostname");
    GetOptValue<uint16_t>(var_map, http_server_port_, 
                          "DEFAULT.http_server_port");
    GetOptValue<string>(var_map, log_category_, "DEFAULT.log_category");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file");
    GetValueFromTree<int>(log_files_count_, "DEFAULT.log_files_count");
    GetValueFromTree<long>(log_file_size_, "DEFAULT.log_file_size");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");
    GetOptValue<string>(var_map, syslog_facility_, "DEFAULT.syslog_facility");
    if (var_map.count("DEFAULT.use_syslog")) {
        use_syslog_ = true;
    }
    if (var_map.count("DEFAULT.log_local")) {
         log_local_ = true;
    }
    if (var_map.count("DEFAULT.debug")) {
        debug_ = true;
    }
    if (var_map.count("DEFAULT.log_flow")) {
         log_flow_ = true;
    }
}

void AgentParam::ParseMetadataProxyArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, metadata_shared_secret_,
                        "METADATA.metadata_proxy_secret");
}

void AgentParam::ParseFlowArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<float>(var_map, max_vm_flows_, "FLOWS.max_vm_flows");
    GetOptValue<uint16_t>(var_map, linklocal_system_flows_, 
                          "FLOWS.max_system_linklocal_flows");
    GetOptValue<uint16_t>(var_map, linklocal_vm_flows_,
                          "FLOWS.max_vm_linklocal_flows");
}

void AgentParam::ParseHeadlessModeArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<bool>(var_map, headless_mode_, "DEFAULT.headless_mode");
}

void AgentParam::ParseServiceInstanceArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, si_netns_command_, "SERVICE-INSTANCE.netns_command");
    GetOptValue<int>(var_map, si_netns_workers_, "SERVICE-INSTANCE.netns_workers");
    GetOptValue<int>(var_map, si_netns_timeout_, "SERVICE-INSTANCE.netns_timeout");
    GetOptValue<string>(var_map, si_haproxy_ssl_cert_path_,
                        "SERVICE-INSTANCE.haproxy_ssl_cert_path");

}

// Initialize hypervisor mode based on system information
// If "/proc/xen" exists it means we are running in Xen dom0
void AgentParam::InitFromSystem() {
    boost::system::error_code error;
    host_name_ = boost::asio::ip::host_name(error);

    struct stat fstat;
    if (stat("/proc/xen", &fstat) == 0) {
        mode_ = MODE_XEN;
        cout << "Found file /proc/xen. Initializing mode to XEN\n";
    }
    xen_ll_.addr_ = Ip4Address::from_string("169.254.0.1");
    xen_ll_.plen_ = 16;

    return;
}

// Update agent parameters from config file
void AgentParam::InitFromConfig() {
    // Read and parse INI
    try {
        read_ini(config_file_, tree_);
    } catch (exception &e) {
        cout <<  "Error reading config file <" << config_file_ 
            << ">. INI format error??? <" << e.what() << ">\n";
        return;
    } 

    ParseCollector();
    ParseVirtualHost();
    ParseServerList("CONTROL-NODE.server", &xmpp_server_1_, &xmpp_server_2_);
    ParseServerList("DNS.server", &dns_server_1_, &dns_port_1_,
                    &dns_server_2_, &dns_port_2_);
    ParseDiscovery();
    ParseNetworks();
    ParseHypervisor();
    ParseDefaultSection();
    ParseMetadataProxy();
    ParseFlows();
    ParseHeadlessMode();
    ParseSimulateEvpnTor();
    ParseServiceInstance();
    cout << "Config file <" << config_file_ << "> parsing completed.\n";
    return;
}

void AgentParam::InitFromArguments() {
    ParseCollectorArguments(var_map_);
    ParseVirtualHostArguments(var_map_);
    ParseServerListArguments(var_map_, xmpp_server_1_, xmpp_server_2_, 
                             "CONTROL-NODE.server");
    ParseServerListArguments(var_map_, &dns_server_1_, &dns_port_1_,
                             &dns_server_2_, &dns_port_2_, "DNS.server");
    ParseDiscoveryArguments(var_map_);
    ParseNetworksArguments(var_map_);
    ParseHypervisorArguments(var_map_);
    ParseDefaultSectionArguments(var_map_);
    ParseMetadataProxyArguments(var_map_);
    ParseHeadlessModeArguments(var_map_);
    ParseServiceInstanceArguments(var_map_);
    return;
}

// Update max_vm_flows_ if it is greater than 100.
// Update linklocal max flows if they are greater than the max allowed for the
// process. Also, ensure that the process is allowed to open upto
// linklocal_system_flows + kMaxOtherOpenFds files
void AgentParam::ComputeFlowLimits() {
    if (max_vm_flows_ > 100) {
        cout << "Updating flows configuration max-vm-flows to : 100%\n";
        max_vm_flows_ = 100;
    }
    if (max_vm_flows_ < 0) {
        cout << "Updating flows configuration max-vm-flows to : 0%\n";
        max_vm_flows_ = 0;
    }

    struct rlimit rl;
    int result = getrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        if (rl.rlim_max <= Agent::kMaxOtherOpenFds + 1) {
            cout << "Updating linklocal flows configuration to 0\n";
            linklocal_system_flows_ = linklocal_vm_flows_ = 0;
            return;
        }
        if (linklocal_system_flows_ > rl.rlim_max - Agent::kMaxOtherOpenFds - 1) {
            linklocal_system_flows_ = rl.rlim_max - Agent::kMaxOtherOpenFds - 1;
            cout << "Updating linklocal-system-flows configuration to : " <<
                linklocal_system_flows_ << "\n";
        }
        if (rl.rlim_cur < linklocal_system_flows_ + Agent::kMaxOtherOpenFds + 1) {
            struct rlimit new_rl;
            new_rl.rlim_max = rl.rlim_max;
            new_rl.rlim_cur = linklocal_system_flows_ + Agent::kMaxOtherOpenFds + 1;
            result = setrlimit(RLIMIT_NOFILE, &new_rl);
            if (result != 0) {
                if (rl.rlim_cur <= Agent::kMaxOtherOpenFds + 1) {
                    linklocal_system_flows_ = 0;
                } else {
                    linklocal_system_flows_ = rl.rlim_cur - Agent::kMaxOtherOpenFds - 1;
                }
                cout << "Unable to set Max open files limit to : " <<
                    new_rl.rlim_cur <<
                    " Updating linklocal-system-flows configuration to : " <<
                    linklocal_system_flows_ << "\n";
            }
        }
        if (linklocal_vm_flows_ > linklocal_system_flows_) {
            linklocal_vm_flows_ = linklocal_system_flows_;
            cout << "Updating linklocal-vm-flows configuration to : " <<
                linklocal_vm_flows_ << "\n";
        }
    } else {
        cout << "Unable to validate linklocal flow configuration\n";
    }
}

static bool ValidateInterface(bool test_mode, const std::string &ifname) {
    if (test_mode) {
        return true;
    }
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), IF_NAMESIZE);
    int err = ioctl(fd, SIOCGIFHWADDR, (void *)&ifr);
    close (fd);

    if (err < 0) {
        LOG(ERROR, "Error reading interface <" << ifname << ">. Error number "
            << errno << " : " << strerror(errno));
        return false;
    }

    return true;
}

int AgentParam::Validate() {
    // Validate vhost interface name
    if (vhost_.name_ == "") {
        LOG(ERROR, "Configuration error. vhost interface name not specified");
        return (EINVAL);
    }

    // Check if interface is already present
    if (ValidateInterface(test_mode_, vhost_.name_) == false) {
        return (ENODEV);
    }

    // Validate ethernet port
    if (eth_port_ == "") {
        LOG(ERROR, "Configuration error. eth_port not specified");
        return (EINVAL);
    }

    // Check if interface is already present
    if (ValidateInterface(test_mode_, eth_port_) == false) {
        return (ENODEV);
    }

    // Validate physical port used in vmware
    if (mode_ == MODE_VMWARE) {
        if (vmware_physical_port_ == "") {
            LOG(ERROR, "Configuration error. Physical port connecting to "
                "virtual-machines not specified");
            return (EINVAL);
        }

        if (ValidateInterface(test_mode_, vmware_physical_port_) == false) {
            return (ENODEV);
        }
    }

    return 0;
}

void AgentParam::InitVhostAndXenLLPrefix() {
    // Set the prefix address for VHOST and XENLL interfaces
    uint32_t mask = vhost_.plen_ ? (0xFFFFFFFF << (32 - vhost_.plen_)) : 0;
    vhost_.prefix_ = Ip4Address(vhost_.addr_.to_ulong() & mask);

    mask = xen_ll_.plen_ ? (0xFFFFFFFF << (32 - xen_ll_.plen_)) : 0;
    xen_ll_.prefix_ = Ip4Address(xen_ll_.addr_.to_ulong() & mask);
}

void AgentParam::Init(const string &config_file, const string &program_name) {
    config_file_ = config_file;
    program_name_ = program_name;

    InitFromSystem();
    InitFromConfig();
    InitFromArguments();
    InitVhostAndXenLLPrefix();

    vgw_config_table_->Init(tree_);
    ComputeFlowLimits();
}

void AgentParam::LogConfig() const {
    LOG(DEBUG, "vhost interface name        : " << vhost_.name_);
    LOG(DEBUG, "vhost IP Address            : " << vhost_.addr_.to_string() 
        << "/" << vhost_.plen_);
    LOG(DEBUG, "vhost gateway               : " << vhost_.gw_.to_string());
    LOG(DEBUG, "Ethernet port               : " << eth_port_);
    LOG(DEBUG, "XMPP Server-1               : " << xmpp_server_1_);
    LOG(DEBUG, "XMPP Server-2               : " << xmpp_server_2_);
    LOG(DEBUG, "DNS Server-1                : " << dns_server_1_);
    LOG(DEBUG, "DNS Port-1                  : " << dns_port_1_);
    LOG(DEBUG, "DNS Server-2                : " << dns_server_2_);
    LOG(DEBUG, "DNS Port-2                  : " << dns_port_2_);
    LOG(DEBUG, "Discovery Server            : " << dss_server_);
    LOG(DEBUG, "Controller Instances        : " << xmpp_instance_count_);
    LOG(DEBUG, "Tunnel-Type                 : " << tunnel_type_);
    LOG(DEBUG, "Metadata-Proxy Shared Secret: " << metadata_shared_secret_);
    LOG(DEBUG, "Max Vm Flows                : " << max_vm_flows_);
    LOG(DEBUG, "Linklocal Max System Flows  : " << linklocal_system_flows_);
    LOG(DEBUG, "Linklocal Max Vm Flows      : " << linklocal_vm_flows_);
    LOG(DEBUG, "Flow cache timeout          : " << flow_cache_timeout_);
    LOG(DEBUG, "Headless Mode               : " << headless_mode_);
    if (simulate_evpn_tor_) {
        LOG(DEBUG, "Simulate EVPN TOR           : " << simulate_evpn_tor_);
    }
    LOG(DEBUG, "Service instance netns cmd  : " << si_netns_command_);
    LOG(DEBUG, "Service instance workers    : " << si_netns_workers_);
    LOG(DEBUG, "Service instance timeout    : " << si_netns_timeout_);
    LOG(DEBUG, "Service instance HAProxy ssl: " << si_haproxy_ssl_cert_path_);
    if (mode_ == MODE_KVM) {
    LOG(DEBUG, "Hypervisor mode             : kvm");
        return;
    }

    if (mode_ == MODE_XEN) {
    LOG(DEBUG, "Hypervisor mode             : xen");
    LOG(DEBUG, "XEN Link Local port         : " << xen_ll_.name_);
    LOG(DEBUG, "XEN Link Local IP Address   : " << xen_ll_.addr_.to_string()
        << "/" << xen_ll_.plen_);
    }

    if (mode_ == MODE_VMWARE) {
    LOG(DEBUG, "Hypervisor mode             : vmware");
    LOG(DEBUG, "Vmware port                 : " << vmware_physical_port_);
    if (vmware_mode_ == VCENTER) {
    LOG(DEBUG, "Vmware mode                 : Vcenter");
    } else {
    LOG(DEBUG, "Vmware mode                 : Esxi_Neutron");
    }
    }
}

void AgentParam::set_test_mode(bool mode) {
    test_mode_ = mode;
}

void AgentParam::AddOptions
(const boost::program_options::options_description &opt) {
    options_.add(opt);
}

void AgentParam::ParseArguments(int argc, char *argv[]) {
    boost::program_options::store(opt::parse_command_line(argc, argv, options_),
                                  var_map_);
    boost::program_options::notify(var_map_);
}

AgentParam::AgentParam(Agent *agent, bool enable_flow_options,
                       bool enable_vhost_options,
                       bool enable_hypervisor_options,
                       bool enable_service_options) :
        enable_flow_options_(enable_flow_options),
        enable_vhost_options_(enable_vhost_options),
        enable_hypervisor_options_(enable_hypervisor_options),
        enable_service_options_(enable_service_options),
        vhost_(), eth_port_(), xmpp_instance_count_(), xmpp_server_1_(),
        xmpp_server_2_(), dns_server_1_(), dns_server_2_(),
        dns_port_1_(ContrailPorts::DnsServerPort()),
        dns_port_2_(ContrailPorts::DnsServerPort()),
        dss_server_(), mgmt_ip_(), mode_(MODE_KVM), xen_ll_(),
        tunnel_type_(), metadata_shared_secret_(), max_vm_flows_(),
        linklocal_system_flows_(), linklocal_vm_flows_(),
        flow_cache_timeout_(), config_file_(), program_name_(),
        log_file_(), log_local_(false), log_flow_(false), log_level_(),
        log_category_(), use_syslog_(false),
        collector_server_list_(), http_server_port_(), host_name_(),
        agent_stats_interval_(kAgentStatsInterval),
        flow_stats_interval_(kFlowStatsInterval),
        vrouter_stats_interval_(kVrouterStatsInterval),
        vmware_physical_port_(""), test_mode_(false), debug_(false), tree_(),
        headless_mode_(false), simulate_evpn_tor_(false),
        si_netns_command_(), si_netns_workers_(0),
        si_netns_timeout_(0), si_haproxy_ssl_cert_path_(),
        vmware_mode_(ESXI_NEUTRON) {

    vgw_config_table_ = std::auto_ptr<VirtualGatewayConfigTable>
        (new VirtualGatewayConfigTable(agent));

    // Set common command line arguments supported
    boost::program_options::options_description generic("Generic options");
    generic.add_options()
        ("help", "help message")
        ("config_file", 
         opt::value<string>()->default_value(agent->config_file()), 
         "Configuration file")
        ("version", "Display version information")
        ("CONTROL-NODE.server", 
         opt::value<std::vector<std::string> >()->multitoken(),
         "IP addresses of control nodes."
         " Max of 2 Ip addresses can be configured")
        ("DEFAULT.collectors",
         opt::value<std::vector<std::string> >()->multitoken(),
         "Collector server list")
        ("DEFAULT.debug", "Enable debug logging")
        ("DEFAULT.flow_cache_timeout", 
         opt::value<uint16_t>()->default_value(agent->kDefaultFlowCacheTimeout),
         "Flow aging time in seconds")
        ("DEFAULT.hostname", opt::value<string>(), 
         "Hostname of compute-node")
        ("DEFAULT.headless", opt::value<bool>(),
         "Run compute-node in headless mode")
        ("DEFAULT.http_server_port", 
         opt::value<uint16_t>()->default_value(ContrailPorts::HttpPortAgent()), 
         "Sandesh HTTP listener port")
        ("DEFAULT.tunnel_type", opt::value<string>()->default_value("MPLSoGRE"),
         "Tunnel Encapsulation type <MPLSoGRE|MPLSoUDP|VXLAN>")
        ("DISCOVERY.server", opt::value<string>(), 
         "IP address of discovery server")
        ("DISCOVERY.max_control_nodes", opt::value<uint16_t>(), 
         "Maximum number of control node info to be provided by discovery "
         "service <1|2>")
        ("DNS.server", opt::value<std::vector<std::string> >()->multitoken(),
         "IP addresses of dns nodes. Max of 2 Ip addresses can be configured")
        ("METADATA.metadata_proxy_secret", opt::value<string>(),
         "Shared secret for metadata proxy service")
        ("NETWORKS.control_network_ip", opt::value<string>(),
         "control-channel IP address used by WEB-UI to connect to vnswad")
        ;
    options_.add(generic);

    opt::options_description log("Logging options");
    log.add_options()
        ("DEFAULT.log_category", opt::value<string>()->default_value("*"),
         "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_file", 
         opt::value<string>()->default_value(agent->log_file()),
         "Filename for the logs to be written to")
        ("DEFAULT.log_files_count", opt::value<int>()->default_value(10),
         "Maximum log file roll over index")
        ("DEFAULT.log_file_size", opt::value<long>()->default_value(1024*1024),
         "Maximum size of the log file")
        ("DEFAULT.log_level", opt::value<string>()->default_value("SYS_DEBUG"),
         "Severity level for local logging of sandesh messages")
        ("DEFAULT.log_local", "Enable local logging of sandesh messages")
        ("DEFAULT.use_syslog", "Enable logging to syslog")
        ("DEFAULT.syslog_facility", opt::value<string>()->default_value("LOG_LOCAL0"),
         "Syslog facility to receive log lines")
        ("DEFAULT.log_flow", "Enable local logging of flow sandesh messages")
        ;
    options_.add(log);

    if (enable_flow_options_) {
        opt::options_description flow("Flow options");
        flow.add_options()
            ("FLOWS.max_vm_flows", opt::value<uint16_t>(), 
             "Maximum flows allowed per VM - given as \% of maximum system flows")
            ("FLOWS.max_system_linklocal_flows", opt::value<uint16_t>(), 
             "Maximum number of link-local flows allowed across all VMs")
            ("FLOWS.max_vm_linklocal_flows", opt::value<uint16_t>(), 
             "Maximum number of link-local flows allowed per VM")
            ;
        options_.add(flow);
    }

    if (enable_hypervisor_options_) {
        opt::options_description hypervisor("Hypervisor specific options");
        hypervisor.add_options()
            ("HYPERVISOR.type", opt::value<string>()->default_value("kvm"), 
             "Type of hypervisor <kvm|xen|vmware>")
            ("HYPERVISOR.xen_ll_interface", opt::value<string>(), 
             "Port name on host for link-local network")
            ("HYPERVISOR.xen_ll_ip", opt::value<string>(),
             "IP Address and prefix or the link local port in ip/prefix format")
            ("HYPERVISOR.vmware_physical_port", opt::value<string>(),
             "Physical port used to connect to VMs in VMWare environment")
            ("HYPERVISOR.vmware_mode",
             opt::value<string>()->default_value("esxi_neutron"), 
             "VMWare mode <esxi_neutron|vcenter>")
            ;
        options_.add(hypervisor);
    }

    if (enable_vhost_options_) {
        opt::options_description vhost("VHOST interface specific options");
        vhost.add_options()
            ("VIRTUAL-HOST-INTERFACE.name", opt::value<string>(),
             "Name of virtual host interface")
            ("VIRTUAL-HOST-INTERFACE.ip", opt::value<string>(), 
             "IP address and prefix in ip/prefix_len format")
            ("VIRTUAL-HOST-INTERFACE.gateway", opt::value<string>(), 
             "Gateway IP address for virtual host")
            ("VIRTUAL-HOST-INTERFACE.physical_interface", opt::value<string>(), 
             "Physical interface name to which virtual host interface maps to")
            ;
        options_.add(vhost);
    }

    if (enable_service_options_) {
        opt::options_description service("Service instance specific options");
        service.add_options()
            ("SERVICE-INSTANCE.netns_command", opt::value<string>(),
             "Script path used when a service instance is spawned with network namespace")
            ("SERVICE-INSTANCE.netns_timeout", opt::value<string>(),
             "Timeout used to set a netns command as failing and to destroy it")
            ("SERVICE-INSTANCE.netns_workers", opt::value<string>(),
             "Number of workers used to spawn netns command")
            ;
        options_.add(service);
    }
}

AgentParam::~AgentParam() {
}
