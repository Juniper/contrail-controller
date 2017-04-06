/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio/ip/host_name.hpp>
#include <boost/functional/hash.hpp>
#include <fstream>
#include <iostream>

#include "base/contrail_ports.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "base/util.h"
#include "cmn/buildinfo.h"
#include "cmn/dns_options.h"
#include "net/address_util.h"

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
// options from /etc/contrail/contrail-dns.conf
void Options::Initialize(EventManager &evm,
                         opt::options_description &cmdline_options) {
    boost::system::error_code error;
    string hostname = host_name(error);
    string host_ip = GetHostIp(evm.io_service(), hostname);
    if (host_ip.empty()) {
        cout << "Error! Cannot resolve host " << hostname <<
                " to a valid IP address";
        exit(-1);
    }

    opt::options_description generic("Generic options");

    // Command line only options.
    generic.add_options()
        ("conf_file", opt::value<string>()->default_value(
                                                    "/etc/contrail/contrail-dns.conf"),
             "Configuration file")
         ("help", "help message")
        ("version", "Display version information")
    ;

    uint16_t default_dns_server_port = ContrailPorts::DnsServerPort();
    uint16_t default_http_server_port = ContrailPorts::HttpPortDns();

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
        ("DEFAULT.collectors",
           opt::value<vector<string> >()->default_value(
               default_collector_server_list_, "127.0.0.1:8086"),
             "Collector server list")
        ("DEFAULT.dns_config_file",
             opt::value<string>()->default_value("dns_config.xml"),
             "DNS Configuration file")

        ("DEFAULT.named_config_file",
             opt::value<string>()->default_value("contrail-named.conf"),
             "Named Configuration file")
        ("DEFAULT.named_config_directory",
             opt::value<string>()->default_value("/etc/contrail/dns"),
             "Named Configuration directory")
        ("DEFAULT.named_log_file",
             opt::value<string>()->default_value("/var/log/contrail/contrail-named.log"),
             "Named log file")
        ("DEFAULT.rndc_config_file",
             opt::value<string>()->default_value("contrail-rndc.conf"),
             "Rndc Configuration file")
        ("DEFAULT.rndc_secret",
             opt::value<string>()->default_value("xvysmOR8lnUQRBcunkC6vg=="),
             "RNDC secret")
        ("DEFAULT.named_max_cache_size",
             opt::value<string>()->default_value("32M"),
             "Maximum cache size, in bytes, used by contrail-named (per view)")
        ("DEFAULT.named_max_retransmissions",
             opt::value<uint16_t>()->default_value(12),
             "Maximum number of retries to named")
        ("DEFAULT.named_retransmission_interval",
             opt::value<uint16_t>()->default_value(1000),
             "Retranmission interval in msec")

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
        ("DEFAULT.log_property_file", opt::value<string>()->default_value(""),
             "log4cplus property file name")
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
        ("DEFAULT.test_mode", opt::bool_switch(&test_mode_),
             "Enable dns to run in test-mode")
        ("DEFAULT.sandesh_send_rate_limit",
              opt::value<uint32_t>()->default_value(
              g_sandesh_constants.DEFAULT_SANDESH_SEND_RATELIMIT),
              "Sandesh send rate limit in messages/sec")

        ("CONFIGDB.config_db_server_list",
             opt::value<vector<string> >()->default_value(
             default_config_db_server_list, default_config_db_server),
             "Config database server list")
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

        ("DEFAULT.xmpp_dns_auth_enable", opt::bool_switch(&xmpp_auth_enable_),
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
        ("SANDESH.sandesh_keyfile", opt::value<string>()->default_value(
            "/etc/contrail/ssl/private/server-privkey.pem"),
            "Sandesh ssl private key")
        ("SANDESH.sandesh_certfile", opt::value<string>()->default_value(
            "/etc/contrail/ssl/certs/server.pem"),
            "Sandesh ssl certificate")
        ("SANDESH.sandesh_ca_cert", opt::value<string>()->default_value(
            "/etc/contrail/ssl/certs/ca-cert.pem"),
            "Sandesh CA ssl certificate")
        ("SANDESH.sandesh_ssl_enable",
             opt::bool_switch(&sandesh_config_.sandesh_ssl_enable),
             "Enable ssl for sandesh connection")
        ("SANDESH.introspect_ssl_enable",
             opt::bool_switch(&sandesh_config_.introspect_ssl_enable),
             "Enable ssl for introspect connection")
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

uint32_t Options::GenerateHash(std::vector<std::string> &list) {
    std::string concat_servers;
    std::vector<std::string>::iterator iter;
    for (iter = list.begin(); iter != list.end(); iter++) {
        concat_servers += *iter;
    }
    boost::hash<std::string> string_hash;
    return(string_hash(concat_servers));
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

    // Randomize Collector List
    collector_chksum_ = GenerateHash(collector_server_list_);
    randomized_collector_server_list_ = collector_server_list_;
    std::random_shuffle(randomized_collector_server_list_.begin(),
                        randomized_collector_server_list_.end());

    GetOptValue<string>(var_map, named_config_file_,
                        "DEFAULT.named_config_file");
    GetOptValue<string>(var_map, named_config_dir_,
                        "DEFAULT.named_config_directory");
    GetOptValue<string>(var_map, named_log_file_, "DEFAULT.named_log_file");
    GetOptValue<string>(var_map, rndc_config_file_, "DEFAULT.rndc_config_file");
    GetOptValue<string>(var_map, rndc_secret_, "DEFAULT.rndc_secret");
    GetOptValue<string>(var_map, named_max_cache_size_,
                        "DEFAULT.named_max_cache_size");
    GetOptValue<uint16_t>(var_map, named_max_retransmissions_,
                        "DEFAULT.named_max_retransmissions");
    GetOptValue<uint16_t>(var_map, named_retransmission_interval_,
                        "DEFAULT.named_retransmission_interval");

    GetOptValue<string>(var_map, host_ip_, "DEFAULT.hostip");
    GetOptValue<string>(var_map, hostname_, "DEFAULT.hostname");

    GetOptValue<uint16_t>(var_map, http_server_port_,
                          "DEFAULT.http_server_port");

    GetOptValue<uint16_t>(var_map, dns_server_port_, "DEFAULT.dns_server_port");

    GetOptValue<string>(var_map, log_category_, "DEFAULT.log_category");
    GetOptValue<string>(var_map, log_file_, "DEFAULT.log_file");
    GetOptValue<string>(var_map, log_property_file_,
                        "DEFAULT.log_property_file");
    GetOptValue<int>(var_map, log_files_count_, "DEFAULT.log_files_count");
    GetOptValue<long>(var_map, log_file_size_, "DEFAULT.log_file_size");
    GetOptValue<string>(var_map, log_level_, "DEFAULT.log_level");
    GetOptValue<bool>(var_map, use_syslog_, "DEFAULT.use_syslog");
    GetOptValue<string>(var_map, syslog_facility_, "DEFAULT.syslog_facility");
    GetOptValue<uint32_t>(var_map, send_ratelimit_,
                              "DEFAULT.sandesh_send_rate_limit");
    GetOptValue< vector<string> >(var_map,
                                  configdb_options_.config_db_server_list,
                                  "CONFIGDB.config_db_server_list");
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

    GetOptValue<bool>(var_map, xmpp_auth_enable_, "DEFAULT.xmpp_dns_auth_enable");
    GetOptValue<string>(var_map, xmpp_server_cert_, "DEFAULT.xmpp_server_cert");
    GetOptValue<string>(var_map, xmpp_server_key_, "DEFAULT.xmpp_server_key");
    GetOptValue<string>(var_map, xmpp_ca_cert_, "DEFAULT.xmpp_ca_cert");

    GetOptValue<string>(var_map, sandesh_config_.keyfile,
                        "SANDESH.sandesh_keyfile");
    GetOptValue<string>(var_map, sandesh_config_.certfile,
                        "SANDESH.sandesh_certfile");
    GetOptValue<string>(var_map, sandesh_config_.ca_cert,
                        "SANDESH.sandesh_ca_cert");
    GetOptValue<bool>(var_map, sandesh_config_.sandesh_ssl_enable,
                      "SANDESH.sandesh_ssl_enable");
    GetOptValue<bool>(var_map, sandesh_config_.introspect_ssl_enable,
                      "SANDESH.introspect_ssl_enable");
}

void Options::ParseReConfig() {
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
        // ReConnect Collectors
        Sandesh::ReConfigCollectors(randomized_collector_server_list_);
    }
}
