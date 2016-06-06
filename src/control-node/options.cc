/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "options.h"

#include <fstream>
#include <iostream>
#include <boost/asio/ip/host_name.hpp>

#include "control-node/buildinfo.h"
#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"
#include "net/address_util.h"

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;

// Process command line options for control-node.
Options::Options() {
}

bool Options::Parse(EventManager &evm, int argc, char *argv[]) {
    opt::options_description cmdline_options("Allowed options");
    Initialize(evm, cmdline_options);

    try {
        return Process(argc, argv, cmdline_options);
    } catch (boost::program_options::error &e) {
        cout << "Error " << e.what() << endl;
    } catch (...) {
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    return false;
}

// Initialize control-node's command line option tags with appropriate default
// values. Options can from a config file as well. By default, we read
// options from /etc/contrail/contrail-control.conf
void Options::Initialize(EventManager &evm,
                         opt::options_description &cmdline_options) {
    boost::system::error_code error;
    string hostname = host_name(error);
    string host_ip = GetHostIp(evm.io_service(), hostname);

    if (host_ip.empty())
        host_ip = "127.0.0.1";

    opt::options_description generic("Generic options");

    // Command line only options.
    generic.add_options()
        ("conf_file", opt::value<string>()->default_value(
                                            "/etc/contrail/contrail-control.conf"),
             "Configuration file")
         ("help", "help message")
        ("version", "Display version information")
    ;

    uint16_t default_bgp_port = ContrailPorts::ControlBgp();
    uint16_t default_http_server_port = ContrailPorts::HttpPortControl();
    uint16_t default_xmpp_port = ContrailPorts::ControlXmpp();
    uint16_t default_discovery_port = ContrailPorts::DiscoveryServerPort();

    default_collector_server_list_.push_back("127.0.0.1:8086");

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
        ("DEFAULT.bgp_config_file",
             opt::value<string>()->default_value("bgp_config.xml"),
             "BGP Configuration file")
        ("DEFAULT.bgp_port",
             opt::value<uint16_t>()->default_value(default_bgp_port),
             "BGP listener port")
        ("DEFAULT.collectors",
           opt::value<vector<string> >()->default_value(
               default_collector_server_list_, "127.0.0.1:8086"),
             "Collector server list")

        ("DEFAULT.gr_helper_bgp_disable",
            opt::bool_switch(&gr_helper_bgp_disable_),
            "Disable Graceful Restart Helper functionality for BGP peers")
        ("DEFAULT.gr_helper_xmpp_disable",
            opt::bool_switch(&gr_helper_xmpp_disable_),
            "Disable Graceful Restart Helper functionality for XMPP agents")

        ("DEFAULT.hostip", opt::value<string>()->default_value(host_ip),
             "IP address of control-node")
        ("DEFAULT.hostname", opt::value<string>()->default_value(hostname),
             "Hostname of control-node")
        ("DEFAULT.http_server_port",
             opt::value<uint16_t>()->default_value(default_http_server_port),
             "Sandesh HTTP listener port")

        ("DEFAULT.log_category",
             opt::value<string>()->default_value(log_category_),
             "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_disable", opt::bool_switch(&log_disable_),
             "Disable sandesh logging")
        ("DEFAULT.log_file", opt::value<string>()->default_value("<stdout>"),
             "Filename for the logs to be written to")
        ("DEFAULT.log_property_file", opt::value<string>()->default_value(""),
             "log4cplus property file name")
        ("DEFAULT.log_files_count",
             opt::value<int>()->default_value(10),
             "Maximum log file roll over index")
        ("DEFAULT.log_file_size",
             opt::value<long>()->default_value(10*1024*1024),
             "Maximum size of the log file")
        ("DEFAULT.log_level", opt::value<string>()->default_value("SYS_NOTICE"),
             "Severity level for local logging of sandesh messages")
        ("DEFAULT.log_local", opt::bool_switch(&log_local_),
             "Enable local logging of sandesh messages")
        ("DEFAULT.use_syslog", opt::bool_switch(&use_syslog_),
             "Enable logging to syslog")
        ("DEFAULT.syslog_facility", opt::value<string>()->default_value("LOG_LOCAL0"),
             "Syslog facility to receive log lines")
        ("DEFAULT.task_track_run_time", opt::bool_switch(&task_track_run_time_),
             "Enable tracking of run time per task id")
        ("DEFAULT.test_mode", opt::bool_switch(&test_mode_),
             "Enable control-node to run in test-mode")
        ("DEFAULT.tcp_hold_time", opt::value<int>()->default_value(30),
             "Configurable TCP hold time")
        ("DEFAULT.optimize_snat", opt::bool_switch(&optimize_snat_),
             "Enable control-node optimizations for SNAT (deprecated)")

        ("DEFAULT.xmpp_server_port",
             opt::value<uint16_t>()->default_value(default_xmpp_port),
             "XMPP listener port")
        ("DEFAULT.xmpp_auth_enable", opt::bool_switch(&xmpp_auth_enable_),
             "Enable authentication over Xmpp")
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
        ("DEFAULT.sandesh_send_rate_limit",
              opt::value<uint32_t>()->default_value(
              Sandesh::get_send_rate_limit()),
              "Sandesh send rate limit in messages/sec")

        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(
                                                       default_discovery_port),
             "Port of Discovery Server")
        ("DISCOVERY.server", opt::value<string>()->default_value("127.0.0.1"),
             "IP address of Discovery Server")

        ("IFMAP.certs_store",  opt::value<string>(),
             "Certificates store to use for communication with IFMAP server")
        ("IFMAP.password", opt::value<string>()->default_value("control-node"),
             "IFMAP server password")
        ("IFMAP.server_url", opt::value<string>()->default_value(
             ifmap_config_options_.server_url), "IFMAP server URL")
        ("IFMAP.user", opt::value<string>()->default_value("control-node"),
             "IFMAP server username")
        ("IFMAP.stale_entries_cleanup_timeout",
             opt::value<int>()->default_value(10),
             "IFMAP stale entries cleanup timeout")
        ("IFMAP.end_of_rib_timeout", opt::value<int>()->default_value(10),
             "IFMAP end of rib timeout")
        ("IFMAP.peer_response_wait_time", opt::value<int>()->default_value(60),
             "IFMAP peer response wait time")
        ;

    config_file_options_.add(config);
    cmdline_options.add(generic).add(config);
}

template <typename ValueType>
void Options::GetOptValue(const boost::program_options::variables_map &var_map,
                          ValueType &var, std::string val) {
    GetOptValueImpl(var_map, var, val, static_cast<ValueType *>(0));
}

template <typename ValueType>
void Options::GetOptValueImpl(
    const boost::program_options::variables_map &var_map,
    ValueType &var, std::string val, ValueType*) {
    // Check if the value is present.
    if (var_map.count(val)) {
        var = var_map[val].as<ValueType>();
    }
}

template <typename ElementType>
void Options::GetOptValueImpl(
    const boost::program_options::variables_map &var_map,
    std::vector<ElementType> &var, std::string val, std::vector<ElementType>*) {
    // Check if the value is present.
    if (var_map.count(val)) {
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
    }
}

// Process command line options. They can come from a conf file as well. Options
// from command line always overrides those that come from the config file.
bool Options::Process(int argc, char *argv[],
        opt::options_description &cmdline_options) {
    // Process options off command line first.
    opt::variables_map var_map;
    opt::store(opt::parse_command_line(argc, argv, cmdline_options), var_map);

    // Process options off configuration file.
    GetOptValue<string>(var_map, config_file_, "conf_file");
    ifstream config_file_in;
    config_file_in.open(config_file_.c_str());
    if (config_file_in.good()) {
        opt::store(opt::parse_config_file(config_file_in, config_file_options_),
                   var_map);
    }
    config_file_in.close();

    opt::notify(var_map);

    if (var_map.count("help")) {
        cout << cmdline_options << endl;
        return false;
    }

    if (var_map.count("version")) {
        string build_info;
        cout << MiscUtils::GetBuildInfo(MiscUtils::ControlNode, BuildInfo,
                                        build_info) << endl;
        return false;
    }

    // Retrieve the options.
    GetOptValue<string>(var_map, bgp_config_file_, "DEFAULT.bgp_config_file");
    GetOptValue<uint16_t>(var_map, bgp_port_, "DEFAULT.bgp_port");
    GetOptValue< vector<string> >(var_map, collector_server_list_,
                                  "DEFAULT.collectors");
    string error_msg;
    if (!ValidateServerEndpoints(collector_server_list_, &error_msg)) {
        cout << "Invalid endpoint : " << error_msg;
        return false;
    }

    collectors_configured_ = true;
    if (collector_server_list_.size() == 1 &&
        !collector_server_list_[0].compare(default_collector_server_list_[0])) {
        collectors_configured_ = false;
    }

    GetOptValue<string>(var_map, host_ip_, "DEFAULT.hostip");
    if (!ValidateIPAddressString(host_ip_, &error_msg)) {
        cout << "Invalid IP address: " << host_ip_ << error_msg;
        return false;
    }

    GetOptValue<string>(var_map, hostname_, "DEFAULT.hostname");

    GetOptValue<uint16_t>(var_map, http_server_port_,
                          "DEFAULT.http_server_port");

    GetOptValue<string>(var_map, log_category_, "DEFAULT.log_category");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file");
    GetOptValue<string>(var_map, log_property_file_, "DEFAULT.log_property_file");
    GetOptValue<int>(var_map, log_files_count_, "DEFAULT.log_files_count");
    GetOptValue<long>(var_map, log_file_size_, "DEFAULT.log_file_size");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");
    GetOptValue<string>(var_map, syslog_facility_, "DEFAULT.syslog_facility");
    GetOptValue<int>(var_map, tcp_hold_time_, "DEFAULT.tcp_hold_time");
    GetOptValue<uint16_t>(var_map, xmpp_port_, "DEFAULT.xmpp_server_port");
    GetOptValue<string>(var_map, xmpp_server_cert_, "DEFAULT.xmpp_server_cert");
    GetOptValue<string>(var_map, xmpp_server_key_, "DEFAULT.xmpp_server_key");
    GetOptValue<string>(var_map, xmpp_ca_cert_, "DEFAULT.xmpp_ca_cert");
    GetOptValue<uint32_t>(var_map, sandesh_ratelimit_,
                              "DEFAULT.sandesh_send_rate_limit");

    GetOptValue<uint16_t>(var_map, discovery_port_, "DISCOVERY.port");
    GetOptValue<string>(var_map, discovery_server_, "DISCOVERY.server");


    GetOptValue<string>(var_map, ifmap_config_options_.password,
                        "IFMAP.password");
    GetOptValue<string>(var_map, ifmap_config_options_.server_url,
                        "IFMAP.server_url");
    GetOptValue<string>(var_map, ifmap_config_options_.user,
                        "IFMAP.user");
    GetOptValue<string>(var_map, ifmap_config_options_.certs_store,
                        "IFMAP.certs_store");
    GetOptValue<int>(var_map,
                     ifmap_config_options_.stale_entries_cleanup_timeout,
                     "IFMAP.stale_entries_cleanup_timeout");
    GetOptValue<int>(var_map, ifmap_config_options_.end_of_rib_timeout,
                     "IFMAP.end_of_rib_timeout");
    GetOptValue<int>(var_map,
                     ifmap_config_options_.peer_response_wait_time,
                     "IFMAP.peer_response_wait_time");

    return true;
}
