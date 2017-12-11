/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ANALYTICS_OPTIONS_H_
#define ANALYTICS_OPTIONS_H_

#include <cassert>
#include <boost/program_options.hpp>
#include "io/event_manager.h"
#include "viz_types.h"
#include "config/config-client-mgr/config_client_options.h"

class ConfigClientManager;

#define ANALYTICS_DATA_TTL_DEFAULT 48 // g_viz_constants.AnalyticsTTL

class DbWriteOptions {
public:
    const uint32_t get_disk_usage_percentage_high_watermark0() const {
        return disk_usage_percentage_high_watermark0_; }
    const uint32_t get_disk_usage_percentage_low_watermark0() const {
        return disk_usage_percentage_low_watermark0_; }
    const uint32_t get_disk_usage_percentage_high_watermark1() const {
        return disk_usage_percentage_high_watermark1_; }
    const uint32_t get_disk_usage_percentage_low_watermark1() const {
        return disk_usage_percentage_low_watermark1_; }
    const uint32_t get_disk_usage_percentage_high_watermark2() const {
        return disk_usage_percentage_high_watermark2_; }
    const uint32_t get_disk_usage_percentage_low_watermark2() const {
        return disk_usage_percentage_low_watermark2_; }

    const uint32_t get_pending_compaction_tasks_high_watermark0() const {
        return pending_compaction_tasks_high_watermark0_; }
    const uint32_t get_pending_compaction_tasks_low_watermark0() const {
        return pending_compaction_tasks_low_watermark0_; }
    const uint32_t get_pending_compaction_tasks_high_watermark1() const {
        return pending_compaction_tasks_high_watermark1_; }
    const uint32_t get_pending_compaction_tasks_low_watermark1() const {
        return pending_compaction_tasks_low_watermark1_; }
    const uint32_t get_pending_compaction_tasks_high_watermark2() const {
        return pending_compaction_tasks_high_watermark2_; }
    const uint32_t get_pending_compaction_tasks_low_watermark2() const {
        return pending_compaction_tasks_low_watermark2_; }

    SandeshLevel::type get_high_watermark0_message_severity_level() const {
        return Sandesh::StringToLevel(
                high_watermark0_message_severity_level_); }
    SandeshLevel::type get_low_watermark0_message_severity_level() const {
        return Sandesh::StringToLevel(
                low_watermark0_message_severity_level_); }
    SandeshLevel::type get_high_watermark1_message_severity_level() const {
        return Sandesh::StringToLevel(
                high_watermark1_message_severity_level_); }
    SandeshLevel::type get_low_watermark1_message_severity_level() const {
        return Sandesh::StringToLevel(
                low_watermark1_message_severity_level_); }
    SandeshLevel::type get_high_watermark2_message_severity_level() const {
        return Sandesh::StringToLevel(
                high_watermark2_message_severity_level_); }
    SandeshLevel::type get_low_watermark2_message_severity_level() const {
        return Sandesh::StringToLevel(
                low_watermark2_message_severity_level_); }

    uint32_t disk_usage_percentage_high_watermark0_;
    uint32_t disk_usage_percentage_low_watermark0_;
    uint32_t disk_usage_percentage_high_watermark1_;
    uint32_t disk_usage_percentage_low_watermark1_;
    uint32_t disk_usage_percentage_high_watermark2_;
    uint32_t disk_usage_percentage_low_watermark2_;
    uint32_t pending_compaction_tasks_high_watermark0_;
    uint32_t pending_compaction_tasks_low_watermark0_;
    uint32_t pending_compaction_tasks_high_watermark1_;
    uint32_t pending_compaction_tasks_low_watermark1_;
    uint32_t pending_compaction_tasks_high_watermark2_;
    uint32_t pending_compaction_tasks_low_watermark2_;
    std::string high_watermark0_message_severity_level_;
    std::string low_watermark0_message_severity_level_;
    std::string high_watermark1_message_severity_level_;
    std::string low_watermark1_message_severity_level_;
    std::string high_watermark2_message_severity_level_;
    std::string low_watermark2_message_severity_level_;
};

// Process command line/configuration file options for collector.
class Options {
public:
    struct Cassandra {
        Cassandra() :
            user_(),
            password_(),
            compaction_strategy_(),
            flow_tables_compaction_strategy_(),
            disable_all_db_writes_(false),
            disable_db_stats_writes_(false),
            disable_db_messages_writes_(false)
        {
        }

        std::string cluster_id_;
        vector<string> cassandra_ips_;
        vector<int> cassandra_ports_;
        TtlMap ttlmap_;
        std::string user_;
        std::string password_;
        std::string compaction_strategy_;
        std::string flow_tables_compaction_strategy_;
        bool disable_all_db_writes_;
        bool disable_db_stats_writes_;
        bool disable_db_messages_writes_;
    };

    Options();
    bool Parse(EventManager &evm, int argc, char **argv);
    void ParseReConfig();

    const Cassandra get_cassandra_options() const {
        return cassandra_options_;
    }

    void add_cassandra_ip(std::string cassandra_ip) {
        cassandra_options_.cassandra_ips_.push_back(cassandra_ip);
    }
    void add_cassandra_port(int cassandra_port) {
        cassandra_options_.cassandra_ports_.push_back(cassandra_port);
    }

    void set_ttl_map(TtlMap &ttlmap) {
        cassandra_options_.ttlmap_ = ttlmap;
    }

    const TtlMap get_ttl_map() const {
        return cassandra_options_.ttlmap_;
    }

    const std::vector<std::string> cassandra_server_list() const {
        return cassandra_server_list_;
    }
    const std::string zookeeper_server_list() const {
        return zookeeper_server_list_;
    }
    const std::vector<std::string> uve_proxy_list() const {
        return uve_proxy_list_;
    }
    const std::vector<std::string> kafka_broker_list() const {
        return kafka_broker_list_;
    }
    const std::vector<std::string> collector_structured_syslog_tcp_forward_destination() const {
        return collector_structured_syslog_tcp_forward_destination_;
    }
    const std::vector<std::string> collector_structured_syslog_kafka_broker_list() const {
        return collector_structured_syslog_kafka_broker_list_;
    }
    const std::vector<std::string> grok_key_list() const {
        return grok_key_list_;
    }
    const std::vector<std::string> grok_attrib_list() const {
        return grok_attrib_list_;
    }
    const std::string collector_structured_syslog_kafka_topic() const {
        return collector_structured_syslog_kafka_topic_;
    }
    const uint16_t collector_structured_syslog_kafka_partitions() const {
        return collector_structured_syslog_kafka_partitions_;
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
    bool collector_structured_syslog_port(uint16_t *collector_structured_syslog_port) const {
        if (collector_structured_syslog_port_configured_) {
            *collector_structured_syslog_port = collector_structured_syslog_port_;
        }
        return collector_structured_syslog_port_configured_;
    }
    const std::vector<std::string> config_file() const {
        return config_file_;
    }
    const DbWriteOptions get_db_write_options() const {
        return db_write_options_;
    }
    const std::string redis_server() const { return redis_server_; }
    const uint16_t redis_port() const { return redis_port_; }
    const std::string redis_password() const { return redis_password_; }
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
    const bool use_grok() const { return use_grok_; }
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
    const bool disable_flow_collection() const { return disable_flow_collection_; }
    const bool disable_all_db_writes() const { return cassandra_options_.disable_all_db_writes_; }
    const bool disable_db_statistics_writes() const { return cassandra_options_.disable_db_stats_writes_; }
    const bool disable_db_messages_writes() const { return cassandra_options_.disable_db_messages_writes_; }
    const std::string cluster_id() const { return cassandra_options_.cluster_id_; }
    const std::string auth_host() const { return ks_server_; }
    const uint16_t auth_port() const { return ks_port_; }
    const std::string auth_protocol() const { return ks_protocol_; }
    const std::string auth_user() const { return ks_user_; }
    const std::string auth_passwd() const { return ks_password_; }
    const std::string auth_tenant() const { return ks_tenant_; }
    const std::string keystone_keyfile() const { return ks_key_; }
    const std::string keystone_certfile() const { return ks_cert_; }
    const std::string keystone_cafile() const { return ks_ca_; }
    const SandeshConfig &sandesh_config() const { return sandesh_config_; }
    const int api_server_checksum() const { return api_server_checksum_; }
    const std::vector<std::string> api_server_list() const {
        return api_server_list_;
    }
    const bool api_server_use_ssl() const { return api_server_use_ssl_; }

    void set_config_client_manager(boost::shared_ptr<ConfigClientManager> mgr) {
        config_client_manager_ = mgr;
    }

    const ConfigClientOptions &configdb_options() const {
        return configdb_options_;
    }

private:
    void Process(int argc, char *argv[],
            boost::program_options::options_description &cmdline_options);
    void Initialize(EventManager &evm,
                    boost::program_options::options_description &options);
    uint32_t GenerateHash(const std::vector<std::string> &);
    uint32_t GenerateHash(const ConfigClientOptions &config);
    void ParseConfigOptions(const boost::program_options::variables_map
                            &var_map);

    std::string collector_server_;
    uint16_t collector_port_;
    uint16_t collector_protobuf_port_;
    bool collector_protobuf_port_configured_;
    uint16_t collector_structured_syslog_port_;
    bool collector_structured_syslog_port_configured_;
    std::vector<std::string> collector_structured_syslog_tcp_forward_destination_;
    std::vector<std::string> collector_structured_syslog_kafka_broker_list_;
    std::string collector_structured_syslog_kafka_topic_;
    uint16_t collector_structured_syslog_kafka_partitions_;
    std::vector<std::string> config_file_;
    std::string redis_server_;
    uint16_t redis_port_;
    std::string redis_password_;
    Cassandra cassandra_options_;
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
    bool use_grok_;
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
    std::string zookeeper_server_list_;
    std::vector<std::string> uve_proxy_list_;
    std::vector<std::string> kafka_broker_list_;
    uint16_t partitions_;
    uint32_t sandesh_ratelimit_;
    bool disable_flow_collection_;
    std::string ks_server_;
    uint16_t    ks_port_;
    std::string ks_protocol_;
    std::string ks_user_;
    std::string ks_password_;
    std::string ks_tenant_;
    std::string ks_authurl_;
    bool        ks_insecure_;
    std::string memcache_servers_;
    std::string ks_cert_;
    std::string ks_key_;
    std::string ks_ca_;
    SandeshConfig sandesh_config_;
    uint32_t api_server_checksum_;
    std::vector<std::string> api_server_list_;
    bool api_server_use_ssl_;
    std::vector<std::string> grok_key_list_;
    std::vector<std::string> grok_attrib_list_;

    boost::program_options::options_description config_file_options_;
    DbWriteOptions db_write_options_;

    uint32_t configdb_chksum_;
    boost::shared_ptr<ConfigClientManager> config_client_manager_;
    ConfigClientOptions configdb_options_;
};

#endif /* ANALYTICS_OPTIONS_H_ */
