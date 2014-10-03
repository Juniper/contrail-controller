/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/program_options.hpp>
#include "io/event_manager.h"

#define ANALYTICS_DATA_TTL_DEFAULT 48 // g_viz_constants.AnalyticsTTL

// Process command line/configuration file options for collector.
class Options {
public:
    Options();
    bool Parse(EventManager &evm, int argc, char **argv);

    const std::vector<std::string> cassandra_server_list() const {
        return cassandra_server_list_;
    }
    const std::string collector_server() const { return collector_server_; }
    const uint16_t collector_port() const { return collector_port_; };
    bool collector_protobuf_port(uint16_t *collector_protobuf_port) const {
        if (collector_protobuf_port_configured_) {
            *collector_protobuf_port = collector_protobuf_port_;
        }
        return collector_protobuf_port_configured_;
    }
    const std::string config_file() const { return config_file_; };
    const std::string discovery_server() const { return discovery_server_; }
    const uint16_t discovery_port() const { return discovery_port_; }
    const std::string redis_server() const { return redis_server_; }
    const uint16_t redis_port() const { return redis_port_; }
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
    const bool dup() const { return dup_; }
    const int analytics_data_ttl() const { return analytics_data_ttl_; }
    const int syslog_port() const { return syslog_port_; }
    const int sflow_port() const { return sflow_port_; }
    const int ipfix_port() const { return ipfix_port_; }
    const bool test_mode() const { return test_mode_; }

private:
    template <typename ValueType>
    bool GetOptValueIfNotDefaulted(
        const boost::program_options::variables_map &var_map,
        ValueType &var, std::string val);
    // Implementation overloads
    template <typename ValueType>
    bool GetOptValueIfNotDefaultedImpl(
        const boost::program_options::variables_map &var_map,
        ValueType &var, std::string val, ValueType*);
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
    void Process(int argc, char *argv[],
            boost::program_options::options_description &cmdline_options);
    void Initialize(EventManager &evm,
                    boost::program_options::options_description &options);

    std::string collector_server_;
    uint16_t collector_port_;
    uint16_t collector_protobuf_port_;
    bool collector_protobuf_port_configured_;
    std::string config_file_;
    std::string discovery_server_;
    uint16_t discovery_port_;
    std::string redis_server_;
    uint16_t redis_port_;
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
    int syslog_port_;
    int sflow_port_;
    int ipfix_port_;
    bool test_mode_;
    bool dup_;
    int analytics_data_ttl_;
    std::vector<std::string> cassandra_server_list_;

    boost::program_options::options_description config_file_options_;
};
