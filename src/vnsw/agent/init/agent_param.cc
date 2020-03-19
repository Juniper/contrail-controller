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
#include <net/if_arp.h>
#include <unistd.h>
#include <iostream>
#include <string>

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

std::map<string, std::map<string, string> >
AgentParam::ParseDerivedStats(const std::vector<std::string> &dsvec) {
    std::map<string, std::map<string, string> > dsmap;
    std::map<string, std::map<string, string> >::iterator dsiter;
    
    for (size_t idx=0; idx!=dsvec.size(); idx++) {
        size_t pos = dsvec[idx].find(':');
        assert(pos != string::npos);
        string dsfull = dsvec[idx].substr(0,pos);
        string dsarg = dsvec[idx].substr(pos+1, string::npos);
        
        size_t dpos = dsfull.find('.');
        assert(dpos != string::npos);
        string dsstruct = dsfull.substr(0,dpos);
        string dsattr = dsfull.substr(dpos+1, string::npos);

        dsiter = dsmap.find(dsstruct);
        std::map<string, string> dselem;
        if (dsiter!=dsmap.end()) dselem = dsiter->second;
        dselem[dsattr] = dsarg;
        
        dsmap[dsstruct] = dselem;
    }
    return dsmap;
}

void AgentParam::ParseCollectorDS() {
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>("DEFAULT.collectors")) {
        boost::split(collector_server_list_, opt_str.get(),
                     boost::is_any_of(" "));
    }
    if (opt_str = tree_.get_optional<string>("DEFAULT.derived_stats")) {
        vector<string> dsvec;
        boost::split(dsvec, opt_str.get(),
                     boost::is_any_of(" "));
        derived_stats_map_ = ParseDerivedStats(dsvec);
    }
}

void AgentParam::BuildAddressList(const string &val) {
    compute_node_address_list_.clear();
    if (val.empty()) {
        return;
    }

    vector<string> tokens;
    boost::split(tokens, val, boost::is_any_of(" "));
    vector<string>::iterator it = tokens.begin();
    while (it != tokens.end()) {
        std::string str = *it;
        ++it;

        boost::algorithm::trim(str);
        Ip4Address addr;
        if (GetIpAddress(str, &addr)) {
            compute_node_address_list_.push_back(addr);
        } else {
            LOG(ERROR, "Error in parsing address " << *it);
        }
    }
}

void AgentParam::ParseVirtualHost() {
    boost::system::error_code ec;
    optional<string> opt_str;

    GetValueFromTree<string>(vhost_.name_, "VIRTUAL-HOST-INTERFACE.name");

    if (opt_str = tree_.get_optional<string>("VIRTUAL-HOST-INTERFACE.ip")) {
        ec = Ip4PrefixParse(opt_str.get(), &vhost_.addr_, &vhost_.plen_);
        if (ec != 0 || vhost_.plen_ > 32) {
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

    if (opt_str = tree_.get_optional<string>
        ("VIRTUAL-HOST-INTERFACE.compute_node_address")) {
        BuildAddressList(opt_str.get());
    }
}

void AgentParam::ParseDns() {
    ParseServerList("DNS.server", &dns_server_1_, &dns_port_1_,
                    &dns_server_2_, &dns_port_2_);
    if (!GetValueFromTree<uint16_t>(dns_client_port_,
                                    "DNS.dns_client_port")) {
        dns_client_port_ = ContrailPorts::VrouterAgentDnsClientUdpPort();
    }
    GetValueFromTree<uint32_t>(dns_timeout_, "DNS.dns_timeout");
    GetValueFromTree<uint32_t>(dns_max_retries_, "DNS.dns_max_retries");
}

void AgentParam::ParseDiscovery() {
    GetValueFromTree<string>(dss_server_, "DISCOVERY.server");
    GetValueFromTree<uint32_t>(dss_port_, "DISCOVERY.port");
    if (!GetValueFromTree<bool>(dss_ssl_, "DISCOVERY.ssl")) {
        // set defaults
        dss_ssl_ = false;
    }
    if(!GetValueFromTree<string>(dss_cert_, "DISCOVERY.cert")) {
        // set default
       dss_cert_ = "/etc/contrail/ssl/server.pem";
    }
    if (!GetValueFromTree<string>(dss_key_, "DISCOVERY.key")) {
        // set default
        dss_key_ = "/etc/contrail/ssl/private/server-privkey.pem";
    }
    if (!GetValueFromTree<string>(dss_cacert_, "DISCOVERY.cacert")) {
        // set default
       dss_cacert_ = "/etc/contrail/ssl/ca-cert.pem";
    }
    if (!GetValueFromTree<uint16_t>(xmpp_instance_count_,
                                    "DISCOVERY.max_control_nodes")) {
        xmpp_instance_count_ = MAX_XMPP_SERVERS;
    }
}

void AgentParam::ParseNetworks() {
    ParseIp("NETWORKS.control_network_ip", &mgmt_ip_);
}

void AgentParam::ParseHypervisor() {
    optional<string> opt_str;
    if (opt_str = tree_.get_optional<string>("HYPERVISOR.type")) {
        // Initialize mode to KVM. Will be overwritten for XEN later
        hypervisor_mode_ = AgentParam::MODE_KVM;

        if (opt_str.get() == "xen") {
            hypervisor_mode_ = AgentParam::MODE_XEN;
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
            hypervisor_mode_ = AgentParam::MODE_VMWARE;
            GetValueFromTree<string>(vmware_physical_port_,
                                     "HYPERVISOR.vmware_physical_interface");
        } else if (opt_str.get() == "docker") {
            hypervisor_mode_ = AgentParam::MODE_DOCKER;
        } else {
            hypervisor_mode_ = AgentParam::MODE_KVM;
        }
    }

    if ((opt_str = tree_.get_optional<string>("HYPERVISOR.vmware_mode")) && opt_str.get() != "") {
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

void AgentParam::ParsePlatform() {
    std::string vrouter_platform;
    GetValueFromTree<string>(vrouter_platform, "DEFAULT.platform");
    if (vrouter_platform=="nic") {
        platform_ = AgentParam::VROUTER_ON_NIC;
    } else if (vrouter_platform=="dpdk") {
        platform_ = AgentParam::VROUTER_ON_HOST_DPDK;
        GetValueFromTree<string>(physical_interface_pci_addr_,
                                 "DEFAULT.physical_interface_address");
        GetValueFromTree<string>(physical_interface_mac_addr_,
                                 "DEFAULT.physical_interface_mac");
    } else {
        platform_ = AgentParam::VROUTER_ON_HOST;
    }
}

void AgentParam::ParseDefaultSection() {
    optional<string> opt_str;
    optional<unsigned int> opt_uint;

    GetValueFromTree<string>(host_name_, "DEFAULT.hostname");
    GetValueFromTree<string>(agent_name_, "DEFAULT.agent_name");

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

    if (!GetValueFromTree<uint32_t>(stale_interface_cleanup_timeout_,
                                   "DEFAULT.stale_interface_cleanup_timeout")) {
        stale_interface_cleanup_timeout_ =
            Agent::kDefaultStaleInterfaceCleanupTimeout;
    }

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
        log_category_ = "";
    }

    if (optional<bool> log_local_opt =
        tree_.get_optional<bool>("DEFAULT.log_local")) {
        log_local_ = true;
    } else {
        log_local_ = false;
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

    if (!GetValueFromTree<string>(log_property_file_, "DEFAULT.log_property_file")) {
        log_property_file_ = "";
    }

    if (!GetValueFromTree<bool>(xmpp_auth_enable_, "DEFAULT.xmpp_auth_enable")) {
        // set defaults
        xmpp_auth_enable_ = false;
    }

    if (!GetValueFromTree<string>(xmpp_server_cert_, "DEFAULT.xmpp_server_cert")) {
        // set defaults
        xmpp_server_cert_ = "/etc/contrail/ssl/certs/server.pem";
    }

    if (!GetValueFromTree<string>(xmpp_server_key_, "DEFAULT.xmpp_server_key")) {
        // set defaults
        xmpp_server_key_ = "/etc/contrail/ssl/private/server-privkey.pem";
    }

    if (!GetValueFromTree<string>(xmpp_ca_cert_, "DEFAULT.xmpp_ca_cert")) {
        // set defaults
        xmpp_ca_cert_ = "/etc/contrail/ssl/certs/ca-cert.pem";
    }

    if (!GetValueFromTree<bool>(xmpp_dns_auth_enable_,
                                "DEFAULT.xmpp_dns_auth_enable")) {
        // set defaults
        xmpp_dns_auth_enable_ = false;
    }

    if (!GetValueFromTree<uint32_t>(send_ratelimit_,
                                    "DEFAULT.sandesh_send_rate_limit")) {
        send_ratelimit_ = Sandesh::get_send_rate_limit();
    }
    if (!GetValueFromTree<uint16_t>(min_aap_prefix_len_,
                                    "DEFAULT.min_aap_prefix_len")) {
        min_aap_prefix_len_ = Agent::kMinAapPrefixLen;
    }

    if (optional<bool> subnet_hosts_resolvable_opt =
            tree_.get_optional<bool>("DEFAULT.subnet_hosts_resolvable")) {
        subnet_hosts_resolvable_ = *subnet_hosts_resolvable_opt;
    } else {
        subnet_hosts_resolvable_ = true;
    }

    if (!GetValueFromTree<uint16_t>(mirror_client_port_,
                                    "DEFAULT.mirror_client_port")) {
        mirror_client_port_ = ContrailPorts::VrouterAgentMirrorClientUdpPort();
    }

    if (!GetValueFromTree<uint32_t>(pkt0_tx_buffer_count_,
                                    "DEFAULT.pkt0_tx_buffers")) {
        pkt0_tx_buffer_count_ = Agent::kPkt0TxBufferCount;
    }
    if (!GetValueFromTree<bool>(measure_queue_delay_,
                                "DEFAULT.measure_queue_delay")) {
        measure_queue_delay_ = false;
    }
}

void AgentParam::ParseTaskSection() {
    GetValueFromTree<uint32_t>(tbb_thread_count_, "TASK.thread_count");
    GetValueFromTree<uint32_t>(tbb_exec_delay_, "TASK.log_exec_threshold");
    GetValueFromTree<uint32_t>(tbb_schedule_delay_,
                               "TASK.log_schedule_threshold");
    if (!GetValueFromTree<uint32_t>(tbb_keepawake_timeout_,
                                    "TASK.tbb_keepawake_timeout")) {
        tbb_keepawake_timeout_ = Agent::kDefaultTbbKeepawakeTimeout;
    }
}

void AgentParam::ParseMetadataProxy() {
    GetValueFromTree<string>(metadata_shared_secret_,
                             "METADATA.metadata_proxy_secret");
    if (!GetValueFromTree<uint16_t>(metadata_proxy_port_,
                                    "METADATA.metadata_proxy_port")) {
        metadata_proxy_port_ = ContrailPorts::MetadataProxyVrouterAgentPort();
    }
}

void AgentParam::ParseFlows() {
    if (!GetValueFromTree<uint16_t>(flow_thread_count_,
                                    "FLOWS.thread_count")) {
        flow_thread_count_ = Agent::kDefaultFlowThreadCount;
    }

    if (!GetValueFromTree<uint16_t>(flow_latency_limit_,
                                    "FLOWS.latency_limit")) {
        flow_latency_limit_ = Agent::kDefaultFlowLatencyLimit;
    }

    if (!GetValueFromTree<bool>(flow_trace_enable_, "FLOWS.trace_enable")) {
        flow_trace_enable_ = true;
    }

    if (!GetValueFromTree<bool>(flow_hash_excl_rid_,
                                "FLOWS.hash_exclude_router_id")) {
        flow_hash_excl_rid_ = false;
    }

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
    if (!GetValueFromTree<uint16_t>(flow_index_sm_log_count_,
                                    "FLOWS.index_sm_log_count")) {
        flow_index_sm_log_count_ = Agent::kDefaultFlowIndexSmLogCount;
    }

    GetValueFromTree<uint32_t>(flow_add_tokens_, "FLOWS.add_tokens");
    GetValueFromTree<uint32_t>(flow_ksync_tokens_, "FLOWS.ksync_tokens");
    GetValueFromTree<uint32_t>(flow_del_tokens_, "FLOWS.del_tokens");
    GetValueFromTree<uint32_t>(flow_update_tokens_, "FLOWS.update_tokens");
}

void AgentParam::ParseHeadlessMode() {
    if (!GetValueFromTree<bool>(headless_mode_, "DEFAULT.headless_mode")) {
        headless_mode_ = false;
    }
}

void AgentParam::ParseDhcpRelayMode() {
    if (!GetValueFromTree<bool>(dhcp_relay_mode_, "DEFAULT.dhcp_relay_mode")) {
        dhcp_relay_mode_ = false;
    }
}

void AgentParam::ParseAgentInfo() {
    std::string mode;
    GetValueFromTree<string>(mode, "DEFAULT.agent_mode");
    set_agent_mode(mode);

    GetValueFromTree<string>(mode, "DEFAULT.gateway_mode");
    set_gateway_mode(mode);

    if (!GetValueFromTree<string>(agent_base_dir_,
                                  "DEFAULT.agent_base_directory")) {
        agent_base_dir_ = "/var/lib/contrail";
    }
}

void AgentParam::set_agent_mode(const std::string &mode) {
    std::string agent_mode = boost::to_lower_copy(mode);
    if (agent_mode == "tsn")
        agent_mode_ = TSN_AGENT;
    else if (agent_mode == "tor")
        agent_mode_ = TOR_AGENT;
    else
        agent_mode_ = VROUTER_AGENT;
}

void AgentParam::set_gateway_mode(const std::string &mode) {
    std::string gateway_mode = boost::to_lower_copy(mode);
    if (gateway_mode == "server")
        gateway_mode_ = SERVER;
    else if (gateway_mode == "vcpe")
        gateway_mode_ = VCPE;
    else
        gateway_mode_ = NONE;
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
    GetValueFromTree<string>(si_docker_command_,
                             "SERVICE-INSTANCE.docker_command");
    GetValueFromTree<int>(si_netns_workers_,
                          "SERVICE-INSTANCE.netns_workers");
    GetValueFromTree<int>(si_netns_timeout_,
                          "SERVICE-INSTANCE.netns_timeout");
    GetValueFromTree<string>(si_lb_ssl_cert_path_,
                         "SERVICE-INSTANCE.lb_ssl_cert_path");
    GetValueFromTree<string>(si_lbaas_auth_conf_,
                         "SERVICE-INSTANCE.lbaas_auth_conf");
}

void AgentParam::ParseNexthopServer() {
    GetValueFromTree<string>(nexthop_server_endpoint_,
                             "NEXTHOP-SERVER.endpoint");
    GetValueFromTree<bool>(nexthop_server_add_pid_,
                           "NEXTHOP-SERVER.add_pid");
    if (nexthop_server_add_pid_) {
        std::stringstream ss;
        ss << nexthop_server_endpoint_ << "." << getpid();
        nexthop_server_endpoint_ = ss.str();
    }
}

void AgentParam::ParseServices() {
    GetValueFromTree<string>(bgp_as_a_service_port_range_,
                             "SERVICES.bgp_as_a_service_port_range");
    GetValueFromTree<uint32_t>(services_queue_limit_, "SERVICES.queue_limit");
}

void AgentParam::ParseQueue() {

    const std::string qos_str = "QUEUE";
    std::string input;
    std::vector<std::string> tokens;
    std::string sep = "[],";
    BOOST_FOREACH(const ptree::value_type &section, tree_) {
        if (section.first.compare(0, qos_str.size(), qos_str) != 0) {
            continue;
        }
        uint16_t queue;
        std::string hw_queue = section.first;
        if (sscanf(hw_queue.c_str(), "QUEUE-%hu", &queue) != 1) {
                continue;
        }
        BOOST_FOREACH(const ptree::value_type &key, section.second) {
            if (key.first.compare("logical_queue") == 0) {
                input = key.second.get_value<string>();
                boost::split(tokens, input, boost::is_any_of(sep),
                         boost::token_compress_on);

                for (std::vector<string>::const_iterator it = tokens.begin();
                    it != tokens.end(); it++) {

                    if (*it == Agent::NullString()) {
                        continue;
                    }

                    string range = *it;
                    std::vector<uint16_t> range_value;
                    if (stringToIntegerList(range, "-", range_value)) {
                        if (range_value.size() == 1) {
                            qos_queue_map_[range_value[0]] = queue;
                            continue;
                        }

                        if (range_value[0] > range_value[1]) {
                            continue;
                        }

                        for (uint16_t i = range_value[0]; i <= range_value[1]; i++) {
                            qos_queue_map_[i] = queue;
                        }
                    }
                }
            }
            if (key.first.compare("default_hw_queue") == 0) {
                bool is_default = key.second.get_value<bool>();
                if (is_default) {
                    default_nic_queue_ = queue;
                }
            }
        }
    }
}

void AgentParam::ParseCollectorDSArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue< vector<string> >(var_map, collector_server_list_,
                                      "DEFAULT.collectors");
    vector<string> dsvec;
    if (GetOptValue< vector<string> >(var_map, dsvec,
                                      "DEFAULT.derived_stats")) {
        derived_stats_map_ = ParseDerivedStats(dsvec);
    }

    GetValueFromTree<uint32_t>(task_monitor_timeout_msec_,
                               "TASK.task_monitor_timeout");
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

void AgentParam::ParseDnsArguments
    (const boost::program_options::variables_map &var_map) {
    ParseServerListArguments(var_map_, &dns_server_1_, &dns_port_1_,
                             &dns_server_2_, &dns_port_2_, "DNS.server");
    GetOptValue<uint16_t>(var_map, dns_client_port_, "DNS.dns_client_port");
    GetOptValue<uint32_t>(var_map, dns_timeout_, "DNS.dns_timeout");
    GetOptValue<uint32_t>(var_map, dns_max_retries_, "DNS.dns_max_retries");
}

void AgentParam::ParseDiscoveryArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, dss_server_, "DISCOVERY.server");
    GetOptValue<uint32_t>(var_map, dss_port_, "DISCOVERY.port");
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
            hypervisor_mode_ = AgentParam::MODE_XEN;
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
            hypervisor_mode_ = AgentParam::MODE_VMWARE;
            GetOptValue<string>(var_map, vmware_physical_port_,
                                "HYPERVISOR.vmware_physical_interface");
        } else {
            hypervisor_mode_ = AgentParam::MODE_KVM;
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
    GetOptValue<uint32_t>(var_map, stale_interface_cleanup_timeout_,
                          "DEFAULT.stale_interface_cleanup_timeout");
    GetOptValue<string>(var_map, host_name_, "DEFAULT.hostname");
    GetOptValue<string>(var_map, agent_name_, "DEFAULT.agent_name");
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
    if (var_map.count("DEFAULT.log_flow")) {
         log_flow_ = true;
    }

    GetOptValue<bool>(var_map, xmpp_auth_enable_, "DEFAULT.xmpp_auth_enable");
    GetOptValue<bool>(var_map, xmpp_dns_auth_enable_,
                      "DEFAULT.xmpp_dns_auth_enable");
    GetOptValue<string>(var_map, xmpp_server_cert_, "DEFAULT.xmpp_server_cert");
    GetOptValue<string>(var_map, xmpp_server_key_, 
                        "DEFAULT.xmpp_server_key");
    GetOptValue<string>(var_map, xmpp_ca_cert_, "DEFAULT.xmpp_ca_cert");

    GetOptValue<uint32_t>(var_map, send_ratelimit_,
                          "DEFAULT.sandesh_send_rate_limit");
    GetOptValue<bool>(var_map, subnet_hosts_resolvable_,
                      "DEFAULT.subnet_hosts_resolvable");
    GetOptValue<uint16_t>(var_map, mirror_client_port_,
                          "DEFAULT.mirror_client_port");
    GetOptValue<uint32_t>(var_map, pkt0_tx_buffer_count_,
                          "DEFAULT.pkt0_tx_buffers");
    GetOptValue<bool>(var_map, measure_queue_delay_,
                      "DEFAULT.measure_queue_delay");
    GetOptValue<uint16_t>(var_map, min_aap_prefix_len_,
                          "DEFAULT.min_aap_prefix_len");
}

void AgentParam::ParseTaskSectionArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint32_t>(var_map, tbb_thread_count_,
                          "TASK.thread_count");
    GetOptValue<uint32_t>(var_map, tbb_exec_delay_,
                          "TASK.log_exec_threshold");
    GetOptValue<uint32_t>(var_map, tbb_schedule_delay_,
                          "TASK.log_schedule_threshold");
    GetOptValue<uint32_t>(var_map, tbb_keepawake_timeout_,
                          "TASK.tbb_keepawake_timeout");
    GetOptValue<uint32_t>(var_map, task_monitor_timeout_msec_,
                          "TASK.task_monitor_timeout");
}

void AgentParam::ParseMetadataProxyArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, metadata_shared_secret_,
                        "METADATA.metadata_proxy_secret");
    GetOptValue<uint16_t>(var_map, metadata_proxy_port_,
                        "METADATA.metadata_proxy_port");
}

void AgentParam::ParseFlowArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint16_t>(var_map, flow_thread_count_,
                          "FLOWS.thread_count");
    GetOptValue<uint16_t>(var_map, flow_latency_limit_,
                          "FLOWS.latency_limit");
    GetOptValue<bool>(var_map, flow_trace_enable_, "FLOWS.trace_enable");
    GetOptValue<bool>(var_map, flow_hash_excl_rid_,
                      "FLOWS.hash_exclude_router_id");
    uint16_t val = 0;
    if (GetOptValue<uint16_t>(var_map, val, "FLOWS.max_vm_flows")) {
        max_vm_flows_ = (float)val;
    }

    GetOptValue<uint16_t>(var_map, linklocal_system_flows_,
                          "FLOWS.max_system_linklocal_flows");
    GetOptValue<uint16_t>(var_map, linklocal_vm_flows_,
                          "FLOWS.max_vm_linklocal_flows");
    GetOptValue<uint16_t>(var_map, flow_index_sm_log_count_,
                          "FLOWS.index_sm_log_count");
    GetOptValue<uint32_t>(var_map, flow_add_tokens_,
                          "FLOWS.add_tokens");
    GetOptValue<uint32_t>(var_map, flow_ksync_tokens_,
                          "FLOWS.ksync_tokens");
    GetOptValue<uint32_t>(var_map, flow_del_tokens_,
                          "FLOWS.del_tokens");
    GetOptValue<uint32_t>(var_map, flow_update_tokens_,
                          "FLOWS.update_tokens");
}

void AgentParam::ParseHeadlessModeArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<bool>(var_map, headless_mode_, "DEFAULT.headless_mode");
}

void AgentParam::ParseDhcpRelayModeArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<bool>(var_map, dhcp_relay_mode_, "DEFAULT.dhcp_relay_mode");
}

void AgentParam::ParseAgentInfoArguments
    (const boost::program_options::variables_map &var_map) {
    std::string mode;
    if (GetOptValue<string>(var_map, mode, "DEFAULT.agent_mode")) {
        set_agent_mode(mode);
    }
    if (GetOptValue<string>(var_map, mode, "DEFAULT.gateway_mode")) {
        set_gateway_mode(mode);
    }
    GetOptValue<string>(var_map, agent_base_dir_,
                        "DEFAULT.agent_base_directory");
}

void AgentParam::ParseServiceInstanceArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, si_netns_command_, "SERVICE-INSTANCE.netns_command");
    GetOptValue<string>(var_map, si_docker_command_, "SERVICE-INSTANCE.docker_command");
    GetOptValue<int>(var_map, si_netns_workers_, "SERVICE-INSTANCE.netns_workers");
    GetOptValue<int>(var_map, si_netns_timeout_, "SERVICE-INSTANCE.netns_timeout");
    GetOptValue<string>(var_map, si_lb_ssl_cert_path_,
                        "SERVICE-INSTANCE.lb_ssl_cert_path");
    GetOptValue<string>(var_map, si_lbaas_auth_conf_,
                        "SERVICE-INSTANCE.lbaas_auth_conf");

}

void AgentParam::ParseNexthopServerArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, nexthop_server_endpoint_,
                        "NEXTHOP-SERVER.endpoint");
    GetOptValue<bool>(var_map, nexthop_server_add_pid_,
                             "NEXTHOP-SERVER.add_pid");
    if (nexthop_server_add_pid_) {
        std::stringstream ss;
        ss << nexthop_server_endpoint_ << "." << getpid();
        nexthop_server_endpoint_ = ss.str();
    }
}

void AgentParam::ParsePlatformArguments
    (const boost::program_options::variables_map &var_map) {
    boost::system::error_code ec;
    if (var_map.count("DEFAULT.platform") &&
        !var_map["DEFAULT.platform"].defaulted()) {
        if (var_map["DEFAULT.platform"].as<string>() == "nic") {
            platform_ = AgentParam::VROUTER_ON_NIC;
        } else if (var_map["DEFAULT.platform"].as<string>() == "dpdk") {
            platform_ = AgentParam::VROUTER_ON_HOST_DPDK;
            if (var_map.count("DEFAULT.physical_interface_address")) {
                physical_interface_pci_addr_ =
                var_map["DEFAULT.physical_interface_address"].as<string>();
                physical_interface_mac_addr_ =
                var_map["DEFAULT.physical_interface_mac"].as<string>();
            }
        } else {
            platform_ = AgentParam::VROUTER_ON_HOST;
        }
    }
}

void AgentParam::ParseServicesArguments
    (const boost::program_options::variables_map &v) {
    GetOptValue<string>(v, bgp_as_a_service_port_range_,
                        "SERVICES.bgp_as_a_service_port_range");
    GetOptValue<uint32_t>(v, services_queue_limit_, "SERVICES.queue_limit");
    GetOptValue<uint32_t>(v, bgpaas_max_shared_sessions_,
                          "SERVICES.bgpaas_max_shared_sessions");
}

// Initialize hypervisor mode based on system information
// If "/proc/xen" exists it means we are running in Xen dom0
void AgentParam::InitFromSystem() {
    boost::system::error_code error;
    host_name_ = boost::asio::ip::host_name(error);
    agent_name_ = host_name_;

    struct stat fstat;
    if (stat("/proc/xen", &fstat) == 0) {
        hypervisor_mode_ = MODE_XEN;
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

    ParseCollectorDS();
    ParseVirtualHost();
    ParseServerList("CONTROL-NODE.server", &xmpp_server_1_, &xmpp_server_2_);
    ParseDns();
    ParseDiscovery();
    ParseNetworks();
    ParseHypervisor();
    ParseDefaultSection();
    ParseTaskSection();
    ParseMetadataProxy();
    ParseFlows();
    ParseHeadlessMode();
    ParseDhcpRelayMode();
    ParseSimulateEvpnTor();
    ParseServiceInstance();
    ParseAgentInfo();
    ParseNexthopServer();
    ParsePlatform();
    ParseServices();
    ParseQueue();
    cout << "Config file <" << config_file_ << "> parsing completed.\n";
    return;
}

void AgentParam::InitFromArguments() {
    ParseCollectorDSArguments(var_map_);
    ParseVirtualHostArguments(var_map_);
    ParseServerListArguments(var_map_, xmpp_server_1_, xmpp_server_2_,
                             "CONTROL-NODE.server");
    ParseDnsArguments(var_map_);
    ParseDiscoveryArguments(var_map_);
    ParseNetworksArguments(var_map_);
    ParseHypervisorArguments(var_map_);
    ParseDefaultSectionArguments(var_map_);
    ParseTaskSectionArguments(var_map_);
    ParseFlowArguments(var_map_);
    ParseMetadataProxyArguments(var_map_);
    ParseHeadlessModeArguments(var_map_);
    ParseDhcpRelayModeArguments(var_map_);
    ParseServiceInstanceArguments(var_map_);
    ParseAgentInfoArguments(var_map_);
    ParseNexthopServerArguments(var_map_);
    ParsePlatformArguments(var_map_);
    ParseServicesArguments(var_map_);
    return;
}

void AgentParam::UpdateBgpAsaServicePortRangeValue() {
    if (!stringToIntegerList(bgp_as_a_service_port_range_, "-",
                             bgp_as_a_service_port_range_value_)) {
        bgp_as_a_service_port_range_value_.clear();
        return;
    }
}

void AgentParam::UpdateBgpAsaServicePortRange() {
    if (!stringToIntegerList(bgp_as_a_service_port_range_, "-",
                             bgp_as_a_service_port_range_value_)) {
        bgp_as_a_service_port_range_value_.clear();
        return;
    }
    uint16_t start = bgp_as_a_service_port_range_value_[0];
    uint16_t end = bgp_as_a_service_port_range_value_[1];

    uint16_t count = end - start + 1;
    if (count > Agent::kMaxBgpAsAServerSessions) {
        bgp_as_a_service_port_range_value_[1] =
            start + Agent::kMaxBgpAsAServerSessions - 1;
        count = Agent::kMaxBgpAsAServerSessions;
    }

    struct rlimit rl;
    int result = getrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        if (rl.rlim_max <= Agent::kMaxOtherOpenFds) {
            cout << "Clearing BGP as a Service port range," <<
                    "as Max fd system limit is inadequate\n";
            bgp_as_a_service_port_range_value_.clear();
            return;
        }
        if (count > rl.rlim_max - Agent::kMaxOtherOpenFds) {
            bgp_as_a_service_port_range_value_[1] =
                start + rl.rlim_max - Agent::kMaxOtherOpenFds - 1;
            cout << "Updating BGP as a Service port range to " <<
                bgp_as_a_service_port_range_value_[0] << " - " <<
                bgp_as_a_service_port_range_value_[1] << "\n";
        }
    } else {
        cout << "Unable to validate BGP as a server port range configuration\n";
    }
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
        if (linklocal_system_flows_ > rl.rlim_max -
                                      Agent::kMaxOtherOpenFds - 1) {
            linklocal_system_flows_ = rl.rlim_max -
                                      Agent::kMaxOtherOpenFds - 1;
            cout << "Updating linklocal-system-flows configuration to : " <<
                linklocal_system_flows_ << "\n";
        }
        if (rl.rlim_cur < linklocal_system_flows_ +
                          Agent::kMaxOtherOpenFds + 1) {
            struct rlimit new_rl;
            new_rl.rlim_max = rl.rlim_max;
            new_rl.rlim_cur = linklocal_system_flows_ +
                              Agent::kMaxOtherOpenFds + 1;
            result = setrlimit(RLIMIT_NOFILE, &new_rl);
            if (result != 0) {
                if (rl.rlim_cur <= Agent::kMaxOtherOpenFds + 1) {
                    linklocal_system_flows_ = 0;
                } else {
                    linklocal_system_flows_ = rl.rlim_cur -
                                              Agent::kMaxOtherOpenFds - 1;
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

static bool ValidateInterface(bool test_mode, const std::string &ifname,
                              bool *no_arp, string *eth_encap) {
    *no_arp = false;
    *eth_encap = "";

    if (test_mode) {
        return true;
    }
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), IF_NAMESIZE);
    int err = ioctl(fd, SIOCGIFFLAGS, (void *)&ifr);
    close (fd);

    if (err < 0) {
        LOG(ERROR, "Error reading interface <" << ifname << ">. Error number "
            << errno << " : " << strerror(errno));
        return false;
    }

    if ((ifr.ifr_flags & IFF_NOARP)) {
        *no_arp = true;
    }

    char fname[128];
    snprintf(fname, 128, "/sys/class/net/%s/type", ifname.c_str());
    FILE *f = fopen(fname, "r");
    if (f) {
        int type;
        if (fscanf(f, "%d", &type) >= 0) {
            if (type == ARPHRD_NONE) {
                *eth_encap = "none";
            }
        }
    }

    return true;
}

int AgentParam::Validate() {
    // TODO: fix the validation for the DPDK platform
    if (platform_ == AgentParam::VROUTER_ON_HOST_DPDK)
        return 0;

    // Validate vhost interface name
    if (vhost_.name_ == "") {
        LOG(ERROR, "Configuration error. vhost interface name not specified");
        return (EINVAL);
    }

    bool no_arp;
    string encap;
    // Check if interface is already present
    if (ValidateInterface(test_mode_, vhost_.name_, &no_arp, &encap) == false) {
        return (ENODEV);
    }

    // Validate ethernet port
    if (eth_port_ == "") {
        LOG(ERROR, "Configuration error. eth_port not specified");
        return (EINVAL);
    }

    // Check if interface is already present
    if (ValidateInterface(test_mode_, eth_port_, &eth_port_no_arp_,
                          &eth_port_encap_type_) == false) {
        return (ENODEV);
    }

    // Validate physical port used in vmware
    if (hypervisor_mode_ == MODE_VMWARE) {
        if (vmware_physical_port_ == "") {
            LOG(ERROR, "Configuration error. Physical port connecting to "
                "virtual-machines not specified");
            return (EINVAL);
        }

        if (ValidateInterface(test_mode_, vmware_physical_port_, &no_arp,
                              &encap) == false) {
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
    UpdateBgpAsaServicePortRangeValue();
    ComputeFlowLimits();
    vgw_config_table_->InitFromConfig(tree_);
}

void AgentParam::LogConfig() const {
    LOG(DEBUG, "vhost interface name        : " << vhost_.name_);
    LOG(DEBUG, "vhost IP Address            : " << vhost_.addr_.to_string()
        << "/" << vhost_.plen_);
    LOG(DEBUG, "vhost gateway               : " << vhost_.gw_.to_string());
    LOG(DEBUG, "Ethernet port               : " << eth_port_);

    LOG(DEBUG, "XMPP Server-1               : " << xmpp_server_1_);
    LOG(DEBUG, "XMPP Server-2               : " << xmpp_server_2_);
    LOG(DEBUG, "Xmpp Authentication         : " << xmpp_auth_enable_);
    if (xmpp_auth_enable_) {
        LOG(DEBUG, "Xmpp Server Certificate : " << xmpp_server_cert_);
        LOG(DEBUG, "Xmpp Server Key         : " << xmpp_server_key_);
        LOG(DEBUG, "Xmpp CA Certificate     : " << xmpp_ca_cert_);
    }

    LOG(DEBUG, "DNS Server-1                : " << dns_server_1_);
    LOG(DEBUG, "DNS Port-1                  : " << dns_port_1_);
    LOG(DEBUG, "DNS Server-2                : " << dns_server_2_);
    LOG(DEBUG, "DNS Port-2                  : " << dns_port_2_);
    LOG(DEBUG, "DNS client port             : " << dns_client_port_);
    LOG(DEBUG, "DNS timeout                 : " << dns_timeout_);
    LOG(DEBUG, "DNS max retries             : " << dns_max_retries_);
    LOG(DEBUG, "Xmpp Dns Authentication     : " << xmpp_dns_auth_enable_);
    if (xmpp_dns_auth_enable_) {
        LOG(DEBUG, "Xmpp Server Certificate : " << xmpp_server_cert_);
        LOG(DEBUG, "Xmpp Server Key         : " << xmpp_server_key_);
        LOG(DEBUG, "Xmpp CA Certificate     : " << xmpp_ca_cert_);
    }

    LOG(DEBUG, "Discovery Server:Port       : " << dss_server_ << ":" << dss_port_);
    LOG(DEBUG, "Controller Instances        : " << xmpp_instance_count_);
    LOG(DEBUG, "Tunnel-Type                 : " << tunnel_type_);
    LOG(DEBUG, "Metadata-Proxy Shared Secret: " << metadata_shared_secret_);
    LOG(DEBUG, "Metadata-Proxy Port         : " << metadata_proxy_port_);
    LOG(DEBUG, "Max Vm Flows                : " << max_vm_flows_);
    LOG(DEBUG, "Linklocal Max System Flows  : " << linklocal_system_flows_);
    LOG(DEBUG, "Linklocal Max Vm Flows      : " << linklocal_vm_flows_);
    LOG(DEBUG, "Flow cache timeout          : " << flow_cache_timeout_);
    LOG(DEBUG, "Stale Interface cleanup timeout  : "
        << stale_interface_cleanup_timeout_);
    LOG(DEBUG, "Flow thread count           : " << flow_thread_count_);
    LOG(DEBUG, "Flow latency limit          : " << flow_latency_limit_);
    LOG(DEBUG, "Flow index-mgr sm log count : " << flow_index_sm_log_count_);
    LOG(DEBUG, "Flow add-tokens             : " << flow_add_tokens_);
    LOG(DEBUG, "Flow ksync-tokens           : " << flow_ksync_tokens_);
    LOG(DEBUG, "Flow del-tokens             : " << flow_del_tokens_);
    LOG(DEBUG, "Flow update-tokens          : " << flow_update_tokens_);
    LOG(DEBUG, "Flow excluding Router ID in hash    :" << flow_hash_excl_rid_);

    if (agent_mode_ == VROUTER_AGENT)
        LOG(DEBUG, "Agent Mode                  : Vrouter");
    else if (agent_mode_ == TSN_AGENT)
        LOG(DEBUG, "Agent Mode                  : TSN");
    else if (agent_mode_ == TOR_AGENT)
        LOG(DEBUG, "Agent Mode                  : TOR");

    if (gateway_mode_ == SERVER)
        LOG(DEBUG, "Gateway Mode                : Server");
    else if (gateway_mode_ == VCPE)
        LOG(DEBUG, "Gateway Mode                : vCPE");
    else if (gateway_mode_ == NONE)
        LOG(DEBUG, "Gateway Mode                : None");

    LOG(DEBUG, "Headless Mode               : " << headless_mode_);
    LOG(DEBUG, "DHCP Relay Mode             : " << dhcp_relay_mode_);
    if (simulate_evpn_tor_) {
        LOG(DEBUG, "Simulate EVPN TOR           : " << simulate_evpn_tor_);
    }
    LOG(DEBUG, "Service instance netns cmd  : " << si_netns_command_);
    LOG(DEBUG, "Service instance docker cmd  : " << si_docker_command_);
    LOG(DEBUG, "Service instance workers    : " << si_netns_workers_);
    LOG(DEBUG, "Service instance timeout    : " << si_netns_timeout_);
    LOG(DEBUG, "Service instance lb ssl     : " << si_lb_ssl_cert_path_);
    LOG(DEBUG, "Service instance lbaas auth : " << si_lbaas_auth_conf_);
    LOG(DEBUG, "Bgp as a service port range : " << bgp_as_a_service_port_range_);
    LOG(DEBUG, "Services queue limit        : " << services_queue_limit_);
    LOG(DEBUG, "BGPAAS max shared sessions for service port  : " << bgpaas_max_shared_sessions_);
    if (hypervisor_mode_ == MODE_KVM) {
    LOG(DEBUG, "Hypervisor mode             : kvm");
        return;
    }

    if (hypervisor_mode_ == MODE_XEN) {
    LOG(DEBUG, "Hypervisor mode             : xen");
    LOG(DEBUG, "XEN Link Local port         : " << xen_ll_.name_);
    LOG(DEBUG, "XEN Link Local IP Address   : " << xen_ll_.addr_.to_string()
        << "/" << xen_ll_.plen_);
    }

    if (hypervisor_mode_ == MODE_VMWARE) {
    LOG(DEBUG, "Hypervisor mode             : vmware");
    LOG(DEBUG, "Vmware port                 : " << vmware_physical_port_);
    if (vmware_mode_ == VCENTER) {
    LOG(DEBUG, "Vmware mode                 : Vcenter");
    } else {
    LOG(DEBUG, "Vmware mode                 : Esxi_Neutron");
    }
    }
    LOG(DEBUG, "Nexthop server endpoint  : " << nexthop_server_endpoint_);
    LOG(DEBUG, "Agent base directory     : " << agent_base_dir_);
}

void AgentParam::PostValidateLogConfig() const {
    LOG(DEBUG, "Ethernet Port Encap Type    : " << eth_port_encap_type_);
    if (eth_port_no_arp_) {
    LOG(DEBUG, "Ethernet Port No-ARP        : " << "TRUE");
    }

    if (platform_ == VROUTER_ON_NIC) {
        LOG(DEBUG, "Platform mode           : Vrouter on NIC");
    } else if (platform_ == VROUTER_ON_HOST_DPDK) {
        LOG(DEBUG, "Platform mode           : Vrouter on DPDK");
    } else {
        LOG(DEBUG, "Platform mode           : Vrouter on host linux kernel ");
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

AgentParam::AgentParam(bool enable_flow_options,
                       bool enable_vhost_options,
                       bool enable_hypervisor_options,
                       bool enable_service_options,
                       AgentMode agent_mode) :
        enable_flow_options_(enable_flow_options),
        enable_vhost_options_(enable_vhost_options),
        enable_hypervisor_options_(enable_hypervisor_options),
        enable_service_options_(enable_service_options),
        agent_mode_(agent_mode), gateway_mode_(NONE), vhost_(),
        pkt0_tx_buffer_count_(Agent::kPkt0TxBufferCount),
        measure_queue_delay_(false),
        agent_name_(), eth_port_(),
        eth_port_no_arp_(false), eth_port_encap_type_(),
        xmpp_instance_count_(),
        dns_port_1_(ContrailPorts::DnsServerPort()),
        dns_port_2_(ContrailPorts::DnsServerPort()),
        dns_client_port_(0), dns_timeout_(3000),
        dns_max_retries_(2), mirror_client_port_(0),
        dss_server_(), dss_port_(0), mgmt_ip_(), hypervisor_mode_(MODE_KVM), 
        xen_ll_(), tunnel_type_(), metadata_shared_secret_(),
        metadata_proxy_port_(0), max_vm_flows_(),
        linklocal_system_flows_(), linklocal_vm_flows_(),
        flow_cache_timeout_(), flow_index_sm_log_count_(),
        flow_add_tokens_(Agent::kFlowAddTokens),
        flow_ksync_tokens_(Agent::kFlowKSyncTokens),
        flow_del_tokens_(Agent::kFlowDelTokens),
        flow_update_tokens_(Agent::kFlowUpdateTokens),
        stale_interface_cleanup_timeout_
        (Agent::kDefaultStaleInterfaceCleanupTimeout),
        config_file_(), program_name_(),
        log_file_(), log_local_(false), log_flow_(false), log_level_(),
        log_category_(), use_syslog_(false),
        http_server_port_(), host_name_(),
        agent_stats_interval_(kAgentStatsInterval),
        flow_stats_interval_(kFlowStatsInterval),
        vrouter_stats_interval_(kVrouterStatsInterval),
        vmware_physical_port_(""), test_mode_(false), tree_(),
        vgw_config_table_(new VirtualGatewayConfigTable() ),
        headless_mode_(false), dhcp_relay_mode_(false),
        xmpp_auth_enable_(false),
        xmpp_server_cert_(""), xmpp_server_key_(""), xmpp_ca_cert_(""),
        xmpp_dns_auth_enable_(false),
        simulate_evpn_tor_(false), si_netns_command_(),
        si_docker_command_(), si_netns_workers_(0),
        si_netns_timeout_(0), si_lb_ssl_cert_path_(), si_lbaas_auth_conf_(),
        vmware_mode_(ESXI_NEUTRON), nexthop_server_endpoint_(),
        nexthop_server_add_pid_(0),
        vrouter_on_nic_mode_(false),
        exception_packet_interface_(""),
        platform_(VROUTER_ON_HOST),
        physical_interface_pci_addr_(""),
        physical_interface_mac_addr_(""),
        agent_base_dir_(),
        send_ratelimit_(sandesh_send_rate_limit()),
        flow_thread_count_(Agent::kDefaultFlowThreadCount),
        flow_trace_enable_(true),
        flow_hash_excl_rid_(false),
        flow_latency_limit_(Agent::kDefaultFlowLatencyLimit),
        subnet_hosts_resolvable_(true),
        bgp_as_a_service_port_range_("50000-50512"),
        services_queue_limit_(1024),
        bgpaas_max_shared_sessions_(4),
        tbb_thread_count_(Agent::kMaxTbbThreads),
        tbb_exec_delay_(0),
        tbb_schedule_delay_(0),
        tbb_keepawake_timeout_(Agent::kDefaultTbbKeepawakeTimeout),
        task_monitor_timeout_msec_(Agent::kDefaultTaskMonitorTimeout),
        default_nic_queue_(Agent::kInvalidQueueId),
        min_aap_prefix_len_(Agent::kMinAapPrefixLen) {
    // Set common command line arguments supported
    boost::program_options::options_description generic("Generic options");
    generic.add_options()
        ("help", "help message")
        ("config_file",
         opt::value<string>()->default_value(Agent::config_file_),
         "Configuration file")
        ("version", "Display version information")
        ("CONTROL-NODE.server",
         opt::value<std::vector<std::string> >()->multitoken(),
         "IP addresses of control nodes."
         " Max of 2 Ip addresses can be configured")
        ("DEFAULT.collectors",
         opt::value<std::vector<std::string> >()->multitoken(),
         "Collector server list")
        ("DEFAULT.derived_stats",
         opt::value<std::vector<std::string> >()->multitoken(),
         "Derived Stats Parameters")
        ("DEFAULT.flow_cache_timeout",
         opt::value<uint16_t>()->default_value(Agent::kDefaultFlowCacheTimeout),
         "Flow aging time in seconds")
        ("DEFAULT.stale_interface_cleanup_timeout",
         opt::value<uint32_t>(),
         "Stale Interface cleanup timeout")
        ("DEFAULT.hostname", opt::value<string>(),
         "Hostname of compute-node")
        ("DEFAULT.headless_mode", opt::value<bool>(),
         "Run compute-node in headless mode")
        ("DEFAULT.dhcp_relay_mode", opt::value<bool>(),
         "Enable / Disable DHCP relay of DHCP packets from virtual instance")
        ("DEFAULT.http_server_port",
         opt::value<uint16_t>()->default_value(ContrailPorts::HttpPortAgent()),
         "Sandesh HTTP listener port")
        ("DEFAULT.tunnel_type", opt::value<string>()->default_value("MPLSoGRE"),
         "Tunnel Encapsulation type <MPLSoGRE|MPLSoUDP|VXLAN>")
        ("DEFAULT.agent_mode", opt::value<string>(),
         "Run agent in vrouter / tsn / tor mode")
        ("DEFAULT.gateway_mode", opt::value<string>(),
          "Set gateway mode to server/ vcpe")
        ("DEFAULT.agent_base_directory", opt::value<string>(),
         "Base directory used by the agent")
        ("DISCOVERY.port", opt::value<uint32_t>()->default_value(DISCOVERY_SERVER_PORT),
         "Listen port of discovery server")
        ("DISCOVERY.server", opt::value<string>()->default_value("127.0.0.1"),
         "IP address of discovery server")
        ("DISCOVERY.ssl", opt::bool_switch(&dss_ssl_),
             "ssl enabled for discovery")
        ("DISCOVERY.cert",
             opt::value<string>()->default_value(
             "/etc/contrail/ssl/certs/server.pem"),
             "discovery Server ssl certificate")
        ("DISCOVERY.key",
             opt::value<string>()->default_value(
             "/etc/contrail/ssl/private/server-privkey.pem"),
             "discovery Server ssl private key")
        ("DISCOVERY.cacert",
             opt::value<string>()->default_value(
             "/etc/contrail/ssl/certs/ca-cert.pem"),
             "discovery CA ssl certificate")
        ("DISCOVERY.max_control_nodes", 
         opt::value<uint16_t>()->default_value(MAX_XMPP_SERVERS),
         "Maximum number of control node info to be provided by discovery "
         "service <1|2>")
        ("DNS.server", opt::value<std::vector<std::string> >()->multitoken(),
         "IP addresses of dns nodes. Max of 2 Ip addresses can be configured")
        ("DEFAULT.xmpp_auth_enable", opt::value<bool>()->default_value(false),
         "Enable Xmpp over TLS")
        ("DEFAULT.xmpp_server_cert",
          opt::value<string>()->default_value(
          "/etc/contrail/ssl/certs/server.pem"),
          "XMPP Server ssl certificate")
        ("DEFAULT.xmpp_server_key",
          opt::value<string>()->default_value(
          "/etc/contrail/ssl/private/server-privkey.pem"),
          "XMPP Server ssl private key")
        ("DEFAULT.xmpp_ca_cert",
          opt::value<string>()->default_value(
          "/etc/contrail/ssl/certs/ca.pem"),
          "XMPP CA ssl certificate")
        ("DEFAULT.xmpp_dns_auth_enable", opt::value<bool>()->default_value(false),
         "Enable Xmpp over TLS for DNS")
        ("METADATA.metadata_proxy_secret", opt::value<string>(),
         "Shared secret for metadata proxy service")
        ("NETWORKS.control_network_ip", opt::value<string>(),
         "control-channel IP address used by WEB-UI to connect to vnswad")
        ("DEFAULT.platform", opt::value<string>(),
         "Mode in which vrouter is running, option are dpdk or vnic")
        ("DEFAULT.sandesh_send_rate_limit",
         opt::value<uint32_t>()->default_value(
         g_sandesh_constants.DEFAULT_SANDESH_SEND_RATELIMIT),
         "Sandesh send rate limit in messages/sec")
        ("DEFAULT.subnet_hosts_resolvable",
          opt::value<bool>()->default_value(true))
        ("DEFAULT.pkt0_tx_buffers", opt::value<uint32_t>(),
         "Number of tx-buffers for pkt0 interface")
        ("DEFAULT.min_aap_prefix_len", opt::value<uint16_t>(),
         "Minimum prefix-len Allowed-address-pair entries")
        ;
    options_.add(generic);

    opt::options_description log("Logging options");
    log.add_options()
        ("DEFAULT.log_category", opt::value<string>()->default_value("*"),
         "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_file",
         opt::value<string>()->default_value(Agent::log_file_),
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
            ("FLOWS.thread_count", opt::value<uint16_t>(),
             "Number of threads for flow setup")
            ("FLOWS.max_vm_flows", opt::value<uint16_t>(),
             "Maximum flows allowed per VM - given as \% (in integer) of "
             "maximum system flows")
            ("FLOWS.max_system_linklocal_flows", opt::value<uint16_t>(),
             "Maximum number of link-local flows allowed across all VMs")
            ("FLOWS.max_vm_linklocal_flows", opt::value<uint16_t>(),
             "Maximum number of link-local flows allowed per VM")
            ("FLOWS.trace_enable", opt::value<bool>(),
             "Enable flow tracing")
            ("FLOWS.add_tokens", opt::value<uint32_t>(),
             "Number of add-tokens")
            ("FLOWS.ksync_tokens", opt::value<uint32_t>(),
             "Number of ksync-tokens")
            ("FLOWS.del_tokens", opt::value<uint32_t>(),
             "Number of delete-tokens")
            ("FLOWS.update_tokens", opt::value<uint32_t>(),
             "Number of update-tokens")
            ("FLOWS.hash_exclude_router_id", opt::value<bool>(),
             "Exclude router-id in hash calculation")
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
            ("VIRTUAL-HOST-INTERFACE.compute_node_address",
             opt::value<std::vector<std::string> >()->multitoken(),
             "List of addresses on compute node")
            ("VIRTUAL-HOST-INTERFACE.physical_port_routes",
             opt::value<std::vector<std::string> >()->multitoken(),
             "Static routes to be added on physical interface")
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


    opt::options_description tbb("TBB specific options");
    tbb.add_options()
        ("TASK.thread_count", opt::value<uint32_t>(),
         "Max number of threads used by TBB")
        ("TASK.log_exec_threshold", opt::value<uint32_t>(),
         "Log message if task takes more than threshold (msec) to execute")
        ("TASK.log_schedule_threshold", opt::value<uint32_t>(),
         "Log message if task takes more than threshold (msec) to schedule")
        ("TASK.tbb_keepawake_timeout", opt::value<uint32_t>(),
         "Timeout for the TBB keepawake timer")
        ("TASK.task_monitor_timeout", opt::value<uint32_t>(),
         "Timeout for the Task monitoring")
        ;
    options_.add(tbb);
}

AgentParam::~AgentParam() {
}
