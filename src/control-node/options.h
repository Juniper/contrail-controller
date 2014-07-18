/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/program_options.hpp>
#include "io/event_manager.h"

// Process command line/configuration file options for control-node.
class Options {
public:
    Options();
    bool Parse(EventManager &evm, int argc, char **argv);

    const std::string bgp_config_file() const { return bgp_config_file_; }
    const uint16_t bgp_port() const { return bgp_port_; }
    const std::vector<std::string> collector_server_list() const {
        return collector_server_list_;
    }
    const std::string config_file() const { return config_file_; };
    const std::string discovery_server() const { return discovery_server_; }
    const uint16_t discovery_port() const { return discovery_port_; }
    const std::string hostname() const { return hostname_; }
    const std::string host_ip() const { return host_ip_; }
    const uint16_t http_server_port() const { return http_server_port_; }
    const std::string log_category() const { return log_category_; }
    const bool log_disable() const { return log_disable_; }
    const std::string log_file() const { return log_file_; }
    const int log_files_count() const { return log_files_count_; }
    const long log_file_size() const { return log_file_size_; }
    const std::string log_level() const { return log_level_; }
    const bool log_local() const { return log_local_; }
    const bool use_syslog() const { return use_syslog_; }
    const std::string syslog_facility() const { return syslog_facility_; }
    const std::string ifmap_server_url() const { return ifmap_server_url_; }
    const std::string ifmap_password() const { return ifmap_password_; }
    const std::string ifmap_user() const { return ifmap_user_; }
    const std::string ifmap_certs_store() const { return ifmap_certs_store_; }
    const uint16_t xmpp_port() const { return xmpp_port_; }
    const bool test_mode() const { return test_mode_; }
    const bool collectors_configured() const { return collectors_configured_; }

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
    int log_files_count_;
    long log_file_size_;
    std::string log_level_;
    bool log_local_;
    bool use_syslog_;
    std::string syslog_facility_;
    std::string ifmap_server_url_;
    std::string ifmap_password_;
    std::string ifmap_user_;
    std::string ifmap_certs_store_;
    uint16_t xmpp_port_;
    bool test_mode_;
    bool collectors_configured_;

    std::vector<std::string> default_collector_server_list_;
    boost::program_options::options_description config_file_options_;
};
