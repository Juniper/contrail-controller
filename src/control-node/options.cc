/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "options.h"

#include <fstream>
#include <iostream>
#include <boost/asio/ip/host_name.hpp>
#include <boost/functional/hash.hpp>

#include "control-node/buildinfo.h"
#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"
#include "config_client_manager.h"
#include <base/address_util.h>
#include <base/options_util.h>

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;
using namespace options::util;

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

    default_collector_server_list_.push_back("127.0.0.1:8086");

    vector<string> default_config_db_server_list;
    string default_config_db_server(host_ip + ":9042");
    default_config_db_server_list.push_back(default_config_db_server);

    vector<string> default_rabbitmq_server_list;
    string default_rabbitmq_server(host_ip + ":5672");
    default_rabbitmq_server_list.push_back(default_rabbitmq_server);

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
        ("DEFAULT.log_file", opt::value<string>()->default_value(
             "/var/log/contrail/contrail-control.log"),
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
        ("DEFAULT.log_local",
             opt::bool_switch(&log_local_)->default_value(true),
             "Enable local logging of sandesh messages")
        ("DEFAULT.mvpn_ipv4_enable", opt::bool_switch(&mvpn_ipv4_enable_),
             "Enable NGEN Multicast VPN support for IPv4 routes")
        ("DEFAULT.use_syslog", opt::bool_switch(&use_syslog_),
             "Enable logging to syslog")
        ("DEFAULT.syslog_facility",
             opt::value<string>()->default_value("LOG_LOCAL0"),
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
        ("CONFIGDB.config_db_use_ssl",
             opt::value<bool>()->default_value(false),
             "Use SSL for Cassandra connection")
        ("CONFIGDB.config_db_ca_certs",
             opt::value<string>()->default_value(""),
             "CA Certificate file for SSL Cassandra connection")
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

        ("CONFIGDB.config_db_use_etcd",
             opt::value<bool>()->default_value(false),
             "Use etcd as the contrail DB client")
        ;

    sandesh::options::AddOptions(&config, &sandesh_config_);

    config_file_options_.add(config);
    cmdline_options.add(generic).add(config);
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

    // Randomize Collector List
    collector_chksum_ = GenerateHash(collector_server_list_);
    randomized_collector_server_list_ = collector_server_list_;
    std::random_shuffle(randomized_collector_server_list_.begin(),
                        randomized_collector_server_list_.end());

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

    ParseConfigOptions(var_map);

    sandesh::options::ProcessOptions(var_map, &sandesh_config_);
    return true;
}

void Options::ParseReConfig(bool force_reinit) {
    // ReParse the filtered config params
    opt::variables_map var_map;
    ifstream config_file_in;
    config_file_in.open(config_file_.c_str());
    if (config_file_in.good()) {
        opt::store(opt::parse_config_file(config_file_in, config_file_options_),
                   var_map);
    }
    config_file_in.close();

    collector_server_list_.clear();
    GetOptValue< vector<string> >(var_map, collector_server_list_,
                                  "DEFAULT.collectors");

    uint32_t new_chksum = GenerateHash(collector_server_list_);
    if (collector_chksum_ != new_chksum) {
        collector_chksum_ = new_chksum;
        randomized_collector_server_list_.clear();
        randomized_collector_server_list_ = collector_server_list_;
        std::random_shuffle(randomized_collector_server_list_.begin(),
                            randomized_collector_server_list_.end());
    }
    // ReConnect Collectors irrespective of change list to achieve
    // rebalance when older collector node/s are up again.
    Sandesh::ReConfigCollectors(randomized_collector_server_list_);

    uint32_t old_config_chksum = configdb_chksum_;
    ParseConfigOptions(var_map);
    if ((force_reinit || old_config_chksum != configdb_chksum_) &&
            config_client_manager_) {
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
    GetOptValue<bool>(var_map,
                     configdb_options_.config_db_use_ssl,
                     "CONFIGDB.config_db_use_ssl");
    GetOptValue<string>(var_map,
                     configdb_options_.config_db_ca_certs,
                     "CONFIGDB.config_db_ca_certs");
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
    GetOptValue<bool>(var_map,
                     configdb_options_.config_db_use_etcd,
                     "CONFIGDB.config_db_use_etcd");
    configdb_chksum_ = GenerateHash(configdb_options_);
}
