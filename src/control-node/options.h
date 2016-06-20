/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/program_options.hpp>
#include "io/event_manager.h"
#include "ifmap/ifmap_config_options.h"

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
    std::string config_file() const { return config_file_; };
    std::string discovery_server() const { return discovery_server_; }
    uint16_t discovery_port() const { return discovery_port_; }
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
    bool task_track_run_time() const { return task_track_run_time_; }
    std::string ifmap_server_url() const {
        return ifmap_config_options_.server_url;
    }
    std::string ifmap_password() const {
        return ifmap_config_options_.password;
    }
    std::string ifmap_user() const { return ifmap_config_options_.user; }
    std::string ifmap_certs_store() const {
        return ifmap_config_options_.certs_store;
    }
    int ifmap_stale_entries_cleanup_timeout() const {
        return ifmap_config_options_.stale_entries_cleanup_timeout;
    }
    int ifmap_end_of_rib_timeout() const {
        return ifmap_config_options_.end_of_rib_timeout;
    }
    int ifmap_peer_response_wait_time() const {
        return ifmap_config_options_.peer_response_wait_time;
    }
    const IFMapConfigOptions &ifmap_config_options() const {
        return ifmap_config_options_;
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
    bool gr_helper_bgp_enable() const { return gr_helper_bgp_enable_; }
    bool gr_helper_xmpp_enable() const { return gr_helper_xmpp_enable_; }
    uint32_t sandesh_send_rate_limit() const { return sandesh_ratelimit_; }

private:

    template <typename ValueType>
    void GetOptValue(const boost::program_options::variables_map &var_map,
                     ValueType &var, std::string val);
    // Implementation overloads
    template <typename ValueType>
    void GetOptValueImpl(const boost::program_options::variables_map &var_map,
                         ValueType &var, std::string val, ValueType*);
    template <typename ElementType>
    void GetOptValueImpl(const boost::program_options::variables_map &var_map,
                         std::vector<ElementType> &var, std::string val,
                         std::vector<ElementType> *);
    bool Process(int argc, char *argv[],
                 boost::program_options::options_description &cmdline_options);
    void Initialize(EventManager &evm,
                    boost::program_options::options_description &options);

    std::string bgp_config_file_;
    uint16_t bgp_port_;
    std::vector<std::string> collector_server_list_;
    std::string config_file_;
    std::string discovery_server_;
    uint16_t discovery_port_;
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
    bool task_track_run_time_;
    IFMapConfigOptions ifmap_config_options_;
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
    bool gr_helper_bgp_enable_;
    bool gr_helper_xmpp_enable_;
    std::vector<std::string> default_collector_server_list_;
    boost::program_options::options_description config_file_options_;
};
