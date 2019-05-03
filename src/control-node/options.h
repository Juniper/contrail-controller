/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/program_options.hpp>
#include "config_client_options.h"
#include "io/event_manager.h"
#include "sandesh/sandesh.h"

class ConfigClientManager;

// Process command line/configuration file options for control-node.
class Options {
public:
    Options();
    bool Parse(EventManager &evm, int argc, char **argv);

    std::string bgp_config_file() const { return bgp_config_file_; }
    uint16_t bgp_port() const { return bgp_port_; }
    std::vector<std::string> collector_server_list() const {
        return collector_server_list_;
    }
    std::vector<std::string> randomized_collector_server_list() const {
        return randomized_collector_server_list_;
    }
    std::string config_file() const { return config_file_; };
    std::string hostname() const { return hostname_; }
    std::string host_ip() const { return host_ip_; }
    uint16_t http_server_port() const { return http_server_port_; }
    std::string log_category() const { return log_category_; }
    bool log_disable() const { return log_disable_; }
    std::string log_file() const { return log_file_; }
    std::string log_property_file() const { return log_property_file_; }
    int log_files_count() const { return log_files_count_; }
    long log_file_size() const { return log_file_size_; }
    std::string log_level() const { return log_level_; }
    bool log_local() const { return log_local_; }
    bool use_syslog() const { return use_syslog_; }
    std::string syslog_facility() const { return syslog_facility_; }
    bool mvpn_ipv4_enable() const { return mvpn_ipv4_enable_; }
    bool task_track_run_time() const { return task_track_run_time_; }
    std::string config_db_user() const {
        return configdb_options_.config_db_username;
    }
    std::string config_db_password() const {
        return configdb_options_.config_db_password;
    }
    bool config_db_use_ssl() const {
        return configdb_options_.config_db_use_ssl;
    }
    std::vector<std::string> config_db_server_list() const {
        return configdb_options_.config_db_server_list;
    }
    std::vector<std::string> rabbitmq_server_list() const {
        return configdb_options_.rabbitmq_server_list;
    }
    std::string rabbitmq_user() const {
        return configdb_options_.rabbitmq_user;
    }
    std::string rabbitmq_password() const {
        return configdb_options_.rabbitmq_password;
    }
    bool rabbitmq_ssl_enabled() const {
        return configdb_options_.rabbitmq_use_ssl;
    }
    bool using_etcd_client() const {
        return configdb_options_.config_db_use_etcd;
    }
    const ConfigClientOptions &configdb_options() const {
        return configdb_options_;
    }
    uint16_t xmpp_port() const { return xmpp_port_; }
    bool xmpp_auth_enabled() const { return xmpp_auth_enable_; }
    std::string xmpp_server_cert() const { return xmpp_server_cert_; }
    std::string xmpp_server_key() const { return xmpp_server_key_; }
    std::string xmpp_ca_cert() const { return xmpp_ca_cert_; }
    bool test_mode() const { return test_mode_; }
    bool collectors_configured() const { return collectors_configured_; }
    int tcp_hold_time() const { return tcp_hold_time_; }
    bool optimize_snat() const { return optimize_snat_; }
    bool gr_helper_bgp_disable() const { return gr_helper_bgp_disable_; }
    bool gr_helper_xmpp_disable() const { return gr_helper_xmpp_disable_; }
    const std::string cassandra_user() const { return cassandra_user_; }
    const std::string cassandra_password() const { return cassandra_password_; }
    const std::vector<std::string> cassandra_server_list() const {
        return cassandra_server_list_;
    }
    const SandeshConfig &sandesh_config() const { return sandesh_config_; }

    void ParseReConfig(bool force_reinit);

    void set_config_client_manager(ConfigClientManager *mgr) {
        config_client_manager_ = mgr;
    }

private:

    bool Process(int argc, char *argv[],
                 boost::program_options::options_description &cmdline_options);
    void Initialize(EventManager &evm,
                    boost::program_options::options_description &options);
    void ParseConfigOptions(const boost::program_options::variables_map
                            &var_map);
    uint32_t GenerateHash(const std::vector<std::string> &list);
    uint32_t GenerateHash(const ConfigClientOptions &config);

    std::string bgp_config_file_;
    uint16_t bgp_port_;
    std::vector<std::string> collector_server_list_;
    std::vector<std::string> randomized_collector_server_list_;
    uint32_t collector_chksum_;
    uint32_t configdb_chksum_;
    std::string config_file_;
    std::string hostname_;
    std::string host_ip_;
    uint16_t http_server_port_;
    std::string log_category_;
    bool log_disable_;
    std::string log_file_;
    std::string log_property_file_;
    int log_files_count_;
    long log_file_size_;
    std::string log_level_;
    bool log_local_;
    bool use_syslog_;
    std::string syslog_facility_;
    bool mvpn_ipv4_enable_;
    bool task_track_run_time_;
    ConfigClientOptions configdb_options_;
    uint16_t xmpp_port_;
    bool xmpp_auth_enable_;
    std::string xmpp_server_cert_;
    std::string xmpp_server_key_;
    std::string xmpp_ca_cert_;
    bool test_mode_;
    bool collectors_configured_;
    int tcp_hold_time_;
    bool optimize_snat_;
    uint32_t sandesh_ratelimit_;
    bool gr_helper_bgp_disable_;
    bool gr_helper_xmpp_disable_;
    std::string cassandra_user_;
    std::string cassandra_password_;
    std::vector<std::string> cassandra_server_list_;
    std::vector<std::string> default_collector_server_list_;
    SandeshConfig sandesh_config_;
    boost::program_options::options_description config_file_options_;
    ConfigClientManager *config_client_manager_;
};
