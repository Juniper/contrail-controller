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
    const std::vector<std::string> collector_server_list() const {
        return collector_server_list_;
    }
    const std::string config_file() const { return config_file_; };
    const std::string discovery_server() const { return discovery_server_; }
    const uint16_t discovery_port() const { return discovery_port_; }
    const std::string redis_server() const { return redis_server_; }
    const uint16_t redis_port() const { return redis_port_; }
    const std::string hostname() const { return hostname_; }
    const std::string host_ip() const { return host_ip_; }
    const uint16_t http_server_port() const { return http_server_port_; }
    const uint64_t start_time() const { return start_time_; }
    const int max_tasks() const { return max_tasks_; }
    const int max_slice() const { return max_slice_; }
    const std::string log_category() const { return log_category_; }
    const bool log_disable() const { return log_disable_; }
    const std::string log_file() const { return log_file_; }
    const int log_files_count() const { return log_files_count_; }
    const long log_file_size() const { return log_file_size_; }
    const std::string log_level() const { return log_level_; }
    const bool log_local() const { return log_local_; }
    const int analytics_data_ttl() const { return analytics_data_ttl_; }
    const bool test_mode() const { return test_mode_; }

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
    void Process(int argc, char *argv[],
            boost::program_options::options_description &cmdline_options);
    void Initialize(EventManager &evm,
                    boost::program_options::options_description &options);

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
    uint64_t start_time_;
    int max_tasks_;
    int max_slice_;
    bool test_mode_;
    int analytics_data_ttl_;
    std::vector<std::string> cassandra_server_list_;
    std::vector<std::string> collector_server_list_;

    boost::program_options::options_description config_file_options_;
};
