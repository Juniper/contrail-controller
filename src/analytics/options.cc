/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>
#include <boost/asio/ip/host_name.hpp>

#include "analytics/buildinfo.h"
#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"

#include "options.h"

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;

// Process command line options for collector   .
Options::Options() {
}

bool Options::Parse(EventManager &evm, int argc, char *argv[]) {
    opt::options_description cmdline_options("Allowed options");
    Initialize(evm, cmdline_options);

    Process(argc, argv, cmdline_options);
    return true;
}

// Initialize collector's command line option tags with appropriate default
// values. Options can from a config file as well. By default, we read
// options from /etc/contrail/contrail-collector.conf
void Options::Initialize(EventManager &evm,
                         opt::options_description &cmdline_options) {
    boost::system::error_code error;
    string hostname = host_name(error);
    string host_ip = GetHostIp(evm.io_service(), hostname);

    if (host_ip.empty()) {
        cout << "Error! Cannot resolve host " << hostname <<
                "to a valid IP address";
        exit(-1);
    }

    opt::options_description generic("Generic options");

    // Command line only options.
    generic.add_options()
        ("conf_file", opt::value<string>()->default_value(
                                            "/etc/contrail/contrail-collector.conf"),
             "Configuration file")
         ("help", "help message")
        ("version", "Display version information")
    ;

    uint16_t default_redis_port = ContrailPorts::RedisUvePort();
    uint16_t default_collector_port = ContrailPorts::CollectorPort();
    uint16_t default_collector_protobuf_port =
        ContrailPorts::CollectorProtobufPort();
    uint16_t default_http_server_port = ContrailPorts::HttpPortCollector();
    uint16_t default_discovery_port = ContrailPorts::DiscoveryServerPort();

    vector<string> default_cassandra_server_list;
    default_cassandra_server_list.push_back("127.0.0.1:9160");

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
        ("COLLECTOR.port", opt::value<uint16_t>()->default_value(
                                                default_collector_port),
             "Listener port of sandesh collector server")
        ("COLLECTOR.server",
             opt::value<string>()->default_value("0.0.0.0"),
             "IP address of sandesh collector server")
        ("COLLECTOR.protobuf_port",
            opt::value<uint16_t>()->default_value(
                default_collector_protobuf_port),
         "Listener port of Google Protocol Buffer collector server")

        ("DEFAULT.analytics_data_ttl",
             opt::value<int>()->default_value(ANALYTICS_DATA_TTL_DEFAULT),
             "global TTL(hours) for analytics data")
        ("DEFAULT.cassandra_server_list",
           opt::value<vector<string> >()->default_value(
               default_cassandra_server_list, "127.0.0.1:9160"),
             "Cassandra server list")
        ("DEFAULT.dup", opt::bool_switch(&dup_), "Internal use flag")
        ("DEFAULT.hostip", opt::value<string>()->default_value(host_ip),
             "IP address of collector")
        ("DEFAULT.hostname", opt::value<string>()->default_value(hostname),
             "Hostname of collector")
        ("DEFAULT.http_server_port",
             opt::value<uint16_t>()->default_value(default_http_server_port),
             "Sandesh HTTP listener port")

        ("DEFAULT.log_category", opt::value<string>(),
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
        ("DEFAULT.use_syslog", opt::bool_switch(&use_syslog_),
             "Enable logging to syslog")
        ("DEFAULT.syslog_facility", opt::value<string>()->default_value("LOG_LOCAL0"),
             "Syslog facility to receive log lines")
        ("DEFAULT.syslog_port", opt::value<int>()->default_value(-1),
             "Syslog listener port (< 0 will disable the syslog)")
        ("DEFAULT.sflow_port", opt::value<int>()->default_value(6343),
             "sFlow listener UDP port (< 0 will disable sFlow Collector)")
        ("DEFAULT.ipfix_port", opt::value<int>()->default_value(4739),
             "ipfix listener UDP port (< 0 will disable ipfix Collector)")
        ("DEFAULT.test_mode", opt::bool_switch(&test_mode_),
             "Enable collector to run in test-mode")

        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(
                                                       default_discovery_port),
             "Port of Discovery Server")
        ("DISCOVERY.server", opt::value<string>(),
             "IP address of Discovery Server")

        ("REDIS.port",
             opt::value<uint16_t>()->default_value(default_redis_port),
             "Port of Redis-uve server")
        ("REDIS.server", opt::value<string>()->default_value("127.0.0.1"),
             "IP address of Redis Server")
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

template <typename ValueType>
bool Options::GetOptValueIfNotDefaulted(
    const boost::program_options::variables_map &var_map,
    ValueType &var, std::string val) {
    return GetOptValueIfNotDefaultedImpl(var_map, var, val,
        static_cast<ValueType *>(0));
}

template <typename ValueType>
bool Options::GetOptValueIfNotDefaultedImpl(
    const boost::program_options::variables_map &var_map,
    ValueType &var, std::string val, ValueType*) {
    // Check if the value is present.
    if (var_map.count(val) && !var_map[val].defaulted()) {
        var = var_map[val].as<ValueType>();
        return true;
    }
    return false;
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
        cout << MiscUtils::GetBuildInfo(MiscUtils::Analytics, BuildInfo,
                                        build_info) << endl;
        exit(0);
    }

    // Retrieve the options.
    GetOptValue<uint16_t>(var_map, collector_port_, "COLLECTOR.port");
    GetOptValue<string>(var_map, collector_server_, "COLLECTOR.server");
    if (GetOptValueIfNotDefaulted<uint16_t>(var_map, collector_protobuf_port_,
            "COLLECTOR.protobuf_port")) {
        collector_protobuf_port_configured_ = true;
    } else {
        collector_protobuf_port_configured_ = false;
    }
    GetOptValue<int>(var_map, analytics_data_ttl_,
                     "DEFAULT.analytics_data_ttl");

    GetOptValue< vector<string> >(var_map, cassandra_server_list_,
                                  "DEFAULT.cassandra_server_list");
    GetOptValue<string>(var_map, host_ip_, "DEFAULT.hostip");
    GetOptValue<string>(var_map, hostname_, "DEFAULT.hostname");
    GetOptValue<uint16_t>(var_map, http_server_port_,
                          "DEFAULT.http_server_port");

    GetOptValue<string>(var_map, log_category_, "DEFAULT.log_category");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file");
    GetOptValue<int>(var_map, log_files_count_, "DEFAULT.log_files_count");
    GetOptValue<long>(var_map, log_file_size_, "DEFAULT.log_file_size");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");
    GetOptValue<bool>(var_map, use_syslog_, "DEFAULT.use_syslog");
    GetOptValue<string>(var_map, syslog_facility_, "DEFAULT.syslog_facility");
    GetOptValue<int>(var_map, syslog_port_, "DEFAULT.syslog_port");
    GetOptValue<int>(var_map, sflow_port_, "DEFAULT.sflow_port");
    GetOptValue<int>(var_map, ipfix_port_, "DEFAULT.ipfix_port");

    GetOptValue<uint16_t>(var_map, discovery_port_, "DISCOVERY.port");
    GetOptValue<string>(var_map, discovery_server_, "DISCOVERY.server");

    GetOptValue<uint16_t>(var_map, redis_port_, "REDIS.port");
    GetOptValue<string>(var_map, redis_server_, "REDIS.server");
}
