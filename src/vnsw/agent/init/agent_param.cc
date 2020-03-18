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
#include <boost/thread/thread.hpp>

#include <base/bgp_as_service_utils.h>
#include <base/logging.h>
#include <base/misc_utils.h>
#include <base/options_util.h>

#include <contrail/pkt0_interface.h>

#include <sandesh/sandesh_trace.h>

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <vgw/cfg_vgw.h>

using namespace std;
using namespace boost::property_tree;
using boost::optional;
namespace opt = boost::program_options;
using namespace options::util;

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
        if (value.size() == 1) {
            boost::split(value, value[0], boost::is_any_of(" \t"));
        }
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
        if (value.size() == 1) {
            boost::split(value, value[0], boost::is_any_of(" \t"));
        }
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

void AgentParam::set_agent_mode(const std::string &mode) {
    std::string agent_mode = boost::to_lower_copy(mode);
    if (agent_mode == "tsn")
        agent_mode_ = TSN_AGENT;
    else if (agent_mode == "tsn-no-forwarding")
        agent_mode_ = TSN_NO_FORWARDING_AGENT;
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
    else if (gateway_mode == "pbb")
        gateway_mode_ = PBB;
    else
        gateway_mode_ = NONE;
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
                    nic_queue_list_.insert(queue);
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
    GetValueFromTree<uint32_t>(task_monitor_timeout_msec_,
                               "TASK.task_monitor_timeout");
}

void AgentParam::ParseSessionDestinationArguments
    (const boost::program_options::variables_map &var_map) {
    slo_destination_.clear();
    sample_destination_.clear();
    GetOptValue< vector<string> >(var_map, slo_destination_,
                                  "SESSION.slo_destination");
    GetOptValue< vector<string> >(var_map, sample_destination_,
                                  "SESSION.sample_destination");
    //validate the string
    std::set<string> valid_dest_values;
    valid_dest_values.insert("collector");
    valid_dest_values.insert("file");
    valid_dest_values.insert("syslog");
    valid_dest_values.insert("");
    for (uint32_t i=0; i<slo_destination_.size(); i++) {
        assert(valid_dest_values.find(slo_destination_[i]) !=
            valid_dest_values.end());
    }
    for (uint32_t i=0; i<sample_destination_.size(); i++) {
        assert(valid_dest_values.find(sample_destination_[i]) !=
            valid_dest_values.end());
    }
}

void AgentParam::ParseCollectorArguments
    (const boost::program_options::variables_map &var_map) {
    collector_server_list_.clear();
    GetOptValueIfNotDefaulted< vector<string> >(var_map, collector_server_list_,
                                  "DEFAULT.collectors");
    if (collector_server_list_.size() == 1) {
        boost::split(collector_server_list_, collector_server_list_[0],
                     boost::is_any_of(" "));
    }
}

void AgentParam::ParseControllerServersArguments
    (const boost::program_options::variables_map &var_map) {
    controller_server_list_.clear();
    GetOptValueIfNotDefaulted< vector<string> >(var_map, controller_server_list_,
                                  "CONTROL-NODE.servers");
    if (controller_server_list_.size() == 1) {
        boost::split(controller_server_list_, controller_server_list_[0],
                     boost::is_any_of(" "));
    }
    GetOptValue<string>(var_map, subcluster_name_, "CONTROL-NODE.subcluster_name");
}

void AgentParam::ParseDnsServersArguments
    (const boost::program_options::variables_map &var_map) {
    dns_server_list_.clear();
    GetOptValueIfNotDefaulted< vector<string> >(var_map, dns_server_list_,
                                  "DNS.servers");
    if (dns_server_list_.size() == 1) {
        boost::split(dns_server_list_, dns_server_list_[0],
                     boost::is_any_of(" "));
    }
}

void AgentParam::ParseTsnServersArguments
    (const boost::program_options::variables_map &var_map) {
    tsn_server_list_.clear();
    GetOptValueIfNotDefaulted< vector<string> >(var_map, tsn_server_list_,
                                  "DEFAULT.tsn_servers");
    if (tsn_server_list_.size() == 1) {
        boost::split(tsn_server_list_, tsn_server_list_[0],
                     boost::is_any_of(" "));
    }
    std::sort(tsn_server_list_.begin(), tsn_server_list_.end());
}

void AgentParam::ParseCollectorDSArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValueIfNotDefaulted< vector<string> >(var_map, collector_server_list_,
                                      "DEFAULT.collectors");
    vector<string> dsvec;
    if (GetOptValueIfNotDefaulted< vector<string> >(var_map, dsvec,
                                      "DEFAULT.derived_stats")) {
        derived_stats_map_ = ParseDerivedStats(dsvec);
    }
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
    GetOptValue<uint16_t>(var_map, dns_client_port_, "DNS.dns_client_port");
    GetOptValue<uint32_t>(var_map, dns_timeout_, "DNS.dns_timeout");
    GetOptValue<uint32_t>(var_map, dns_max_retries_, "DNS.dns_max_retries");
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
    if (!GetValueFromTree<uint16_t>(vmi_vm_vn_uve_interval_,
                                    "DEFAULT.vmi_vm_vn_uve_interval")) {
        vmi_vm_vn_uve_interval_ = Agent::kDefaultVmiVmVnUveInterval;
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
    GetOptValue<int>(var_map, log_files_count_, "DEFAULT.log_files_count");
    GetOptValue<long>(var_map, log_file_size_, "DEFAULT.log_file_size");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");
    GetOptValue<string>(var_map, syslog_facility_, "DEFAULT.syslog_facility");

    GetOptValue<bool>(var_map, xmpp_auth_enable_, "DEFAULT.xmpp_auth_enable");
    GetOptValue<bool>(var_map, xmpp_dns_auth_enable_,
                      "DEFAULT.xmpp_dns_auth_enable");
    GetOptValue<string>(var_map, xmpp_server_cert_, "DEFAULT.xmpp_server_cert");
    GetOptValue<string>(var_map, xmpp_server_key_,
                        "DEFAULT.xmpp_server_key");
    GetOptValue<string>(var_map, xmpp_ca_cert_, "DEFAULT.xmpp_ca_cert");

    GetOptValue<bool>(var_map, subnet_hosts_resolvable_,
                      "DEFAULT.subnet_hosts_resolvable");
    GetOptValue<uint16_t>(var_map, mirror_client_port_,
                          "DEFAULT.mirror_client_port");
    GetOptValue<uint32_t>(var_map, pkt0_tx_buffer_count_,
                          "DEFAULT.pkt0_tx_buffers");
    GetOptValue<bool>(var_map, measure_queue_delay_,
                      "DEFAULT.measure_queue_delay");
    GetOptValue<string>(var_map, tunnel_type_,
                        "DEFAULT.tunnel_type");
    GetOptValue<uint16_t>(var_map, min_aap_prefix_len_,
                         "DEFAULT.min_aap_prefix_len");
    GetOptValue<uint16_t>(var_map, vmi_vm_vn_uve_interval_,
                          "DEFAULT.vmi_vm_vn_uve_interval");
    GetOptValue<bool>(var_map, mvpn_ipv4_enable_,
                          "DEFAULT.mvpn_ipv4_enable");
    float high_watermark = 0;
    if (GetOptValue<float>(var_map, high_watermark, "DEFAULT.vr_object_high_watermark")) {
        vr_object_high_watermark_ = high_watermark;
    }
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
    GetOptValue<string>(var_map, ksync_thread_cpu_pin_policy_,
                        "TASK.ksync_thread_cpu_pin_policy");
    GetOptValue<uint32_t>(var_map, flow_netlink_pin_cpuid_,
                        "TASK.flow_netlink_pin_cpuid");
}

void AgentParam::ParseMetadataProxyArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, metadata_shared_secret_,
                        "METADATA.metadata_proxy_secret");
    GetOptValue<uint16_t>(var_map, metadata_proxy_port_,
                        "METADATA.metadata_proxy_port");
    GetOptValue<bool>(var_map, metadata_use_ssl_,
                        "METADATA.metadata_use_ssl");
    GetOptValue<string>(var_map, metadata_client_cert_,
                        "METADATA.metadata_client_cert");
    GetOptValue<string>(var_map, metadata_client_cert_type_,
                        "METADATA.metadata_client_cert_type");
    GetOptValue<string>(var_map, metadata_client_key_,
                        "METADATA.metadata_client_key");
    GetOptValue<string>(var_map, metadata_ca_cert_,
                        "METADATA.metadata_ca_cert");
}

void AgentParam::ParseFlowArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint16_t>(var_map, flow_thread_count_,
                          "FLOWS.thread_count");
    GetOptValue<uint16_t>(var_map, flow_latency_limit_,
                          "FLOWS.latency_limit");
    GetOptValue<bool>(var_map, flow_trace_enable_, "FLOWS.trace_enable");
    float val = 0;
    if (GetOptValue<float>(var_map, val, "FLOWS.max_vm_flows")) {
        max_vm_flows_ = val;
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
    GetOptValue<bool>(var_map, flow_hash_excl_rid_,
                      "FLOWS.hash_exclude_router_id");
    GetOptValue<uint16_t>(var_map, max_sessions_per_aggregate_,
                          "FLOWS.max_sessions_per_aggregate");
    GetOptValue<uint16_t>(var_map, max_aggregates_per_session_endpoint_,
                          "FLOWS.max_aggregates_per_session_endpoint");
    GetOptValue<uint16_t>(var_map, max_endpoints_per_session_msg_,
                          "FLOWS.max_endpoints_per_session_msg");
    GetOptValue<uint16_t>(var_map, fabric_snat_hash_table_size_,
                          "FLOWS.fabric_snat_hash_table_size");
}

void AgentParam::ParseDhcpRelayModeArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<bool>(var_map, dhcp_relay_mode_, "DEFAULT.dhcp_relay_mode");
}

void AgentParam::ParseSimulateEvpnTorArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<bool>(var_map, simulate_evpn_tor_, "DEFAULT.simulate_evpn_tor");
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

//this has been intentionally seperated from platform to provide an easier mix and match in the future if necessary
void AgentParam::ParseTestFrameworkArguments(
    const boost::program_options::variables_map &var_map) {
    GetOptValue<bool>(var_map, AgentMock_, "DEFAULT.agent_mock");

    if (AgentMock_) {
       GetOptValue<bool>(var_map, cat_MockDPDK_,
          "AGENT-TEST-FRAMEWORK.mock_dpdk");
       GetOptValue<string>(var_map, cat_kSocketDir_,
          "AGENT-TEST-FRAMEWORK.ksocketdir");
    }

   if (!AgentMock_ && cat_MockDPDK_) {
      std::cout <<"Please fix conf file parameters."
          <<" The DPDK cannot be mocked unless the Agent"
          <<"  is also executed in the Mock mode" << std::endl;
      exit(1);
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

void AgentParam::ParseSandeshArguments
    (const boost::program_options::variables_map &v) {
    sandesh::options::ProcessOptions(v, &sandesh_config_);
}

void AgentParam::ParseRestartArguments
    (const boost::program_options::variables_map &v) {
    GetOptValue<bool>(v, restart_backup_enable_, "RESTART.backup_enable");
    GetOptValue<uint64_t>(v, restart_backup_idle_timeout_,
                          "RESTART.backup_idle_timeout");
    GetOptValue<string>(v, restart_backup_dir_, "RESTART.backup_dir");
    GetOptValue<uint16_t>(v, restart_backup_count_, "RESTART.backup_count");

    GetOptValue<bool>(v, restart_restore_enable_, "RESTART.restore_enable");
    GetOptValue<uint64_t>(v, restart_restore_audit_timeout_,
                          "RESTART.restore_audit_timeout");
    huge_page_file_1G_.clear();
    GetOptValueIfNotDefaulted< vector<string> >(v, huge_page_file_1G_,
                                                "RESTART.huge_page_1G");
    if (huge_page_file_1G_.size() == 1) {
        boost::split(huge_page_file_1G_, huge_page_file_1G_[0],
                     boost::is_any_of(" "));
    }
    huge_page_file_2M_.clear();
    GetOptValueIfNotDefaulted< vector<string> >(v, huge_page_file_2M_,
                                                "RESTART.huge_page_2M");
    if (huge_page_file_2M_.size() == 1) {
        boost::split(huge_page_file_2M_, huge_page_file_2M_[0],
                     boost::is_any_of(" "));
    }
}

void AgentParam::ParseLlgrArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint16_t>(var_map, llgr_params_.stale_config_cleanup_time_,
                          "LLGR.stale_config_cleanup_time_");
    GetOptValue<uint16_t>(var_map, llgr_params_.config_inactivity_time_,
                          "LLGR.config_inactivity_time");
    GetOptValue<uint16_t>(var_map, llgr_params_.config_fallback_time_,
                          "LLGR.config_fallback_time");
    GetOptValue<uint16_t>(var_map, llgr_params_.end_of_rib_rx_fallback_time_,
                          "LLGR.end_of_rib_rx_fallback_time");
    GetOptValue<uint16_t>(var_map, llgr_params_.end_of_rib_tx_fallback_time_,
                          "LLGR.end_of_rib_tx_fallback_time");
    GetOptValue<uint16_t>(var_map, llgr_params_.end_of_rib_tx_inactivity_time_,
                          "LLGR.end_of_rib_tx_inactivity_time_");
    GetOptValue<uint32_t>(var_map, llgr_params_.llgr_stale_time_,
                          "LLGR.llgr_stale_time");
}

void AgentParam::ParseMacLearning
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<uint32_t>(var_map, mac_learning_thread_count_,
                          "MAC-LEARNING.thread_count");
    GetOptValue<uint32_t>(var_map, mac_learning_add_tokens_,
                          "MAC-LEARNING.add_tokens");
    GetOptValue<uint32_t>(var_map, mac_learning_delete_tokens_,
                          "MAC-LEARNING.del_tokens");
    GetOptValue<uint32_t>(var_map, mac_learning_update_tokens_,
                          "MAC-LEARNING.update_tokens");
}

void AgentParam::ParseCryptArguments
    (const boost::program_options::variables_map &var_map) {
    GetOptValue<string>(var_map, crypt_port_,
                        "CRYPT.crypt_interface");
}

void AgentParam::ParseTraceArguments
(const boost::program_options::variables_map &var_map) {
    trace_buff_size_map.clear();
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["BgpAsAService"],
                          "TRACEBUFFSIZE.BgpAsAService");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["AgentDBwalkTrace"],
                          "TRACEBUFFSIZE.AgentDBwalkTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["BgpAsAService"],
                          "TRACEBUFFSIZE.BgpAsAService");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["CryptTunnel"],
                          "TRACEBUFFSIZE.CryptTunnel");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["HealthCheck"],
                          "TRACEBUFFSIZE.HealthCheck");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["MplsTrace"],
                          "TRACEBUFFSIZE.MplsTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Multicast"],
                          "TRACEBUFFSIZE.Multicast");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["InstanceManager"],
                          "TRACEBUFFSIZE.InstanceManager");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["OperIfmap"],
                          "TRACEBUFFSIZE.OperIfmap");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["PathPreference"],
                          "TRACEBUFFSIZE.PathPreference");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["MulticastPolicy"],
                          "TRACEBUFFSIZE.MulticastPolicy");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["TaskTrace"],
                          "TRACEBUFFSIZE.TaskTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["InterfaceMplsData"],
                          "TRACEBUFFSIZE.InterfaceMplsData");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["VrfMplsData"],
                          "TRACEBUFFSIZE.VrfMplsData");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["VlanMplsData"],
                          "TRACEBUFFSIZE.VlanMplsData");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["RouteMplsData"],
                          "TRACEBUFFSIZE.RouteMplsData");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["VersionTrace"],
                          "TRACEBUFFSIZE.VersionTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["DnsBind"],
                          "TRACEBUFFSIZE.DnsBind");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["IFMapAgentTrace"],
                          "TRACEBUFFSIZE.IFMapAgentTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["IOTraceBuf"],
                          "TRACEBUFFSIZE.IOTraceBuf");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["XmppMessageTrace"],
                          "TRACEBUFFSIZE.XmppMessageTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["XmppTrace"],
                          "TRACEBUFFSIZE.XmppTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Config"],
                          "TRACEBUFFSIZE.Config");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["ControllerConnections"],
                          "TRACEBUFFSIZE.ControllerConnections");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["ControllerInfo"],
                          "TRACEBUFFSIZE.ControllerInfo");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["ControllerTxConfig_1"],
                          "TRACEBUFFSIZE.ControllerTxConfig_1");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["ControllerTxConfig_2"],
                          "TRACEBUFFSIZE.ControllerTxConfig_2");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["ControllerRouteWalker"],
                          "TRACEBUFFSIZE.ControllerRouteWalker");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Controller"],
                          "TRACEBUFFSIZE.Controller");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["ControllerRxRouteXmppMessage1"],
                          "TRACEBUFFSIZE.ControllerRxRouteXmppMessage1");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["ControllerRxConfigXmppMessage1"],
                          "TRACEBUFFSIZE.ControllerRxConfigXmppMessage1");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["ControllerRxRouteXmppMessage2"],
                          "TRACEBUFFSIZE.ControllerRxRouteXmppMessage2");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["ControllerRxConfigXmppMessage2"],
                          "TRACEBUFFSIZE.ControllerRxConfigXmppMessage2");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["ControllerTxXmppMessage_1"],
                          "TRACEBUFFSIZE.ControllerTxXmppMessage_1");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["ControllerTxXmppMessage_2"],
                          "TRACEBUFFSIZE.ControllerTxXmppMessage_2");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Acl"],
                          "TRACEBUFFSIZE.Acl");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["VnswIfTrace"],
                          "TRACEBUFFSIZE.VnswIfTrace");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Flow"],
                          "TRACEBUFFSIZE.Flow");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Packet"],
                          "TRACEBUFFSIZE.Packet");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["FlowHandler"],
                          "TRACEBUFFSIZE.FlowHandler");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["ProuterUve"],
                          "TRACEBUFFSIZE.ProuterUve");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["SessionStats"],
                          "TRACEBUFFSIZE.SessionStats");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["FlowExportStats"],
                          "TRACEBUFFSIZE.FlowExportStats");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Dhcp"],
                          "TRACEBUFFSIZE.Dhcp");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Dhcpv6"],
                          "TRACEBUFFSIZE.Dhcpv6");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Icmpv6"],
                          "TRACEBUFFSIZE.Icmpv6");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Arp"],
                          "TRACEBUFFSIZE.Arp");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Metadata"],
                          "TRACEBUFFSIZE.Metadata");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Bfd"],
                          "TRACEBUFFSIZE.Bfd");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Igmp"],
                          "TRACEBUFFSIZE.Igmp");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync Error"],
                          "TRACEBUFFSIZE.KSync Error");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["MacLearning"],
                          "TRACEBUFFSIZE.MacLearning");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Xmpp"],
                          "TRACEBUFFSIZE.Xmpp");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync Interface"],
                          "TRACEBUFFSIZE.KSync Interface");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync Mpls"],
                          "TRACEBUFFSIZE.KSync Mpls");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync Nexthop"],
                          "TRACEBUFFSIZE.KSync Nexthop");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync Mirror"],
                          "TRACEBUFFSIZE.KSync Mirror");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync VxLan"],
                          "TRACEBUFFSIZE.KSync VxLan");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync VrfAssign"],
                          "TRACEBUFFSIZE.KSync VrfAssign");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync Qos Queue Object"],
                          "TRACEBUFFSIZE.KSync Qos Queue Object");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["KSync Forwarding class object"],
                          "TRACEBUFFSIZE.KSync Forwarding class object");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["KSync Qos Config class object"],
                          "TRACEBUFFSIZE.KSync Qos Config class object");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync BridgeRouteTable"],
                          "TRACEBUFFSIZE.KSync BridgeRouteTable");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync FlowTable"],
                          "TRACEBUFFSIZE.KSync FlowTable");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.interface.0"],
                          "TRACEBUFFSIZE.Oper db.interface.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.healthcheck.0"],
                          "TRACEBUFFSIZE.Oper db.healthcheck.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.crypttunnel.0"],
                          "TRACEBUFFSIZE.Oper db.crypttunnel.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.nexthop.0"],
                          "TRACEBUFFSIZE.Oper db.nexthop.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.vrf.0"],
                          "TRACEBUFFSIZE.Oper db.vrf.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.vm.0"],
                          "TRACEBUFFSIZE.Oper db.vm.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.sg.0"],
                          "TRACEBUFFSIZE.Oper db.sg.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.tag.0"],
                          "TRACEBUFFSIZE.Oper db.tag.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.vn.0"],
                          "TRACEBUFFSIZE.Oper db.vn.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.mpls.0"],
                          "TRACEBUFFSIZE.Oper db.mpls.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.acl.0"],
                          "TRACEBUFFSIZE.Oper db.acl.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.mirror_table.0"],
                          "TRACEBUFFSIZE.Oper db.mirror_table.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.vrf_assign.0"],
                          "TRACEBUFFSIZE.Oper db.vrf_assign.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.policy_set.0"],
                          "TRACEBUFFSIZE.Oper db.policy_set.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.vxlan.0"],
                          "TRACEBUFFSIZE.Oper db.vxlan.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.qos_queue.0"],
                          "TRACEBUFFSIZE.Oper db.qos_queue.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.forwardingclass.0"],
                          "TRACEBUFFSIZE.Oper db.forwardingclass.0");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["Oper db.security_logging_object.0"],
                          "TRACEBUFFSIZE.Oper db.security_logging_object.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.qos_config.0"],
                          "TRACEBUFFSIZE.Oper db.qos_config.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.bridge_domain.0"],
                          "TRACEBUFFSIZE.Oper db.bridge_domain.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.multicast_policy.0"],
                          "TRACEBUFFSIZE.Oper db.multicast_policy.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.service-instance.0"],
                          "TRACEBUFFSIZE.Oper db.service-instance.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["Oper db.physical_devices.0"],
                          "TRACEBUFFSIZE.Oper db.physical_devices.0");
    GetOptValue<uint32_t>(var_map,
                          trace_buff_size_map["Oper db.physical_device_vn.0"],
                          "TRACEBUFFSIZE.Oper db.physical_device_vn.0");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["httpbuf"],
                          "TRACEBUFFSIZE.httpbuf");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["OperRoute"],
                          "TRACEBUFFSIZE.OperRoute");
    GetOptValue<uint32_t>(var_map, trace_buff_size_map["KSync Route"],
                          "TRACEBUFFSIZE.KSync Route");
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
    ifstream config_file_in;
    config_file_in.open(config_file_.c_str());
    if (config_file_in.good()) {
        opt::basic_parsed_options<char> ParsedOptions = opt::parse_config_file(config_file_in, config_file_options_, true);
        boost::program_options::store(ParsedOptions,
                   var_map_);
        boost::program_options::notify(var_map_);
        std::vector<boost::program_options::basic_option<char> >::iterator it;
        for (it=ParsedOptions.options.begin() ; it < ParsedOptions.options.end(); ++it) {
            if (it->unregistered) {
                tree_.put(it->string_key,it->value.at(0));
            }
        }
    }
    config_file_in.close();
    cout << "Config file <" << config_file_ << "> parsing completed.\n";
    return;
}

void AgentParam::ProcessArguments() {
    ParseCollectorDSArguments(var_map_);
    ParseVirtualHostArguments(var_map_);
    ParseControllerServersArguments(var_map_);
    ParseDnsServersArguments(var_map_);
    ParseDnsArguments(var_map_);
    ParseNetworksArguments(var_map_);
    ParseHypervisorArguments(var_map_);
    ParseDefaultSectionArguments(var_map_);
    ParseTaskSectionArguments(var_map_);
    ParseFlowArguments(var_map_);
    ParseMetadataProxyArguments(var_map_);
    ParseDhcpRelayModeArguments(var_map_);
    ParseServiceInstanceArguments(var_map_);
    ParseSimulateEvpnTorArguments(var_map_);
    ParseAgentInfoArguments(var_map_);
    ParseNexthopServerArguments(var_map_);
    ParsePlatformArguments(var_map_);
    ParseServicesArguments(var_map_);
    ParseTestFrameworkArguments(var_map_);//sagarc
    ParseSandeshArguments(var_map_);
    ParseQueue();
    ParseRestartArguments(var_map_);
    ParseMacLearning(var_map_);
    ParseTsnServersArguments(var_map_);
    ParseCryptArguments(var_map_);
    ParseSessionDestinationArguments(var_map_);
    ParseTraceArguments(var_map_);
}

void AgentParam::DebugInitFromConfig() {
    // Read and parse INI
    opt::variables_map var_map;
    ifstream config_file_in;
    config_file_in.open(config_file_.c_str());
    if (config_file_in.good()) {
        opt::basic_parsed_options<char> ParsedOptions =
            opt::parse_config_file(config_file_in, config_file_options_, true);
        boost::program_options::store(ParsedOptions, var_map);

        ParseTraceArguments(var_map);
    }
    config_file_in.close();
    LOG(INFO, "Config file parsing for debug params completed. \n");
    return;
}

void AgentParam::ReInitFromConfig() {

    // Read and parse INI
    opt::variables_map var_map;
    ifstream config_file_in;
    config_file_in.open(config_file_.c_str());
    if (config_file_in.good()) {
        opt::basic_parsed_options<char> ParsedOptions =
            opt::parse_config_file(config_file_in, config_file_options_, true);
        boost::program_options::store(ParsedOptions, var_map);

        ParseControllerServersArguments(var_map);
        ParseDnsServersArguments(var_map);
        ParseCollectorArguments(var_map);
        ParseTsnServersArguments(var_map);
        ParseSessionDestinationArguments(var_map);

        LogFilteredConfig();
    }
    config_file_in.close();
    LOG(DEBUG, "Config file re-parsing completed. \n");

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

void AgentParam::ComputeVrWatermark() {
    if (vr_object_high_watermark_ > 95) {
        cout << "Max is 95 updating vrouter objects high watermark to max : 95%\n";
        vr_object_high_watermark_ = 95;
    }
    if (vr_object_high_watermark_ < 50) {
        cout << "Wrong config updating vrouter objects high watermark to lowest : 50%\n";
        vr_object_high_watermark_ = 50;
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
    strncpy(ifr.ifr_name, ifname.c_str(), IF_NAMESIZE-1);
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
        fclose(f);
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

    if (crypt_port_ != "") {
       // Check if interface is already present
       if (ValidateInterface(test_mode_, crypt_port_, &crypt_port_no_arp_,
                             &crypt_port_encap_type_) == false) {
           return (ENODEV);
       }
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

bool AgentParam::IsConfiguredTsnHostRoute(std::string addr) const {
    return (std::find(tsn_server_list_.begin(), tsn_server_list_.end(), addr) !=
            tsn_server_list_.end());
}

void AgentParam::Init(const string &config_file, const string &program_name) {

    config_file_ = config_file;
    program_name_ = program_name;
    InitFromSystem();
    InitFromConfig();
    ProcessArguments();
    ProcessTraceArguments();
    InitVhostAndXenLLPrefix();
    UpdateBgpAsaServicePortRangeValue();
    ComputeFlowLimits();
    ComputeVrWatermark();
    vgw_config_table_->InitFromConfig(tree_);
}

void AgentParam::ReInit() {
    ReInitFromConfig();
}

void SetTraceBufferSize(const string &tracebuff, size_t size) {
    SandeshTraceBufferPtr tb = SandeshTraceBufferGet(tracebuff);
    if (tb && size && SandeshTraceBufferCapacityGet(tracebuff) != size) {
        //Disable the trace before changing the buffer size
        SandeshTraceBufferDisable(tb);
        SandeshTraceBufferPtr trace_buf = SandeshTraceBufferResetSize(tracebuff,
                                                                      size);
        //Allow 30ms before enabling the trace back
        boost::this_thread::sleep( boost::posix_time::milliseconds(30));
        //Enable the trace
        SandeshTraceBufferEnable(trace_buf);

        LOG(INFO, "Trace Buffer size for " << tracebuff << " set to " << size);
    }
}

void AgentParam::ProcessTraceArguments() {
    LOG(INFO, "Processing Trace Buffer size settings");
    for (trace_buff_size_iter = trace_buff_size_map.begin();
         trace_buff_size_iter != trace_buff_size_map.end();
         trace_buff_size_iter++) {
        SetTraceBufferSize(trace_buff_size_iter->first,
                           trace_buff_size_iter->second);
    }
}

void AgentParam::DebugInit() {
    DebugInitFromConfig();
    ProcessTraceArguments();
}

void AgentParam::LogFilteredConfig() const {
    std::string concat_servers;
    std::vector<string> list = controller_server_list();
    std::vector<string>::iterator iter;
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "Xmpp Servers                : " << concat_servers);

    concat_servers.clear();
    list = dns_server_list();
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "DNS Servers                 : " << concat_servers);

    list = tsn_server_list();
    concat_servers.clear();
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "TSN Servers                 : " << concat_servers);

    concat_servers.clear();
    list = collector_server_list();
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "COLLECTOR Servers                 : " << concat_servers);
}

void AgentParam::LogConfig() const {
    LOG(DEBUG, "vhost interface name        : " << vhost_.name_);
    LOG(DEBUG, "vhost IP Address            : " << vhost_.addr_.to_string()
        << "/" << vhost_.plen_);
    LOG(DEBUG, "vhost gateway               : " << vhost_.gw_.to_string());
    LOG(DEBUG, "Ethernet port               : " << eth_port_);

    std::string concat_servers;
    std::vector<string> list = controller_server_list();
    std::vector<string>::iterator iter;
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "Xmpp Servers                : " << concat_servers);
    LOG(DEBUG, "Xmpp Authentication         : " << xmpp_auth_enable_);
    if (xmpp_auth_enable_) {
        LOG(DEBUG, "Xmpp Server Certificate : " << xmpp_server_cert_);
        LOG(DEBUG, "Xmpp Server Key         : " << xmpp_server_key_);
        LOG(DEBUG, "Xmpp CA Certificate     : " << xmpp_ca_cert_);
    }
    LOG(DEBUG, "Cluster-Name : " << subcluster_name_);

    concat_servers.clear();
    list = dns_server_list();
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "DNS Servers                 : " << concat_servers);
    LOG(DEBUG, "DNS client port             : " << dns_client_port_);
    LOG(DEBUG, "DNS timeout                 : " << dns_timeout_);
    LOG(DEBUG, "DNS max retries             : " << dns_max_retries_);
    LOG(DEBUG, "Xmpp Dns Authentication     : " << xmpp_dns_auth_enable_);
    if (xmpp_dns_auth_enable_) {
        LOG(DEBUG, "Xmpp Server Certificate : " << xmpp_server_cert_);
        LOG(DEBUG, "Xmpp Server Key         : " << xmpp_server_key_);
        LOG(DEBUG, "Xmpp CA Certificate     : " << xmpp_ca_cert_);
    }

    concat_servers.clear();
    list = tsn_server_list();
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "TSN Servers                 : " << concat_servers);
    concat_servers.clear();

    list = collector_server_list();
    for (iter = list.begin();
         iter != list.end(); iter++) {
         concat_servers += *iter + " ";
    }
    LOG(DEBUG, "COLLECTOR Servers                 : " << concat_servers);

    LOG(DEBUG, "Tunnel-Type                 : " << tunnel_type_);
    LOG(DEBUG, "Metadata-Proxy Shared Secret: " << metadata_shared_secret_);
    LOG(DEBUG, "Metadata-Proxy Port         : " << metadata_proxy_port_);
    LOG(DEBUG, "Metadata-Proxy SSL Flag     : " << metadata_use_ssl_);
    if (metadata_use_ssl_) {
        LOG(DEBUG, "Metadata Client Certificate     : " << metadata_client_cert_);
        LOG(DEBUG, "Metadata Client Certificate Type: "
            << metadata_client_cert_type_);
        LOG(DEBUG, "Metadata Client Key             : " << metadata_client_key_);
        LOG(DEBUG, "Metadata CA Certificate         : " << metadata_ca_cert_);
    }

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
    LOG(DEBUG, "Pin flow netlink task to CPU: "
        << ksync_thread_cpu_pin_policy_);
    LOG(DEBUG, "Maximum sessions            : " << max_sessions_per_aggregate_);
    LOG(DEBUG, "Maximum session aggregates  : " << max_aggregates_per_session_endpoint_);
    LOG(DEBUG, "Maximum session endpoints   : " << max_endpoints_per_session_msg_);
    LOG(DEBUG, "Fabric SNAT hash table size : " << fabric_snat_hash_table_size_);
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
    else if (gateway_mode_ == PBB)
        LOG(DEBUG, "Gateway Mode                : PBB node");
    else if (gateway_mode_ == NONE)
        LOG(DEBUG, "Gateway Mode                : None");

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

    LOG(DEBUG, "Sandesh Key file            : " << sandesh_config_.keyfile);
    LOG(DEBUG, "Sandesh Cert file           : " << sandesh_config_.certfile);
    LOG(DEBUG, "Sandesh CA Cert             : " << sandesh_config_.ca_cert);
    LOG(DEBUG, "Sandesh SSL Enable          : "
        << sandesh_config_.sandesh_ssl_enable);
    LOG(DEBUG, "Introspect SSL Enable       : "
        << sandesh_config_.introspect_ssl_enable);

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
    LOG(DEBUG, "Vrouter objects high watermark	: " << vr_object_high_watermark_);
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
    }
    else {
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

void AgentParam::ConfigAddOptions
(const boost::program_options::options_description &opt) {
    config_file_options_.add(opt);
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
        crypt_port_(), crypt_port_no_arp_(true), crypt_port_encap_type_(),
        subcluster_name_(),
        dns_client_port_(0), dns_timeout_(3000),
        dns_max_retries_(2), mirror_client_port_(0),
        mgmt_ip_(), hypervisor_mode_(MODE_KVM),
        xen_ll_(), tunnel_type_(), metadata_shared_secret_(),
        metadata_proxy_port_(0), metadata_use_ssl_(false),
        metadata_client_cert_(""), metadata_client_cert_type_("PEM"),
        metadata_client_key_(""), metadata_ca_cert_(""), max_vm_flows_(),
        linklocal_system_flows_(), linklocal_vm_flows_(),
        flow_cache_timeout_(), flow_index_sm_log_count_(),
        flow_add_tokens_(Agent::kFlowAddTokens),
        flow_ksync_tokens_(Agent::kFlowKSyncTokens),
        flow_del_tokens_(Agent::kFlowDelTokens),
        flow_update_tokens_(Agent::kFlowUpdateTokens),
        flow_netlink_pin_cpuid_(0),
        stale_interface_cleanup_timeout_
        (Agent::kDefaultStaleInterfaceCleanupTimeout),
        config_file_(), program_name_(),
        log_file_(), log_files_count_(kLogFilesCount),
        log_file_size_(kLogFileSize),
        log_local_(false), log_flow_(false), log_level_(),
        log_category_(), use_syslog_(false),
        http_server_port_(), host_name_(),
        agent_stats_interval_(kAgentStatsInterval),
        flow_stats_interval_(kFlowStatsInterval),
        vrouter_stats_interval_(kVrouterStatsInterval),
        vmware_physical_port_(""), test_mode_(false), tree_(),
        vgw_config_table_(new VirtualGatewayConfigTable() ),
        dhcp_relay_mode_(false), xmpp_auth_enable_(false),
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
        flow_thread_count_(Agent::kDefaultFlowThreadCount),
        flow_trace_enable_(true),
        flow_hash_excl_rid_(false),
        flow_latency_limit_(Agent::kDefaultFlowLatencyLimit),
        max_sessions_per_aggregate_(Agent::kMaxSessions),
        max_aggregates_per_session_endpoint_(Agent::kMaxSessionAggs),
        max_endpoints_per_session_msg_(Agent::kMaxSessionEndpoints),
        subnet_hosts_resolvable_(true),
        bgp_as_a_service_port_range_("50000-50512"),
        services_queue_limit_(1024),
        bgpaas_max_shared_sessions_(4),
        sandesh_config_(),
        restart_backup_enable_(false),
        restart_backup_idle_timeout_(CFG_BACKUP_IDLE_TIMEOUT),
        restart_backup_dir_(CFG_BACKUP_DIR),
        restart_backup_count_(CFG_BACKUP_COUNT),
        restart_restore_enable_(true),
        restart_restore_audit_timeout_(CFG_RESTORE_AUDIT_TIMEOUT),
        huge_page_file_1G_(),
        huge_page_file_2M_(),
        ksync_thread_cpu_pin_policy_(),
        tbb_thread_count_(Agent::kMaxTbbThreads),
        tbb_exec_delay_(0),
        tbb_schedule_delay_(0),
        tbb_keepawake_timeout_(Agent::kDefaultTbbKeepawakeTimeout),
        task_monitor_timeout_msec_(Agent::kDefaultTaskMonitorTimeout),
        qos_priority_tagging_(true),
        default_nic_queue_(Agent::kInvalidQueueId),
        llgr_params_(),
        mac_learning_thread_count_(Agent::kDefaultFlowThreadCount),
        mac_learning_add_tokens_(Agent::kMacLearningDefaultTokens),
        mac_learning_update_tokens_(Agent::kMacLearningDefaultTokens),
        mac_learning_delete_tokens_(Agent::kMacLearningDefaultTokens),
        min_aap_prefix_len_(Agent::kMinAapPrefixLen),
        vmi_vm_vn_uve_interval_(Agent::kDefaultVmiVmVnUveInterval),
        fabric_snat_hash_table_size_(Agent::kFabricSnatTableSize),
        mvpn_ipv4_enable_(false),AgentMock_(false), cat_MockDPDK_(false),
        cat_kSocketDir_("/tmp/"),
        vr_object_high_watermark_(Agent::kDefaultHighWatermark) {

    uint32_t default_pkt0_tx_buffers = Agent::kPkt0TxBufferCount;
    uint32_t default_stale_interface_cleanup_timeout = Agent::kDefaultStaleInterfaceCleanupTimeout;
    uint32_t default_flow_update_tokens = Agent::kFlowUpdateTokens;
    uint32_t default_flow_del_tokens = Agent::kFlowDelTokens;
    uint32_t default_flow_ksync_tokens = Agent::kFlowKSyncTokens;
    uint32_t default_flow_add_tokens = Agent::kFlowAddTokens;
    uint32_t default_tbb_keepawake_timeout = Agent::kDefaultTbbKeepawakeTimeout;
    uint32_t default_tbb_thread_count = Agent::kMaxTbbThreads;
    uint32_t default_mac_learning_thread_count = Agent::kDefaultFlowThreadCount;
    uint32_t default_mac_learning_add_tokens = Agent::kMacLearningDefaultTokens;
    uint32_t default_mac_learning_update_tokens = Agent::kMacLearningDefaultTokens;
    uint32_t default_mac_learning_delete_tokens = Agent::kMacLearningDefaultTokens;
    uint16_t default_fabric_snat_table_size = Agent::kFabricSnatTableSize;

    // Set common command line arguments supported
    boost::program_options::options_description generic("Generic options");
    generic.add_options()
        ("help", "help message")
        ("config_file",
         opt::value<string>()->default_value(Agent::config_file_),
         "Configuration file")
        ("version", "Display version information")
        ;
    opt::options_description debug("Debug options");
    debug.add_options()
        ("TRACEBUFFSIZE.AgentDBwalkTrace",opt::value<uint32_t>(),
          "AgentDBwalkTrace trace buffer size")
        ("TRACEBUFFSIZE.BgpAsAService",opt::value<uint32_t>(),
          "BgpAsAService trace buffer size")
        ("TRACEBUFFSIZE.CryptTunnel",opt::value<uint32_t>(),
          "CryptTunnel trace buffer size")
        ("TRACEBUFFSIZE.HealthCheck",opt::value<uint32_t>(),
          "HealthCheck trace buffer size")
        ("TRACEBUFFSIZE.MplsTrace",opt::value<uint32_t>(),
          "MplsTrace trace buffer size")
        ("TRACEBUFFSIZE.Multicast",opt::value<uint32_t>(),
          "Multicast trace buffer size")
        ("TRACEBUFFSIZE.InstanceManager",opt::value<uint32_t>(),
          "InstanceManager trace buffer size")
        ("TRACEBUFFSIZE.OperIfmap",opt::value<uint32_t>(),
          "OperIfmap trace buffer size")
        ("TRACEBUFFSIZE.PathPreference",opt::value<uint32_t>(),
          "PathPreference trace buffer size")
        ("TRACEBUFFSIZE.MulticastPolicy",opt::value<uint32_t>(),
          "MulticastPolicy trace buffer size")
        ("TRACEBUFFSIZE.TaskTrace",opt::value<uint32_t>(),
          "TaskTrace trace buffer size")
        ("TRACEBUFFSIZE.InterfaceMplsData",opt::value<uint32_t>(),
          "InterfaceMplsData trace buffer size")
        ("TRACEBUFFSIZE.VrfMplsData",opt::value<uint32_t>(),
          "VrfMplsData trace buffer size")
        ("TRACEBUFFSIZE.VlanMplsData",opt::value<uint32_t>(),
          "VlanMplsData trace buffer size")
        ("TRACEBUFFSIZE.RouteMplsData",opt::value<uint32_t>(),
          "RouteMplsData trace buffer size")
        ("TRACEBUFFSIZE.VersionTrace",opt::value<uint32_t>(),
          "VersionTrace trace buffer size")
        ("TRACEBUFFSIZE.DnsBind",opt::value<uint32_t>(),
          "DnsBind trace buffer size")
        ("TRACEBUFFSIZE.IFMapAgentTrace",opt::value<uint32_t>(),
          "IFMapAgentTrace trace buffer size")
        ("TRACEBUFFSIZE.IOTraceBuf",opt::value<uint32_t>(),
          "IOTraceBuf trace buffer size")
        ("TRACEBUFFSIZE.XmppMessageTrace",opt::value<uint32_t>(),
          "XmppMessageTrace trace buffer size")
        ("TRACEBUFFSIZE.XmppTrace",opt::value<uint32_t>(),
          "XmppTrace trace buffer size")
        ("TRACEBUFFSIZE.Config",opt::value<uint32_t>(),
          "Config trace buffer size")
        ("TRACEBUFFSIZE.ControllerConnections",opt::value<uint32_t>(),
          "ControllerConnections trace buffer size")
        ("TRACEBUFFSIZE.ControllerInfo",opt::value<uint32_t>(),
          "ControllerInfo trace buffer size")
        ("TRACEBUFFSIZE.ControllerTxConfig_1",opt::value<uint32_t>(),
          "ControllerTxConfig_1 trace buffer size")
        ("TRACEBUFFSIZE.ControllerTxConfig_2",opt::value<uint32_t>(),
          "ControllerTxConfig_2 trace buffer size")
        ("TRACEBUFFSIZE.ControllerRouteWalker",opt::value<uint32_t>(),
          "ControllerRouteWalker trace buffer size")
        ("TRACEBUFFSIZE.Controller",opt::value<uint32_t>(),
          "Controller trace buffer size")
        ("TRACEBUFFSIZE.ControllerRxRouteXmppMessage1",opt::value<uint32_t>(),
          "ControllerRxRouteXmppMessage1 trace buffer size")
        ("TRACEBUFFSIZE.ControllerRxConfigXmppMessage1",opt::value<uint32_t>(),
          "ControllerRxConfigXmppMessage1 trace buffer size")
        ("TRACEBUFFSIZE.ControllerRxRouteXmppMessage2",opt::value<uint32_t>(),
          "ControllerRxRouteXmppMessage2 trace buffer size")
        ("TRACEBUFFSIZE.ControllerRxConfigXmppMessage2",opt::value<uint32_t>(),
          "ControllerRxConfigXmppMessage2 trace buffer size")
        ("TRACEBUFFSIZE.ControllerTxXmppMessage_1",opt::value<uint32_t>(),
          "ControllerTxXmppMessage_1 trace buffer size")
        ("TRACEBUFFSIZE.ControllerTxXmppMessage_2",opt::value<uint32_t>(),
          "ControllerTxXmppMessage_2 trace buffer size")
        ("TRACEBUFFSIZE.Acl",opt::value<uint32_t>(),
          "Acl trace buffer size")
        ("TRACEBUFFSIZE.VnswIfTrace",opt::value<uint32_t>(),
          "VnswIfTrace trace buffer size")
        ("TRACEBUFFSIZE.Flow",opt::value<uint32_t>(),
          "Flow trace buffer size")
        ("TRACEBUFFSIZE.Packet",opt::value<uint32_t>(),
          "Packet trace buffer size")
        ("TRACEBUFFSIZE.FlowHandler",opt::value<uint32_t>(),
          "FlowHandler trace buffer size")
        ("TRACEBUFFSIZE.ProuterUve",opt::value<uint32_t>(),
          "ProuterUve trace buffer size")
        ("TRACEBUFFSIZE.SessionStats",opt::value<uint32_t>(),
          "SessionStats trace buffer size")
        ("TRACEBUFFSIZE.FlowExportStats",opt::value<uint32_t>(),
          "FlowExportStats trace buffer size")
        ("TRACEBUFFSIZE.Dhcp",opt::value<uint32_t>(),
          "Dhcp trace buffer size")
        ("TRACEBUFFSIZE.Dhcpv6",opt::value<uint32_t>(),
          "Dhcpv6 trace buffer size")
        ("TRACEBUFFSIZE.Icmpv6",opt::value<uint32_t>(),
          "Icmpv6 trace buffer size")
        ("TRACEBUFFSIZE.Arp",opt::value<uint32_t>(),
          "Arp trace buffer size")
        ("TRACEBUFFSIZE.Metadata",opt::value<uint32_t>(),
          "Metadata trace buffer size")
        ("TRACEBUFFSIZE.Bfd",opt::value<uint32_t>(),
          "Bfd trace buffer size")
        ("TRACEBUFFSIZE.Igmp",opt::value<uint32_t>(),
          "Igmp trace buffer size")
        ("TRACEBUFFSIZE.KSync Error",opt::value<uint32_t>(),
          "KSync Error trace buffer size")
        ("TRACEBUFFSIZE.MacLearning",opt::value<uint32_t>(),
          "MacLearning trace buffer size")
        ("TRACEBUFFSIZE.Xmpp",opt::value<uint32_t>(),
          "Xmpp trace buffer size")
        ("TRACEBUFFSIZE.KSync Interface",opt::value<uint32_t>(),
          "KSync Interface trace buffer size")
        ("TRACEBUFFSIZE.KSync Mpls",opt::value<uint32_t>(),
          "KSync Mpls trace buffer size")
        ("TRACEBUFFSIZE.KSync Nexthop",opt::value<uint32_t>(),
          "KSync Nexthop trace buffer size")
        ("TRACEBUFFSIZE.KSync Mirror",opt::value<uint32_t>(),
          "KSync Mirror trace buffer size")
        ("TRACEBUFFSIZE.KSync VxLan",opt::value<uint32_t>(),
          "KSync VxLan trace buffer size")
        ("TRACEBUFFSIZE.KSync VrfAssign",opt::value<uint32_t>(),
          "KSync VrfAssign trace buffer size")
        ("TRACEBUFFSIZE.KSync Qos Queue Object",opt::value<uint32_t>(),
          "KSync Qos Queue Object trace buffer size")
        ("TRACEBUFFSIZE.KSync Forwarding class object",opt::value<uint32_t>(),
          "KSync Forwarding class object trace buffer size")
        ("TRACEBUFFSIZE.KSync Qos Config class object",opt::value<uint32_t>(),
          "KSync Qos Config class object trace buffer size")
        ("TRACEBUFFSIZE.KSync BridgeRouteTable",opt::value<uint32_t>(),
          "KSync BridgeRouteTable trace buffer size")
        ("TRACEBUFFSIZE.KSync FlowTable",opt::value<uint32_t>(),
          "KSync FlowTable trace buffer size")
        ("TRACEBUFFSIZE.Oper db.interface.0",opt::value<uint32_t>(),
          "Oper db.interface.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.healthcheck.0",opt::value<uint32_t>(),
          "Oper db.healthcheck.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.crypttunnel.0",opt::value<uint32_t>(),
          "Oper db.crypttunnel.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.nexthop.0",opt::value<uint32_t>(),
          "Oper db.nexthop.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.vrf.0",opt::value<uint32_t>(),
          "Oper db.vrf.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.vm.0",opt::value<uint32_t>(),
          "Oper db.vm.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.sg.0",opt::value<uint32_t>(),
          "Oper db.sg.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.tag.0",opt::value<uint32_t>(),
          "Oper db.tag.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.vn.0",opt::value<uint32_t>(),
          "Oper db.vn.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.mpls.0",opt::value<uint32_t>(),
          "Oper db.mpls.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.acl.0",opt::value<uint32_t>(),
          "Oper db.acl.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.mirror_table.0",opt::value<uint32_t>(),
          "Oper db.mirror_table.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.vrf_assign.0",opt::value<uint32_t>(),
          "Oper db.vrf_assign.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.policy_set.0",opt::value<uint32_t>(),
          "Oper db.policy_set.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.vxlan.0",opt::value<uint32_t>(),
          "Oper db.vxlan.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.qos_queue.0",opt::value<uint32_t>(),
          "Oper db.qos_queue.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.forwardingclass.0",opt::value<uint32_t>(),
          "Oper db.forwardingclass.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.security_logging_object.0",opt::value<uint32_t>(),
          "Oper db.security_logging_object.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.qos_config.0",opt::value<uint32_t>(),
          "Oper db.qos_config.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.bridge_domain.0",opt::value<uint32_t>(),
          "Oper db.bridge_domain.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.multicast_policy.0",opt::value<uint32_t>(),
          "Oper db.multicast_policy.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.service-instance.0",opt::value<uint32_t>(),
          "Oper db.service-instance.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.physical_devices.0",opt::value<uint32_t>(),
          "Oper db.physical_devices.0 trace buffer size")
        ("TRACEBUFFSIZE.Oper db.physical_device_vn.0",opt::value<uint32_t>(),
          "Oper db.physical_device_vn.0 trace buffer size")
        ("TRACEBUFFSIZE.httpbuf",opt::value<uint32_t>(),
          "httpbuf trace buffer size")
        ("TRACEBUFFSIZE.OperRoute",opt::value<uint32_t>(),
          "OperRoute trace buffer size")
        ("TRACEBUFFSIZE.KSync Route",opt::value<uint32_t>(),
          "KSync Route trace buffer size");
    options_.add(debug);
    config_file_options_.add(debug);


    boost::program_options::options_description config("Configuration options");
    config.add_options()
        ("CONTROL-NODE.servers",
         opt::value<std::vector<std::string> >()->multitoken(),
         "List of IPAddress:Port of Control node Servers")
        ("CONTROL-NODE.subcluster_name", opt::value<string>(), "Cluster identifier")
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
         opt::value<uint32_t>()->default_value(default_stale_interface_cleanup_timeout),
         "Stale Interface cleanup timeout")
        ("DEFAULT.hostname", opt::value<string>(),
         "Hostname of compute-node")
        ("DEFAULT.dhcp_relay_mode", opt::bool_switch(&dhcp_relay_mode_),
         "Enable / Disable DHCP relay of DHCP packets from virtual instance")
        ("DEFAULT.agent_name", opt::value<string>(),
         "Agent Name")
        ("DEFAULT.http_server_port",
         opt::value<uint16_t>()->default_value(ContrailPorts::HttpPortAgent()),
         "Sandesh HTTP listener port")
        ("DEFAULT.tunnel_type", opt::value<string>()->default_value("MPLSoGRE"),
         "Tunnel Encapsulation type <MPLSoGRE|MPLSoUDP|VXLAN>")
        ("DEFAULT.agent_mode", opt::value<string>(),
         "Run agent in vrouter / tsn / tor mode")
        ("DEFAULT.gateway_mode", opt::value<string>(),
          "Set gateway mode to server/ vcpe")
        ("DEFAULT.agent_base_directory", opt::value<string>()->default_value("/var/lib/contrail"),
         "Base directory used by the agent")
        ("DNS.servers",
         opt::value<vector<string> >()->multitoken(),
         "List of IPAddress:Port of DNS node Servers")
        ("DEFAULT.xmpp_auth_enable", opt::bool_switch(&xmpp_auth_enable_),
         "Enable Xmpp over TLS")
        ("DEFAULT.tsn_servers",
         opt::value<std::vector<std::string> >()->multitoken(),
         "List of IPAddress of TSN Servers")
        ("DEFAULT.min_aap_prefix_len", opt::value<uint16_t>(),
         "Minimum prefix-len for Allowed-address-pair entries")
        ("DEFAULT.vmi_vm_vn_uve_interval",
         opt::value<uint16_t>()->default_value(Agent::kDefaultVmiVmVnUveInterval),
         "UVE send interval in seconds")
        ("DNS.dns_timeout", opt::value<uint32_t>()->default_value(3000),
         "DNS Timeout")
        ("DNS.dns_max_retries", opt::value<uint32_t>()->default_value(2),
         "Dns Max Retries")
        ("DNS.dns_client_port",
         opt::value<uint16_t>()->default_value(ContrailPorts::VrouterAgentDnsClientUdpPort()),
         "Dns client port")
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
          "/etc/contrail/ssl/certs/ca-cert.pem"),
          "XMPP CA ssl certificate")
        ("DEFAULT.xmpp_dns_auth_enable", opt::bool_switch(&xmpp_dns_auth_enable_),
         "Enable Xmpp over TLS for DNS")
        ("METADATA.metadata_proxy_secret", opt::value<string>(),
         "Shared secret for metadata proxy service")
        ("METADATA.metadata_proxy_port",
         opt::value<uint16_t>()->default_value(ContrailPorts::MetadataProxyVrouterAgentPort()),
        "Metadata proxy port ")
        ("METADATA.metadata_use_ssl", opt::bool_switch(&metadata_use_ssl_),
         "Enable SSL for Metadata proxy service")
        ("METADATA.metadata_client_cert", opt::value<string>()->default_value(""),
          "METADATA Client ssl certificate")
        ("METADATA.metadata_client_cert_type", opt::value<string>()->default_value("PEM"),
          "METADATA Client ssl certificate type")
        ("METADATA.metadata_client_key", opt::value<string>()->default_value(""),
          "METADATA Client ssl private key")
        ("METADATA.metadata_ca_cert", opt::value<string>()->default_value(""),
          "METADATA CA ssl certificate")
        ("NETWORKS.control_network_ip", opt::value<string>(),
         "control-channel IP address used by WEB-UI to connect to vnswad")
        ("DEFAULT.platform", opt::value<string>(),
         "Mode in which vrouter is running, option are dpdk or vnic")
        ("DEFAULT.agent_mock",opt::value<bool>()->default_value(false),
             "Agent Mocking Mode")
        ("AGENT-TEST-FRAMEWORK.mock_dpdk",
             opt::value<bool>()->default_value(false),
             "mock dpdk")
        ("AGENT-TEST-FRAMEWORK.ksocketdir",
              opt::value<string>()->default_value("/tmp/"),
             "ksocket directory")
        ("DEFAULT.subnet_hosts_resolvable",
         opt::bool_switch(&subnet_hosts_resolvable_)->default_value(true))
        ("DEFAULT.pkt0_tx_buffers", opt::value<uint32_t>()->default_value(default_pkt0_tx_buffers),
         "Number of tx-buffers for pkt0 interface")
        ("DEFAULT.physical_interface_address",
          opt::value<string>()->default_value(""))
        ("DEFAULT.physical_interface_mac",
          opt::value<string>()->default_value(""))
        ("HYPERVISOR.vmware_physical_interface",
          opt::value<string>()->default_value(""))
        ("DEFAULT.mirror_client_port",
         opt::value<uint16_t>()->default_value(ContrailPorts::VrouterAgentMirrorClientUdpPort()),
         "Mirror client Port")
        ("DEFAULT.simulate_evpn_tor", opt::bool_switch(&simulate_evpn_tor_),
         "Simulate Evpn Tor")
        ("DEFAULT.measure_queue_delay", opt::bool_switch(&measure_queue_delay_),
          "Measure flow queue delay")
        ("NEXTHOP-SERVER.endpoint", opt::value<string>(),
         "Nexthop Server Endpoint")
        ("NEXTHOP-SERVER.add_pid", opt::bool_switch(&nexthop_server_add_pid_),
         "Enable Nh Sever Pid")
        ("DEFAULT.mvpn_ipv4_enable", opt::bool_switch(&mvpn_ipv4_enable_),
          "Enable MVPN IPv4 in Agent")
        ("DEFAULT.vr_object_high_watermark", opt::value<float>()->default_value(80),
         "Max allowed vr object usage till alarm is raised - given as % (in integer) of object limit in vrouter")
        ;
    options_.add(generic).add(config);
    config_file_options_.add(config);

    opt::options_description restart("Restart options");
    restart.add_options()
        ("RESTART.backup_enable",
         opt::bool_switch(&restart_backup_enable_)->default_value(false),
         "Enable backup of config and resources into a file")
        ("RESTART.backup_idle_timeout", opt::value<uint64_t>()->default_value(CFG_BACKUP_IDLE_TIMEOUT),
         "Generate backup if no change detected in configured time (in msec)")
        ("RESTART.backup_dir", opt::value<string>()->default_value(CFG_BACKUP_DIR),
         "Directory storing backup files for configuraion or resource")
        ("RESTART.backup_count", opt::value<uint16_t>()->default_value(CFG_BACKUP_COUNT),
         "Number of backup files")
        ("RESTART.restore_enable", opt::bool_switch(&restart_restore_enable_)->default_value(true),
         "Enable restore of config and resources from backup files")
        ("RESTART.restore_audit_timeout", opt::value<uint64_t>()->default_value(CFG_RESTORE_AUDIT_TIMEOUT),
         "Audit time for config/resource read from file (in milli-sec)")
        ("RESTART.huge_page_1G",
         opt::value<std::vector<std::string> >()->multitoken(),
         "List of 1G Huge pages to be used by vrouter for flow and bridge entries")
        ("RESTART.huge_page_2M",
         opt::value<std::vector<std::string> >()->multitoken(),
         "List of 2M Huge pages to be used by vrouter");
    options_.add(restart);
    config_file_options_.add(restart);
    opt::options_description log("Logging options");
    std::vector<std::string> default_session_destination;
    std::vector<std::string> implicit_session_destination;
    default_session_destination.push_back("collector");
    log.add_options()
        ("DEFAULT.log_category", opt::value<string>()->default_value(""),
         "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_file",
         opt::value<string>()->default_value(Agent::GetInstance()->log_file()),
         "Filename for the logs to be written to")
        ("DEFAULT.log_files_count", opt::value<int>(),
         "Maximum log file roll over index")
        ("DEFAULT.log_file_size", opt::value<long>(),
         "Maximum size of the log file")
        ("DEFAULT.log_level", opt::value<string>()->default_value("SYS_NOTICE"),
         "Severity level for local logging of sandesh messages")
        ("DEFAULT.log_local", opt::bool_switch(&log_local_),
         "Enable local logging of sandesh messages")
        ("DEFAULT.use_syslog", opt::bool_switch(&use_syslog_),
         "Enable logging to syslog")
        ("DEFAULT.syslog_facility", opt::value<string>()->default_value("LOG_LOCAL0"),
         "Syslog facility to receive log lines")
        ("DEFAULT.log_flow", opt::bool_switch(&log_flow_),
         "Enable local logging of flow sandesh messages")
        ("DEFAULT.log_property_file", opt::value<string>()->default_value(""),
         "Log Property File")
        ("SESSION.slo_destination",
         opt::value<vector<string> >()
             ->default_value(default_session_destination, std::string("collector"))
             ->implicit_value(implicit_session_destination, std::string("")),
        "List of destinations. valid values are collector, file, syslog. Space delimited")
        ("SESSION.sample_destination",
         opt::value<std::vector<std::string> >()
             ->default_value(default_session_destination, std::string("collector"))
             ->implicit_value(implicit_session_destination, std::string("")),
        "List of destinations. valid values are collector, file, syslog. Space delimited");
    options_.add(log);
    config_file_options_.add(log);

    if (enable_flow_options_) {
        opt::options_description flow("Flow options");
        flow.add_options()
            ("FLOWS.thread_count", opt::value<uint16_t>()->default_value(Agent::kDefaultFlowThreadCount),
             "Number of threads for flow setup")
            ("FLOWS.max_vm_flows", opt::value<float>()->default_value(100),
             "Maximum flows allowed per VM - given as % (in integer) of ")
            ("FLOWS.max_system_linklocal_flows", opt::value<uint16_t>()->default_value(Agent::kDefaultMaxLinkLocalOpenFds),
             "Maximum number of link-local flows allowed across all VMs")
            ("FLOWS.max_vm_linklocal_flows", opt::value<uint16_t>()->default_value(Agent::kDefaultMaxLinkLocalOpenFds),
             "Maximum number of link-local flows allowed per VM")
            ("FLOWS.trace_enable", opt::bool_switch(&flow_trace_enable_)->default_value(true),
             "Enable flow tracing")
            ("FLOWS.add_tokens", opt::value<uint32_t>()->default_value(default_flow_add_tokens),
             "Number of add-tokens")
            ("FLOWS.ksync_tokens", opt::value<uint32_t>()->default_value(default_flow_ksync_tokens),
             "Number of ksync-tokens")
            ("FLOWS.del_tokens", opt::value<uint32_t>()->default_value(default_flow_del_tokens),
             "Number of delete-tokens")
            ("FLOWS.update_tokens", opt::value<uint32_t>()->default_value(default_flow_update_tokens),
             "Number of update-tokens")
            ("FLOWS.hash_exclude_router_id", opt::value<bool>(),
             "Exclude router-id in hash calculation")
            ("FLOWS.index_sm_log_count", opt::value<uint16_t>()->default_value(Agent::kDefaultFlowIndexSmLogCount),
             "Index Sm Log Count")
            ("FLOWS.latency_limit", opt::value<uint16_t>()->default_value(Agent::kDefaultFlowLatencyLimit),
             "Latency Limit")
            ("FLOWS.max_sessions_per_aggregate", opt::value<uint16_t>()->default_value(Agent::kMaxSessions),
             "Maximum number of sessions per Session Aggregate entry")
            ("FLOWS.max_aggregates_per_session_endpoint", opt::value<uint16_t>()->default_value(Agent::kMaxSessionAggs),
             "Maximum number of Session Aggregates per SessionEndpoint Entry")
            ("FLOWS.max_endpoints_per_session_msg", opt::value<uint16_t>()->default_value(Agent::kMaxSessionEndpoints),
             "Maximum number of SessionEnpoint entries per SessionEndpointObject")
            ("FLOWS.fabric_snat_hash_table_size", opt::value<uint16_t>()->default_value(default_fabric_snat_table_size),
             "Size of Port NAT hash table")
            ;
        options_.add(flow);
        config_file_options_.add(flow);
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
        config_file_options_.add(hypervisor);
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
            ("VIRTUAL-HOST-INTERFACE.eth_port_no_arp", opt::bool_switch(&eth_port_no_arp_),
             "Ethernet Port No-ARP")
            ("VIRTUAL-HOST-INTERFACE.eth_port_encap_type", opt::value<string>(),
             "Ethernet Port Encap Type")
            ;
        options_.add(vhost);
        config_file_options_.add(vhost);
    }

    if (enable_service_options_) {
        opt::options_description service("Service instance specific options");
        service.add_options()
            ("SERVICE-INSTANCE.netns_command", opt::value<string>(),
             "Script path used when a service instance is spawned with network namespace")
            ("SERVICE-INSTANCE.netns_timeout", opt::value<int>()->default_value(0),
             "Timeout used to set a netns command as failing and to destroy it")
            ("SERVICE-INSTANCE.netns_workers", opt::value<int>()->default_value(0),
             "Number of workers used to spawn netns command")
            ("SERVICE-INSTANCE.docker_command", opt::value<string>(),
             "Service instance docker command")
            ("SERVICE-INSTANCE.lb_ssl_cert_path", opt::value<string>(),
             "Loadbalancer ssl certificate path")
            ("SERVICES.bgp_as_a_service_port_range", opt::value<string>(),
             "Port range for BgPass ")
            ("SERVICES.queue_limit", opt::value<uint32_t>()->default_value(1024),
             "Work queue for different services")
            ("SERVICES.bgpaas_max_shared_sessions", opt::value<uint32_t>(),
             "BGPAAS max shared sessions for service port")
            ("SERVICE-INSTANCE.lbaas_auth_conf", opt::value<string>(),
             "Credentials fo ssl certificates and private-keys")
            ;
        options_.add(service);
        config_file_options_.add(service);
    }


    opt::options_description tbb("TBB specific options");
    tbb.add_options()
        ("TASK.thread_count", opt::value<uint32_t>()->default_value(default_tbb_thread_count),
         "Max number of threads used by TBB")
        ("TASK.log_exec_threshold", opt::value<uint32_t>()->default_value(0),
         "Log message if task takes more than threshold (msec) to execute")
        ("TASK.log_schedule_threshold", opt::value<uint32_t>()->default_value(0),
         "Log message if task takes more than threshold (msec) to schedule")
        ("TASK.tbb_keepawake_timeout", opt::value<uint32_t>()->default_value(default_tbb_keepawake_timeout),
         "Timeout for the TBB keepawake timer")
        ("TASK.task_monitor_timeout", opt::value<uint32_t>(),
         "Timeout for the Task monitoring")
        ("TASK.ksync_thread_cpu_pin_policy", opt::value<string>(),
         "Pin ksync io task to CPU")
        ("TASK.flow_netlink_pin_cpuid", opt::value<uint32_t>(),
         "CPU-ID to pin")
        ;
    options_.add(tbb);
    config_file_options_.add(tbb);

    opt::options_description sandesh("Sandesh specific options");
    sandesh::options::AddOptions(&sandesh, &sandesh_config_);
    options_.add(sandesh);
    config_file_options_.add(sandesh);

    opt::options_description llgr("LLGR");
    llgr.add_options()
        ("LLGR.disable", opt::value<bool>()->default_value(false),
         "Disable LLGR")
        ("LLGR.stale_config_cleanup_time", opt::value<uint16_t>()->default_value(100),
         "LLGR Stale Config Cleanup Time")
        ("LLGR.config_poll_time", opt::value<uint16_t>()->default_value(5),
         "LLGR Config Poll Time")
        ("LLGR.config_inactivity_time", opt::value<uint16_t>()->default_value(15),
         "LLGR Config Inactive Time")
        ("LLGR.config_fallback_time", opt::value<uint16_t>()->default_value(900),
         "LLGR Config Fallback Time")
        ("LLGR.end_of_rib_tx_poll_time", opt::value<uint16_t>()->default_value(5),
         "LLGR End Of Rib Poll Time")
        ("LLGR.end_of_rib_tx_fallback_time", opt::value<uint16_t>()->default_value(60),
         "LLGR End Of Rib Tx Fallback Time")
        ("LLGR.end_of_rib_tx_inactivity_time", opt::value<uint16_t>()->default_value(15),
         "LLGR End Of Rib Tx Inactivity Time")
        ("LLGR.end_of_rib_rx_fallback_time", opt::value<uint16_t>()->default_value(60),
         "LLGR End Of Rib Rx Fallback Time")
        ;
    options_.add(llgr);
    config_file_options_.add(llgr);

    opt::options_description mac_learn("MAC-LEARNING");
    mac_learn.add_options()
        ("MAC-LEARNING.thread_count", opt::value<uint32_t>()->default_value(default_mac_learning_thread_count),
         "Thread Count")
        ("MAC-LEARNING.add_tokens", opt::value<uint32_t>()->default_value(default_mac_learning_add_tokens),
         "Add Tokens")
        ("MAC-LEARNING.del_tokens", opt::value<uint32_t>()->default_value(default_mac_learning_update_tokens),
         "Del Tokens")
        ("MAC-LEARNING.update_tokens", opt::value<uint32_t>()->default_value(default_mac_learning_delete_tokens),
         "Update Tokens")
        ;
    options_.add(mac_learn);
    config_file_options_.add(mac_learn);

    opt::options_description qos("Quality of Service options");
    qos.add_options()
        ("QOS.priority_tagging",
         opt::bool_switch(&qos_priority_tagging_)->default_value(true),
         "Enable Priority tagging")
        ;
    options_.add(qos);
    config_file_options_.add(qos);

    opt::options_description crypt("CRYPT");
    crypt.add_options()
        ("CRYPT.crypt_interface", opt::value<string>(),
          "Interface name to send data for encrypt")
        ;
    options_.add(crypt);
    config_file_options_.add(crypt);

}

AgentParam::~AgentParam() {
}

LlgrParams::LlgrParams() {
    stale_config_cleanup_time_ = kStaleConfigCleanupTime;
    config_poll_time_ = kConfigPollTime;
    config_inactivity_time_ = kConfigInactivityTime;
    config_fallback_time_ = kConfigFallbackTimeOut;
    end_of_rib_tx_poll_time_ = kEorTxPollTime;
    end_of_rib_tx_fallback_time_ = kEorTxFallbackTimeOut;
    end_of_rib_tx_inactivity_time_ = kEorTxInactivityTime;
    end_of_rib_rx_fallback_time_ = kEorRxFallbackTime;
    llgr_stale_time_ = kLlgrStaleTime;
}
