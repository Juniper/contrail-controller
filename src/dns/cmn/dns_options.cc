/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio/ip/host_name.hpp>
#include <fstream>
#include <iostream>

#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"
#include "cmn/buildinfo.h"
#include "cmn/dns_options.h"

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;

// Process command line options for dns.
Options::Options() {
}

bool Options::Parse(EventManager &evm, int argc, char *argv[]) {
    opt::options_description cmdline_options("Allowed options");
    Initialize(evm, cmdline_options);

    Process(argc, argv, cmdline_options);
    return true;
}

// Initialize dns's command line option tags with appropriate default
// values. Options can from a config file as well. By default, we read
// options from /etc/contrail/dns.conf
void Options::Initialize(EventManager &evm,
                         opt::options_description &cmdline_options) {
    boost::system::error_code error;
    string hostname = host_name(error);
    string host_ip = GetHostIp(evm.io_service(), hostname);

    opt::options_description generic("Generic options");

    // Command line only options.
    generic.add_options()
        ("conf_file", opt::value<string>()->default_value(
                                                    "/etc/contrail/dns.conf"),
             "Configuration file")
         ("help", "help message")
        ("version", "Display version information")
    ;

    uint16_t default_dns_server_port = ContrailPorts::DnsServerPort;
    uint16_t default_http_server_port = ContrailPorts::HttpPortDns;
    uint16_t default_discovery_port = ContrailPorts::DiscoveryServerPort;

    default_collector_server_list_.push_back("127.0.0.1:8086");

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
        ("DEFAULT.collectors",
           opt::value<vector<string> >()->default_value(
               default_collector_server_list_, "127.0.0.1:8086"),
             "Collector server list")
        ("DEFAULT.dns_config_file",
             opt::value<string>()->default_value("dns_config.xml"),
             "DNS Configuration file")

        ("DEFAULT.hostip", opt::value<string>()->default_value(host_ip),
             "IP address of DNS Server")
        ("DEFAULT.hostname", opt::value<string>()->default_value(hostname),
             "Hostname of DNS Server")
        ("DEFAULT.http_server_port",
             opt::value<uint16_t>()->default_value(default_http_server_port),
             "Sandesh HTTP listener port")
        ("DEFAULT.dns_server_port",
             opt::value<uint16_t>()->default_value(default_dns_server_port),
             "DNS server port")

        ("DEFAULT.log_category",
             opt::value<string>()->default_value(log_category_),
             "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_disable", opt::bool_switch(&log_disable_),
             "Disable sandesh logging")
        ("DEFAULT.log_file", opt::value<string>()->default_value("<stdout>"),
             "Filename for the logs to be written to")
        ("DEFAULT.log_files_count",
             opt::value<int>()->default_value(10),
             "Maximum log file roll over index")
        ("DEFAULT.log_file_size",
             opt::value<long>()->default_value(1024*1024),
             "Maximum size of the log file")
        ("DEFAULT.log_level", opt::value<string>()->default_value("SYS_NOTICE"),
             "Severity level for local logging of sandesh messages")
        ("DEFAULT.log_local", opt::bool_switch(&log_local_),
             "Enable local logging of sandesh messages")
        ("DEFAULT.test_mode", opt::bool_switch(&test_mode_),
             "Enable dns to run in test-mode")

        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(
                                                       default_discovery_port),
             "Port of Discovery Server")
        ("DISCOVERY.server", opt::value<string>(),
             "IP address of Discovery Server")

        ("IFMAP.certs_store",  opt::value<string>(),
             "Certificates store to use for communication with IFMAP server")
        ("IFMAP.password", opt::value<string>()->default_value(
                                                     "dns_user_passwd"),
             "IFMAP server password")
        ("IFMAP.server_url",
             opt::value<string>()->default_value(ifmap_server_url_),
             "IFMAP server URL")
        ("IFMAP.user", opt::value<string>()->default_value("dns_user"),
             "IFMAP server username")
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
void Options::Process(int argc, char *argv[],
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
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        cout << MiscUtils::GetBuildInfo(MiscUtils::Dns, BuildInfo,
                                        build_info) << endl;
        exit(0);
    }

    // Retrieve the options.
    GetOptValue<string>(var_map, dns_config_file_, "DEFAULT.dns_config_file");
    GetOptValue< vector<string> >(var_map, collector_server_list_,
                                  "DEFAULT.collectors");
    collectors_configured_ = true;
    if (collector_server_list_.size() == 1 &&
        !collector_server_list_[0].compare(default_collector_server_list_[0])) {
        collectors_configured_ = false;
    }

    GetOptValue<string>(var_map, host_ip_, "DEFAULT.hostip");
    GetOptValue<string>(var_map, hostname_, "DEFAULT.hostname");

    GetOptValue<uint16_t>(var_map, http_server_port_,
                          "DEFAULT.http_server_port");

    GetOptValue<uint16_t>(var_map, dns_server_port_, "DEFAULT.dns_server_port");

    GetOptValue<string>(var_map, log_category_, "DEFAULT.log_category");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file");
    GetOptValue<int>(var_map, log_files_count_, "DEFAULT.log_files_count");
    GetOptValue<long>(var_map, log_file_size_, "DEFAULT.log_file_size");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");

    GetOptValue<uint16_t>(var_map, discovery_port_, "DISCOVERY.port");
    GetOptValue<string>(var_map, discovery_server_, "DISCOVERY.server");


    GetOptValue<string>(var_map, ifmap_password_, "IFMAP.password");
    GetOptValue<string>(var_map, ifmap_server_url_, "IFMAP.server_url");
    GetOptValue<string>(var_map, ifmap_user_, "IFMAP.user");
    GetOptValue<string>(var_map, ifmap_certs_store_, "IFMAP.certs_store");
}
