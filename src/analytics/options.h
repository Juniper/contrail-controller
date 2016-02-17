/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cassert>
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
    const std::vector<std::string> kafka_broker_list() const {
        return kafka_broker_list_;
    }
    const uint16_t partitions() const { return partitions_; }
    const std::string collector_server() const { return collector_server_; }
    const uint16_t collector_port() const { return collector_port_; };
    bool collector_protobuf_port(uint16_t *collector_protobuf_port) const {
        if (collector_protobuf_port_configured_) {
            *collector_protobuf_port = collector_protobuf_port_;
        }
        return collector_protobuf_port_configured_;
    }
    const std::vector<std::string> config_file() const {
        return config_file_;
    }
    const std::string discovery_server() const { return discovery_server_; }
    const uint16_t discovery_port() const { return discovery_port_; }
    const std::string redis_server() const { return redis_server_; }
    const uint16_t redis_port() const { return redis_port_; }
    const std::string redis_password() const { return redis_password_; }
    const std::string cassandra_user() const { return cassandra_user_; }
    const std::string cassandra_password() const { return cassandra_password_; }
    const std::string hostname() const { return hostname_; }
    const std::string host_ip() const { return host_ip_; }
    const uint16_t http_server_port() const { return http_server_port_; }
    const std::string log_category() const { return log_category_; }
    const bool log_disable() const { return log_disable_; }
    const std::string log_file() const { return log_file_; }
    const std::string log_property_file() const { return log_property_file_; }
    const int log_files_count() const { return log_files_count_; }
    const long log_file_size() const { return log_file_size_; }
    const std::string log_level() const { return log_level_; }
    const bool log_local() const { return log_local_; }
    const bool use_syslog() const { return use_syslog_; }
    const std::string syslog_facility() const { return syslog_facility_; }
    const std::string kafka_prefix() const { return kafka_prefix_; }
    const bool dup() const { return dup_; }
    const uint64_t analytics_data_ttl() const { return analytics_data_ttl_; }
    const uint64_t analytics_flow_ttl() const { return analytics_flow_ttl_; }
    const uint64_t analytics_statistics_ttl() const { return analytics_statistics_ttl_; }
    const uint64_t analytics_config_audit_ttl() const { return analytics_config_audit_ttl_; }
    const int syslog_port() const { return syslog_port_; }
    const int sflow_port() const { return sflow_port_; }
    const int ipfix_port() const { return ipfix_port_; }
    const bool test_mode() const { return test_mode_; }
    const uint32_t sandesh_send_rate_limit() const { return sandesh_ratelimit_; }
    const bool disable_flow_collection() const { return disable_flow_collection_; }

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
    std::vector<std::string> config_file_;
    std::string discovery_server_;
    uint16_t discovery_port_;
    std::string redis_server_;
    uint16_t redis_port_;
    std::string redis_password_;
    std::string cassandra_user_;
    std::string cassandra_password_;
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
    std::string kafka_prefix_;
    int syslog_port_;
    int sflow_port_;
    int ipfix_port_;
    bool test_mode_;
    bool dup_;
    uint64_t analytics_data_ttl_;
    uint64_t analytics_config_audit_ttl_;
    uint64_t analytics_flow_ttl_;
    uint64_t analytics_statistics_ttl_;
    std::vector<std::string> cassandra_server_list_;
    std::vector<std::string> kafka_broker_list_;
    uint16_t partitions_;
    uint32_t sandesh_ratelimit_;
    bool disable_flow_collection_;

    boost::program_options::options_description config_file_options_;
};
