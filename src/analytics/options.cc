/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "analytics/options.h"

#include <fstream>
#include <iostream>
#include <boost/asio/ip/host_name.hpp>

#include "analytics/buildinfo.h"
#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"
#include "net/address_util.h"
#include "viz_constants.h"

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

    vector<string> conf_files;
    conf_files.push_back("/etc/contrail/contrail-collector.conf");

    opt::options_description generic("Generic options");

    // Command line only options.
    generic.add_options()
        ("conf_file", opt::value<vector<string> >()->default_value(
                                               conf_files,
             "Configuration file"))
         ("help", "help message")
        ("version", "Display version information")
    ;

    uint16_t default_redis_port = ContrailPorts::RedisUvePort();
    uint16_t default_collector_port = ContrailPorts::CollectorPort();
    uint16_t default_collector_protobuf_port =
        ContrailPorts::CollectorProtobufPort();
    uint16_t default_partitions = 15;
    uint16_t default_http_server_port = ContrailPorts::HttpPortCollector();
    uint16_t default_discovery_port = ContrailPorts::DiscoveryServerPort();

    vector<string> default_cassandra_server_list;
#ifdef USE_CASSANDRA_CQL
    string default_cassandra_server("127.0.0.1:9042");
#else // USE_CASSANDRA_CQL
    string default_cassandra_server("127.0.0.1:9160");
#endif // !USE_CASSANDRA_CQL
    default_cassandra_server_list.push_back(default_cassandra_server);

    string default_zookeeper_server("127.0.0.1:2181");

    vector<string> default_kafka_broker_list;
    default_kafka_broker_list.push_back("");

    // Command line and config file options.
    opt::options_description cassandra_config("cassandra Configuration options");
    cassandra_config.add_options()
        ("CASSANDRA.cassandra_user",opt::value<string>()->default_value(""),
              "Cassandra user name")
        ("CASSANDRA.cassandra_password",opt::value<string>()->default_value(""),
              "Cassandra password");

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
             opt::value<uint64_t>()->default_value(g_viz_constants.TtlValuesDefault.find(TtlType::GLOBAL_TTL)->second),
             "global TTL(hours) for analytics data")
        ("DEFAULT.analytics_config_audit_ttl",
             opt::value<uint64_t>()->default_value(g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second),
             "global TTL(hours) for analytics config audit data")
        ("DEFAULT.analytics_statistics_ttl",
             opt::value<uint64_t>()->default_value(g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second),
             "global TTL(hours) for analytics stats data")
        ("DEFAULT.analytics_flow_ttl",
             opt::value<uint64_t>()->default_value(g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second),
             "global TTL(hours) for analytics flow data")
        ("DEFAULT.cassandra_server_list",
           opt::value<vector<string> >()->default_value(
               default_cassandra_server_list, default_cassandra_server),
             "Cassandra server list")
        ("DEFAULT.zookeeper_server_list",
            opt::value<string>()->default_value(""),
            "Zookeeper server list")
        ("DEFAULT.kafka_broker_list",
           opt::value<vector<string> >()->default_value(
               default_kafka_broker_list, ""),
             "Kafka Broker List")
        ("DEFAULT.partitions",
            opt::value<uint16_t>()->default_value(
                default_partitions),
         "Number of partitions to use for publishing to kafka")
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
        ("DEFAULT.log_property_file", opt::value<string>()->default_value(""),
             "log4cplus property file name")
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
        ("DEFAULT.kafka_prefix", opt::value<string>()->default_value(""),
             "System Prefix for Kafka")
        ("DEFAULT.syslog_port", opt::value<int>()->default_value(-1),
             "Syslog listener port (< 0 will disable the syslog)")
        ("DEFAULT.sflow_port", opt::value<int>()->default_value(6343),
             "sFlow listener UDP port (< 0 will disable sFlow Collector)")
        ("DEFAULT.ipfix_port", opt::value<int>()->default_value(4739),
             "ipfix listener UDP port (< 0 will disable ipfix Collector)")
        ("DEFAULT.test_mode", opt::bool_switch(&test_mode_),
             "Enable collector to run in test-mode")
        ("DEFAULT.sandesh_send_rate_limit",
              opt::value<uint32_t>()->default_value(
              Sandesh::get_send_rate_limit()),
              "Sandesh send rate limit in messages/sec")
        ("DEFAULT.disable_flow_collection",
            opt::bool_switch(&disable_flow_collection_),
            "Disable flow message collection")

        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(
                                                       default_discovery_port),
             "Port of Discovery Server")
        ("DISCOVERY.server", opt::value<string>()->default_value("127.0.0.1"),
             "IP address of Discovery Server")

        ("REDIS.port",
             opt::value<uint16_t>()->default_value(default_redis_port),
             "Port of Redis-uve server")
        ("REDIS.server", opt::value<string>()->default_value("127.0.0.1"),
             "IP address of Redis Server")
        ("REDIS.password", opt::value<string>()->default_value(""),
             "password for Redis Server")
        ;

    config_file_options_.add(config).add(cassandra_config);
    cmdline_options.add(generic).add(config).add(cassandra_config);
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
    GetOptValue< vector<string> >(var_map, config_file_,
                                  "conf_file");
    ifstream config_file_in;
    for(std::vector<int>::size_type i = 0; i != config_file_.size(); i++) {
        config_file_in.open(config_file_[i].c_str());
        if (config_file_in.good()) {
           opt::store(opt::parse_config_file(config_file_in, config_file_options_),
                   var_map);
        }
        config_file_in.close();
    }

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

    GetOptValue<uint64_t>(var_map, analytics_data_ttl_,
                     "DEFAULT.analytics_data_ttl");
    if (analytics_data_ttl_ == (uint64_t)-1) {
        analytics_data_ttl_ = g_viz_constants.TtlValuesDefault.find(TtlType::GLOBAL_TTL)->second;
    }   
    GetOptValue<uint64_t>(var_map, analytics_config_audit_ttl_,
                     "DEFAULT.analytics_config_audit_ttl");
    if (analytics_config_audit_ttl_ == (uint64_t)-1) {
        analytics_config_audit_ttl_ = g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second;
    }   
    GetOptValue<uint64_t>(var_map, analytics_statistics_ttl_,
                     "DEFAULT.analytics_statistics_ttl");
    if (analytics_statistics_ttl_ == (uint64_t)-1) {
        analytics_statistics_ttl_ = g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second;
    }   
    GetOptValue<uint64_t>(var_map, analytics_flow_ttl_,
                     "DEFAULT.analytics_flow_ttl");
    if (analytics_flow_ttl_ == (uint64_t)-1) {
        analytics_flow_ttl_ = g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second;
    }   

    GetOptValue< vector<string> >(var_map, cassandra_server_list_,
                                  "DEFAULT.cassandra_server_list");
    GetOptValue<string>(var_map, zookeeper_server_list_,
                        "DEFAULT.zookeeper_server_list");
    GetOptValue< vector<string> >(var_map, kafka_broker_list_,
                                  "DEFAULT.kafka_broker_list");
    GetOptValue<uint16_t>(var_map, partitions_, "DEFAULT.partitions");
    GetOptValue<string>(var_map, host_ip_, "DEFAULT.hostip");
    GetOptValue<string>(var_map, hostname_, "DEFAULT.hostname");
    GetOptValue<uint16_t>(var_map, http_server_port_,
                          "DEFAULT.http_server_port");

    GetOptValue<string>(var_map, log_category_, "DEFAULT.log_category");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file");
    GetOptValue<string>(var_map, log_property_file_, "DEFAULT.log_property_file");
    GetOptValue<int>(var_map, log_files_count_, "DEFAULT.log_files_count");
    GetOptValue<long>(var_map, log_file_size_, "DEFAULT.log_file_size");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");
    GetOptValue<bool>(var_map, use_syslog_, "DEFAULT.use_syslog");
    GetOptValue<string>(var_map, syslog_facility_, "DEFAULT.syslog_facility");
    GetOptValue<string>(var_map, kafka_prefix_, "DEFAULT.kafka_prefix");
    GetOptValue<int>(var_map, syslog_port_, "DEFAULT.syslog_port");
    GetOptValue<int>(var_map, sflow_port_, "DEFAULT.sflow_port");
    GetOptValue<int>(var_map, ipfix_port_, "DEFAULT.ipfix_port");
    GetOptValue<uint32_t>(var_map, sandesh_ratelimit_,
                              "DEFAULT.sandesh_send_rate_limit");

    GetOptValue<uint16_t>(var_map, discovery_port_, "DISCOVERY.port");
    GetOptValue<string>(var_map, discovery_server_, "DISCOVERY.server");

    GetOptValue<uint16_t>(var_map, redis_port_, "REDIS.port");
    GetOptValue<string>(var_map, redis_server_, "REDIS.server");
    GetOptValue<string>(var_map, redis_password_, "REDIS.password");
    GetOptValue<string>(var_map, cassandra_user_, "CASSANDRA.cassandra_user");
    GetOptValue<string>(var_map, cassandra_password_, "CASSANDRA.cassandra_password");
}
