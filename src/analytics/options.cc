/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "analytics/options.h"

#include <fstream>
#include <iostream>
#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/functional/hash.hpp>

#include "analytics/buildinfo.h"
#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"
#include <base/options_util.h>
#include "net/address_util.h"
#include "viz_constants.h"
#include <database/gendb_constants.h>

#include "config/config-client-mgr/config_client_manager.h"

using namespace std;
using namespace boost::asio::ip;
using namespace boost::assign;
namespace opt = boost::program_options;
using namespace options::util;

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

    map<string, vector<string> >::const_iterator it =
        g_vns_constants.ServicesDefaultConfigurationFiles.find(
            g_vns_constants.SERVICE_COLLECTOR);
    assert(it != g_vns_constants.ServicesDefaultConfigurationFiles.end());
    const vector<string> &conf_files(it->second);

    opt::options_description generic("Generic options");

    // Command line only options.
    ostringstream conf_files_oss;
    bool first = true;
    BOOST_FOREACH(const string &cfile, conf_files) {
        if (first) {
            conf_files_oss << cfile;
            first = false;
        } else {
            conf_files_oss << ", " << cfile;
        }
    }
    generic.add_options()
        ("conf_file", opt::value<vector<string> >()->default_value(
             conf_files, conf_files_oss.str()),
             "Configuration file")
         ("help", "help message")
        ("version", "Display version information")
    ;

    uint16_t default_redis_port = ContrailPorts::RedisUvePort();
    uint16_t default_ks_port = ContrailPorts::KeystonePort();
    uint16_t default_collector_port = ContrailPorts::CollectorPort();
    uint16_t default_collector_protobuf_port =
        ContrailPorts::CollectorProtobufPort();
    uint16_t default_collector_structured_syslog_port =
        ContrailPorts::CollectorStructuredSyslogPort();
    uint16_t default_partitions = 15;
    uint16_t default_http_server_port = ContrailPorts::HttpPortCollector();
    uint32_t default_disk_usage_percentage_high_watermark0 = 90;
    uint32_t default_disk_usage_percentage_low_watermark0 = 85;
    uint32_t default_disk_usage_percentage_high_watermark1 = 80;
    uint32_t default_disk_usage_percentage_low_watermark1 = 75;
    uint32_t default_disk_usage_percentage_high_watermark2 = 70;
    uint32_t default_disk_usage_percentage_low_watermark2 = 60;
    uint32_t default_pending_compaction_tasks_high_watermark0 = 400;
    uint32_t default_pending_compaction_tasks_low_watermark0 = 300;
    uint32_t default_pending_compaction_tasks_high_watermark1 = 200;
    uint32_t default_pending_compaction_tasks_low_watermark1 = 150;
    uint32_t default_pending_compaction_tasks_high_watermark2 = 100;
    uint32_t default_pending_compaction_tasks_low_watermark2 = 80;
    std::string default_high_watermark0_message_severity_level = "SYS_EMERG";
    std::string default_low_watermark0_message_severity_level = "SYS_ALERT";
    std::string default_high_watermark1_message_severity_level = "SYS_ERR";
    std::string default_low_watermark1_message_severity_level = "SYS_WARN";
    std::string default_high_watermark2_message_severity_level = "SYS_DEBUG";
    std::string default_low_watermark2_message_severity_level = "INVALID";

    vector<string> default_cassandra_server_list;
    string default_cassandra_server("127.0.0.1:9042");
    default_cassandra_server_list.push_back(default_cassandra_server);

    string default_zookeeper_server("127.0.0.1:2181");

    // e.g. "ModuleClientState-max_sm_queue_count:AggProxySumAnomalyEWM01:AggProxySum"
    vector<string> default_uve_proxy_list = list_of
("UveVirtualNetworkAgent-egress_flow_count:AggProxySumAnomalyEWM01:AggProxySum")
("UveVirtualNetworkAgent-ingress_flow_count:AggProxySumAnomalyEWM01:AggProxySum");

    string default_uve_proxy;
    for (vector<string>::const_iterator it = default_uve_proxy_list.begin();
            it != default_uve_proxy_list.end(); it++) {
        default_uve_proxy = default_uve_proxy + (*it) + std::string(" ");
    }

    vector<string> default_kafka_broker_list;
    default_kafka_broker_list.push_back("");

    string default_api_server("127.0.0.1:8082");
    vector<string> default_api_server_list = list_of(default_api_server);

    vector<string> default_grok_key_list;
    default_grok_key_list.push_back("");

    vector<string> default_grok_attrib_list;
    default_grok_attrib_list.push_back("");

    vector<string> default_structured_syslog_tcp_forward_destination;
    default_structured_syslog_tcp_forward_destination.push_back("");

    vector<string> default_structured_syslog_kafka_broker_list;
    default_structured_syslog_kafka_broker_list.push_back("");

    string default_structured_syslog_kafka_topic("structured_syslog");
    uint16_t default_structured_syslog_kafka_partitions = 30;

    // Command line and config file options.
    opt::options_description cassandra_config("Cassandra Configuration options");
    cassandra_config.add_options()
        ("CASSANDRA.cassandra_user", opt::value<string>()->default_value(""),
              "Cassandra user name")
        ("CASSANDRA.cassandra_password", opt::value<string>()->default_value(""),
              "Cassandra password")
        ("CASSANDRA.compaction_strategy",
            opt::value<string>()->default_value(
                GenDb::g_gendb_constants.SIZE_TIERED_COMPACTION_STRATEGY),
            "Cassandra compaction strategy")
        ("CASSANDRA.flow_tables.compaction_strategy",
            opt::value<string>()->default_value(
                GenDb::g_gendb_constants.DATE_TIERED_COMPACTION_STRATEGY),
            "Cassandra compaction strategy for flow tables");

    // Command line and config file options.
    opt::options_description database_config("Database Configuration options");
    database_config.add_options()
        ("DATABASE.disk_usage_percentage.high_watermark0",
            opt::value<uint32_t>()->default_value(
                default_disk_usage_percentage_high_watermark0),
            "Disk usage percentage high watermark 0")
        ("DATABASE.disk_usage_percentage.low_watermark0",
            opt::value<uint32_t>()->default_value(
                default_disk_usage_percentage_low_watermark0),
            "Disk usage percentage low watermark 0")
        ("DATABASE.disk_usage_percentage.high_watermark1",
            opt::value<uint32_t>()->default_value(
                default_disk_usage_percentage_high_watermark1),
            "Disk usage percentage high watermark 1")
        ("DATABASE.disk_usage_percentage.low_watermark1",
            opt::value<uint32_t>()->default_value(
                default_disk_usage_percentage_low_watermark1),
            "Disk usage percentage low watermark 1")
        ("DATABASE.disk_usage_percentage.high_watermark2",
            opt::value<uint32_t>()->default_value(
                default_disk_usage_percentage_high_watermark2),
            "Disk usage percentage high watermark 2")
        ("DATABASE.disk_usage_percentage.low_watermark2",
            opt::value<uint32_t>()->default_value(
                default_disk_usage_percentage_low_watermark2),
            "Disk usage percentage low watermark 2")
        ("DATABASE.pending_compaction_tasks.high_watermark0",
            opt::value<uint32_t>()->default_value(
                default_pending_compaction_tasks_high_watermark0),
            "Cassandra pending compaction tasks high watermark 0")
        ("DATABASE.pending_compaction_tasks.low_watermark0",
            opt::value<uint32_t>()->default_value(
                default_pending_compaction_tasks_low_watermark0),
            "Cassandra pending compaction tasks low watermark 0")
        ("DATABASE.pending_compaction_tasks.high_watermark1",
            opt::value<uint32_t>()->default_value(
                default_pending_compaction_tasks_high_watermark1),
            "Cassandra pending compaction tasks high watermark 1")
        ("DATABASE.pending_compaction_tasks.low_watermark1",
            opt::value<uint32_t>()->default_value(
                default_pending_compaction_tasks_low_watermark1),
            "Cassandra pending compaction tasks low watermark 1")
        ("DATABASE.pending_compaction_tasks.high_watermark2",
            opt::value<uint32_t>()->default_value(
                default_pending_compaction_tasks_high_watermark2),
            "Cassandra pending compaction tasks high watermark 2")
        ("DATABASE.pending_compaction_tasks.low_watermark2",
            opt::value<uint32_t>()->default_value(
                default_pending_compaction_tasks_low_watermark2),
            "Cassandra pending compaction tasks low watermark 2")
        ("DATABASE.high_watermark0.message_severity_level",
            opt::value<string>()->default_value(
                default_high_watermark0_message_severity_level),
            "High Watermark 0 Message severity level")
        ("DATABASE.low_watermark0.message_severity_level",
            opt::value<string>()->default_value(
                default_low_watermark0_message_severity_level),
            "Low Watermark 0 Message severity level")
        ("DATABASE.high_watermark1.message_severity_level",
            opt::value<string>()->default_value(
                default_high_watermark1_message_severity_level),
            "High Watermark 1 Message severity level")
        ("DATABASE.low_watermark1.message_severity_level",
            opt::value<string>()->default_value(
                default_low_watermark1_message_severity_level),
            "Low Watermark 1 Message severity level")
        ("DATABASE.high_watermark2.message_severity_level",
            opt::value<string>()->default_value(
                default_high_watermark2_message_severity_level),
            "High Watermark 2 Message severity level")
        ("DATABASE.low_watermark2.message_severity_level",
            opt::value<string>()->default_value(
                default_low_watermark2_message_severity_level),
            "Low Watermark 2 Message severity level")

        ("DATABASE.cluster_id", opt::value<string>()->default_value(""),
             "Analytics Cluster Id")
        ("DATABASE.disable_all_writes",
            opt::bool_switch(&cassandra_options_.disable_all_db_writes_),
            "Disable all writes to the database")
        ("DATABASE.disable_statistics_writes",
            opt::bool_switch(&cassandra_options_.disable_db_stats_writes_),
            "Disable statistics writes to the database")
        ("DATABASE.disable_message_writes",
            opt::bool_switch(&cassandra_options_.disable_db_messages_writes_),
            "Disable message writes to the database")
        ;

    // Command line and config file options.
    opt::options_description collector_config("Collector Configuration options");
    collector_config.add_options()
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
        ("STRUCTURED_SYSLOG_COLLECTOR.port",
            opt::value<uint16_t>()->default_value(
                default_collector_structured_syslog_port),
         "Listener port of Structured Syslog collector server")
        ("STRUCTURED_SYSLOG_COLLECTOR.tcp_forward_destination",
           opt::value<vector<string> >()->default_value(
               default_structured_syslog_tcp_forward_destination, ""),
             "Structured Syslog Forward Destination List")
        ("STRUCTURED_SYSLOG_COLLECTOR.kafka_broker_list",
           opt::value<vector<string> >()->default_value(
               default_structured_syslog_kafka_broker_list, ""),
             "Structured Syslog Kafka Broker List")
        ("STRUCTURED_SYSLOG_COLLECTOR.kafka_topic",
           opt::value<string>()->default_value(
               default_structured_syslog_kafka_topic, "structured_syslog"),
             "Structured Syslog Kafka Topic")
        ("STRUCTURED_SYSLOG_COLLECTOR.kafka_partitions",
           opt::value<uint16_t>()->default_value(
               default_structured_syslog_kafka_partitions),
             "Structured Syslog Number of Kafka Partitions")
        ;

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
        ("DEFAULT.analytics_data_ttl",
             opt::value<uint64_t>()->default_value(
                 g_viz_constants.TtlValuesDefault.find(
                 TtlType::GLOBAL_TTL)->second),
             "TTL in hours for analytics data")
        ("DEFAULT.analytics_config_audit_ttl",
             opt::value<uint64_t>()->default_value(
                 g_viz_constants.TtlValuesDefault.find(
                 TtlType::CONFIGAUDIT_TTL)->second),
             "TTL in hours for analytics configuration audit data")
        ("DEFAULT.analytics_statistics_ttl",
             opt::value<uint64_t>()->default_value(
                 g_viz_constants.TtlValuesDefault.find(
                 TtlType::STATSDATA_TTL)->second),
             "TTL in hours for analytics statistics data")
        ("DEFAULT.analytics_flow_ttl",
             opt::value<uint64_t>()->default_value(
                 g_viz_constants.TtlValuesDefault.find(
                 TtlType::FLOWDATA_TTL)->second),
             "TTL in hours for analytics flow data")
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
        ("DEFAULT.grok_key_list",
           opt::value<vector<string> >()->default_value(
               default_grok_key_list, ""),
             "Grok Key List")
        ("DEFAULT.grok_attrib_list",
           opt::value<vector<string> >()->default_value(
               default_grok_attrib_list, ""),
             "Grok Attribute List")
        ("DEFAULT.uve_proxy_list",
           opt::value<vector<string> >()->default_value(
               default_uve_proxy_list, default_uve_proxy),
             "UVE Proxy List")
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
        ("DEFAULT.use_grok", opt::bool_switch(&use_grok_),
             "Enable grok parser")
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
        ("DEFAULT.disable_flow_collection",
            opt::bool_switch(&disable_flow_collection_),
            "Disable flow message collection")
        ;

    // Command line and config file options.
    opt::options_description redis_config("Redis Configuration options");
    redis_config.add_options()
        ("REDIS.port",
             opt::value<uint16_t>()->default_value(default_redis_port),
             "Port of Redis-uve server")
        ("REDIS.server", opt::value<string>()->default_value("127.0.0.1"),
             "IP address of Redis Server")
        ("REDIS.password", opt::value<string>()->default_value(""),
             "password for Redis Server")
        ;

    // Command line and config file options.
    opt::options_description keystone_config("Keystone Configuration options");
    keystone_config.add_options()
        ("KEYSTONE.auth_host", opt::value<string>()->default_value("127.0.0.1"),
             "IP address of keystone Server")
        ("KEYSTONE.auth_port",
             opt::value<uint16_t>()->default_value(default_ks_port),
             "Keystone auth server port")
        ("KEYSTONE.auth_protocol", opt::value<string>()->default_value("http"),
             "protocol used to authenticate with Keystone Server")
        ("KEYSTONE.admin_user", opt::value<string>()->default_value("admin"),
             "Keystone username")
        ("KEYSTONE.admin_password", opt::value<string>()->default_value(
                    "admin123"), "Keystone password")
        ("KEYSTONE.admin_tenant_name", opt::value<string>()->default_value(
                    "tenant"), "Keystone tenant")
        ("KEYSTONE.insecure", opt::bool_switch(&ks_insecure_),
                    "keystone using tls")
        ("KEYSTONE.auth_url", opt::value<string>()->default_value("a.com"),
                    "keystone auth url")
        ("KEYSTONE.memcache_servers", opt::value<string>()->default_value(
                    "127.0.0.1:11211"), "memcache servers")
        ("KEYSTONE.certfile", opt::value<string>()->default_value(
                    "/etc/contrail/ks-cert"), "Keystone certificate")
        ("KEYSTONE.keyfile", opt::value<string>()->default_value(
                    "/etc/contrail/ks-key"), "Keystone private key")
        ("KEYSTONE.cafile", opt::value<string>()->default_value(
                    "/etc/contrail/ks-ca"), "Keystone CA chain")
        ;

    vector<string> default_config_db_server_list;
    string default_config_db_server(host_ip + ":9042");
    default_config_db_server_list.push_back(default_config_db_server);

    vector<string> default_rabbitmq_server_list;
    string default_rabbitmq_server(host_ip + ":5672");
    default_rabbitmq_server_list.push_back(default_rabbitmq_server);
    // configdb_config options.
    opt::options_description configdb_config("Config database Configuration options");
    configdb_config.add_options()
        ("CONFIGDB.config_db_server_list",
             opt::value<vector<string> >()->default_value(
             default_config_db_server_list, default_config_db_server),
             "Config database server list")
        ("CONFIGDB.config_db_username",
             opt::value<string>()->default_value(""),
             "ConfigDB user")
        ("CONFIGDB.config_db_password",
             opt::value<string>()->default_value(""),
             "ConfigDB password")
        ("CONFIGDB.rabbitmq_server_list",
             opt::value<vector<string> >()->default_value(
             default_rabbitmq_server_list, default_rabbitmq_server),
             "RabbitMQ server list")
        ("CONFIGDB.rabbitmq_user",
             opt::value<string>()->default_value("guest"),
             "RabbitMQ user")
        ("CONFIGDB.rabbitmq_password",
             opt::value<string>()->default_value("guest"),
             "RabbitMQ password")
        ("CONFIGDB.rabbitmq_vhost",
             opt::value<string>()->default_value(""),
             "RabbitMQ vhost")
        ("CONFIGDB.rabbitmq_use_ssl",
             opt::value<bool>()->default_value(false),
             "Use SSL for RabbitMQ connection")
        ("CONFIGDB.rabbitmq_ssl_version",
             opt::value<string>()->default_value(""),
             "SSL version for RabbitMQ connection")
        ("CONFIGDB.rabbitmq_ssl_keyfile",
             opt::value<string>()->default_value(""),
             "Keyfile for SSL RabbitMQ connection")
        ("CONFIGDB.rabbitmq_ssl_certfile",
             opt::value<string>()->default_value(""),
             "Certificate file for SSL RabbitMQ connection")
        ("CONFIGDB.rabbitmq_ssl_ca_certs",
             opt::value<string>()->default_value(""),
             "CA Certificate file for SSL RabbitMQ connection")
        ;

    // Command line and config file options.
    opt::options_description sandesh_config("Sandesh Configuration options");
    sandesh::options::AddOptions(&sandesh_config, &sandesh_config_);

    // Command line and config file options for api-server
    opt::options_description api_server_config("Api-Server Configuration options");
    api_server_config.add_options()
        ("API_SERVER.api_server_list",
         opt::value<vector<string> >()->default_value(
            default_api_server_list, default_api_server),
            "Api-Server list")
        ("API_SERVER.api_server_use_ssl",
         opt::bool_switch(&api_server_use_ssl_),
         "Use ssl for connecting to Api-Server")
        ;

    config_file_options_.add(config).add(cassandra_config)
        .add(database_config).add(sandesh_config).add(keystone_config)
        .add(collector_config).add(redis_config).add(api_server_config).add(configdb_config);
    cmdline_options.add(generic).add(config).add(cassandra_config)
        .add(database_config).add(sandesh_config).add(keystone_config)
        .add(collector_config).add(redis_config).add(api_server_config).add(configdb_config);
}

static bool ValidateCompactionStrategyOption(
    const std::string &compaction_strategy,
    const std::string &option) {
    if (!((compaction_strategy ==
        GenDb::g_gendb_constants.DATE_TIERED_COMPACTION_STRATEGY) ||
        (compaction_strategy ==
        GenDb::g_gendb_constants.LEVELED_COMPACTION_STRATEGY) ||
        (compaction_strategy ==
        GenDb::g_gendb_constants.SIZE_TIERED_COMPACTION_STRATEGY))) {
        cout << "Invalid " << option <<  ", please select one of [" <<
            GenDb::g_gendb_constants.DATE_TIERED_COMPACTION_STRATEGY << ", " <<
            GenDb::g_gendb_constants.LEVELED_COMPACTION_STRATEGY << ", " <<
            GenDb::g_gendb_constants.SIZE_TIERED_COMPACTION_STRATEGY << "]" <<
            endl;
        return false;
    }
    return true;
}

uint32_t Options::GenerateHash(const std::vector<std::string> &list) {
    std::string concat_servers;
    std::vector<std::string>::const_iterator iter;
    for (iter = list.begin(); iter != list.end(); iter++) {
        concat_servers += *iter;
    }
    boost::hash<std::string> string_hash;
    return(string_hash(concat_servers));
}

uint32_t Options::GenerateHash(const ConfigClientOptions &config) {
    uint32_t chk_sum = GenerateHash(config.config_db_server_list);
    chk_sum += GenerateHash(config.rabbitmq_server_list);
    boost::hash<std::string> string_hash;
    chk_sum += string_hash(config.rabbitmq_user);
    chk_sum += string_hash(config.rabbitmq_password);
    chk_sum += string_hash(config.config_db_username);
    chk_sum += string_hash(config.config_db_password);
    return chk_sum;
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
           opt::store(opt::parse_config_file(config_file_in, config_file_options_, true),
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
    GetOptValue<uint32_t>(
        var_map, db_write_options_.disk_usage_percentage_high_watermark0_,
        "DATABASE.disk_usage_percentage.high_watermark0");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.disk_usage_percentage_low_watermark0_,
        "DATABASE.disk_usage_percentage.low_watermark0");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.disk_usage_percentage_high_watermark1_,
        "DATABASE.disk_usage_percentage.high_watermark1");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.disk_usage_percentage_low_watermark1_,
        "DATABASE.disk_usage_percentage.low_watermark1");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.disk_usage_percentage_high_watermark2_,
        "DATABASE.disk_usage_percentage.high_watermark2");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.disk_usage_percentage_low_watermark2_,
        "DATABASE.disk_usage_percentage.low_watermark2");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.pending_compaction_tasks_high_watermark0_,
        "DATABASE.pending_compaction_tasks.high_watermark0");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.pending_compaction_tasks_low_watermark0_,
        "DATABASE.pending_compaction_tasks.low_watermark0");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.pending_compaction_tasks_high_watermark1_,
        "DATABASE.pending_compaction_tasks.high_watermark1");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.pending_compaction_tasks_low_watermark1_,
        "DATABASE.pending_compaction_tasks.low_watermark1");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.pending_compaction_tasks_high_watermark2_,
        "DATABASE.pending_compaction_tasks.high_watermark2");
    GetOptValue<uint32_t>(
        var_map, db_write_options_.pending_compaction_tasks_low_watermark2_,
        "DATABASE.pending_compaction_tasks.low_watermark2");

    GetOptValue<string>(
        var_map, db_write_options_.high_watermark0_message_severity_level_,
        "DATABASE.high_watermark0.message_severity_level");
    GetOptValue<string>(
        var_map, db_write_options_.low_watermark0_message_severity_level_,
        "DATABASE.low_watermark0.message_severity_level");
    GetOptValue<string>(
        var_map, db_write_options_.high_watermark1_message_severity_level_,
        "DATABASE.high_watermark1.message_severity_level");
    GetOptValue<string>(
        var_map, db_write_options_.low_watermark1_message_severity_level_,
        "DATABASE.low_watermark1.message_severity_level");
    GetOptValue<string>(
        var_map, db_write_options_.high_watermark2_message_severity_level_,
        "DATABASE.high_watermark2.message_severity_level");
    GetOptValue<string>(
        var_map, db_write_options_.low_watermark2_message_severity_level_,
        "DATABASE.low_watermark2.message_severity_level");

    if (GetOptValueIfNotDefaulted<uint16_t>(var_map, collector_structured_syslog_port_,
            "STRUCTURED_SYSLOG_COLLECTOR.port")) {
        collector_structured_syslog_port_configured_ = true;
    } else {
        collector_structured_syslog_port_configured_ = false;
    }
    GetOptValue< vector<string> >(var_map, collector_structured_syslog_tcp_forward_destination_,
                                  "STRUCTURED_SYSLOG_COLLECTOR.tcp_forward_destination");

    GetOptValue< vector<string> >(var_map, collector_structured_syslog_kafka_broker_list_,
                                  "STRUCTURED_SYSLOG_COLLECTOR.kafka_broker_list");

    GetOptValue<string>(var_map, collector_structured_syslog_kafka_topic_,
                                  "STRUCTURED_SYSLOG_COLLECTOR.kafka_topic");

    GetOptValue<uint16_t>(var_map, collector_structured_syslog_kafka_partitions_,
                                  "STRUCTURED_SYSLOG_COLLECTOR.kafka_partitions");

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
    GetOptValue< vector<string> >(var_map, uve_proxy_list_,
                                  "DEFAULT.uve_proxy_list");
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
    GetOptValue<bool>(var_map, use_grok_, "DEFAULT.use_grok");
    GetOptValue<string>(var_map, syslog_facility_, "DEFAULT.syslog_facility");
    GetOptValue<string>(var_map, kafka_prefix_, "DATABASE.cluster_id");
    GetOptValue<int>(var_map, syslog_port_, "DEFAULT.syslog_port");
    GetOptValue<int>(var_map, sflow_port_, "DEFAULT.sflow_port");
    GetOptValue<int>(var_map, ipfix_port_, "DEFAULT.ipfix_port");
    GetOptValue< vector<string> >(var_map, grok_key_list_, "DEFAULT.grok_key_list");
    GetOptValue< vector<string> >(var_map, grok_attrib_list_, "DEFAULT.grok_attrib_list");
    GetOptValue<uint16_t>(var_map, redis_port_, "REDIS.port");
    GetOptValue<string>(var_map, redis_server_, "REDIS.server");
    GetOptValue<string>(var_map, redis_password_, "REDIS.password");

    GetOptValue<string>(var_map, cassandra_options_.cluster_id_, "DATABASE.cluster_id");

    GetOptValue<string>(var_map, cassandra_options_.user_,
        "CASSANDRA.cassandra_user");
    GetOptValue<string>(var_map, cassandra_options_.password_,
        "CASSANDRA.cassandra_password");
    GetOptValue<string>(var_map, cassandra_options_.compaction_strategy_,
        "CASSANDRA.compaction_strategy");
    if (!ValidateCompactionStrategyOption(
        cassandra_options_.compaction_strategy_,
        "CASSANDRA.compaction_strategy")) {
        exit(-1);
    }
    GetOptValue<string>(var_map,
        cassandra_options_.flow_tables_compaction_strategy_,
        "CASSANDRA.flow_tables.compaction_strategy");
    if (!ValidateCompactionStrategyOption(
        cassandra_options_.flow_tables_compaction_strategy_,
        "CASSANDRA.flow_tables.compaction_strategy")) {
        exit(-1);
    }
    GetOptValue<uint16_t>(var_map, ks_port_, "KEYSTONE.auth_port");
    GetOptValue<string>(var_map, ks_server_, "KEYSTONE.auth_host");
    GetOptValue<string>(var_map, ks_protocol_, "KEYSTONE.auth_protocol");
    GetOptValue<string>(var_map, ks_user_, "KEYSTONE.admin_user");
    GetOptValue<string>(var_map, ks_password_, "KEYSTONE.admin_password");
    GetOptValue<string>(var_map, ks_tenant_, "KEYSTONE.admin_tenant_name");
    GetOptValue<string>(var_map, ks_authurl_, "KEYSTONE.auth_url");
    GetOptValue<string>(var_map, memcache_servers_, "KEYSTONE.memcache_servers");
    GetOptValue<string>(var_map, ks_cert_, "KEYSTONE.certfile");
    GetOptValue<string>(var_map, ks_key_, "KEYSTONE.keyfile");
    GetOptValue<string>(var_map, ks_ca_, "KEYSTONE.cafile");

    sandesh::options::ProcessOptions(var_map, &sandesh_config_);

    GetOptValue< vector<string> >(var_map, api_server_list_,
                                  "API_SERVER.api_server_list");
    api_server_checksum_ = GenerateHash(api_server_list_);
    std::random_shuffle(api_server_list_.begin(), api_server_list_.end());

    GetOptValue<bool>(var_map, api_server_use_ssl_,
                      "API_SERVER.api_server_use_ssl");
    ParseConfigOptions(var_map);
}

void Options::ParseReConfig() {
    // ReParse the filtered config params
    opt::variables_map var_map;
    ifstream config_file_in;
    for(std::vector<int>::size_type i = 0; i != config_file_.size(); i++) {
        config_file_in.open(config_file_[i].c_str());
        if (config_file_in.good()) {
           opt::store(opt::parse_config_file(config_file_in,
                                    config_file_options_, true), var_map);
        }
        config_file_in.close();
    }
    api_server_list_.clear();
    GetOptValue< vector<string> >(var_map, api_server_list_,
                                  "API_SERVER.api_server_list");
    uint32_t new_checksum = GenerateHash(api_server_list_);
    if (api_server_checksum_ != new_checksum) {
        api_server_checksum_ = new_checksum;
        std::random_shuffle(api_server_list_.begin(),
                            api_server_list_.end());
    }

    uint32_t old_config_chksum = configdb_chksum_;
    ParseConfigOptions(var_map);
    if ((old_config_chksum != configdb_chksum_) && config_client_manager_) {
        config_client_manager_->ReinitConfigClient(configdb_options());
    }
}

void Options::ParseConfigOptions(const boost::program_options::variables_map
                                 &var_map) {
    configdb_options_.config_db_server_list.clear();
    GetOptValue< vector<string> >(var_map,
                                  configdb_options_.config_db_server_list,
                                  "CONFIGDB.config_db_server_list");
    GetOptValue<string>(var_map,
                     configdb_options_.config_db_username,
                     "CONFIGDB.config_db_username");
    GetOptValue<string>(var_map,
                     configdb_options_.config_db_password,
                     "CONFIGDB.config_db_password");
    configdb_options_.rabbitmq_server_list.clear();
    GetOptValue< vector<string> >(var_map,
                     configdb_options_.rabbitmq_server_list,
                     "CONFIGDB.rabbitmq_server_list");
    GetOptValue<string>(var_map,
                     configdb_options_.rabbitmq_user,
                     "CONFIGDB.rabbitmq_user");
    GetOptValue<string>(var_map,
                     configdb_options_.rabbitmq_password,
                     "CONFIGDB.rabbitmq_password");
    GetOptValue<string>(var_map,
                     configdb_options_.rabbitmq_vhost,
                     "CONFIGDB.rabbitmq_vhost");
    GetOptValue<bool>(var_map,
                     configdb_options_.rabbitmq_use_ssl,
                     "CONFIGDB.rabbitmq_use_ssl");
    GetOptValue<string>(var_map,
                     configdb_options_.rabbitmq_ssl_version,
                     "CONFIGDB.rabbitmq_ssl_version");
    GetOptValue<string>(var_map,
                     configdb_options_.rabbitmq_ssl_keyfile,
                     "CONFIGDB.rabbitmq_ssl_keyfile");
    GetOptValue<string>(var_map,
                     configdb_options_.rabbitmq_ssl_certfile,
                     "CONFIGDB.rabbitmq_ssl_certfile");
    GetOptValue<string>(var_map,
                     configdb_options_.rabbitmq_ssl_ca_certs,
                     "CONFIGDB.rabbitmq_ssl_ca_certs");
    configdb_chksum_ = GenerateHash(configdb_options_);
}
