/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include <boost/asio/ip/host_name.hpp>

#include "analytics/viz_constants.h"
#include "base/contrail_ports.h"
#include "base/test/task_test_util.h"
#include "base/util.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "analytics/options.h"
#include "io/event_manager.h"
#include "net/address_util.h"

using namespace std;
using namespace boost::asio::ip;

static uint16_t default_redis_port = ContrailPorts::RedisUvePort();
static uint16_t default_collector_port = ContrailPorts::CollectorPort();
static uint16_t default_http_server_port = ContrailPorts::HttpPortCollector();

class OptionsTest : public ::testing::Test {
protected:
    OptionsTest() { }

    virtual void SetUp() {
        boost::system::error_code error;
        hostname_ = host_name(error);
        host_ip_ = GetHostIp(evm_.io_service(), hostname_);
        default_cassandra_server_list_.push_back("127.0.0.1:9042");
    }

    virtual void TearDown() {
        remove("./options_test_collector_config_file.conf");
        remove ("./options_test_cassandra_config_file.conf");
    }

    EventManager evm_;
    std::string hostname_;
    std::string host_ip_;
    vector<string> default_cassandra_server_list_;
    Options options_;
};

TEST_F(OptionsTest, NoArguments) {
    int argc = 1;
    char *argv[argc];
    char argv_0[] = "options_test";
    argv[0] = argv_0;

    options_.Parse(evm_, argc, argv);
    map<string, vector<string> >::const_iterator it =
        g_vns_constants.ServicesDefaultConfigurationFiles.find(
            g_vns_constants.SERVICE_COLLECTOR);
    assert(it != g_vns_constants.ServicesDefaultConfigurationFiles.end());
    const vector<string> &expected_conf_files_(it->second);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(expected_conf_files_,
                     options_.config_file());
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "<stdout>");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), false);
    EXPECT_EQ(options_.analytics_data_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::GLOBAL_TTL)->second);
    EXPECT_EQ(options_.analytics_config_audit_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second);
    EXPECT_EQ(options_.analytics_statistics_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second);
    EXPECT_EQ(options_.analytics_flow_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.sandesh_send_rate_limit(),
        g_sandesh_constants.DEFAULT_SANDESH_SEND_RATELIMIT);
    EXPECT_EQ(options_.disable_flow_collection(), false);
    EXPECT_EQ(options_.disable_db_messages_writes(), false);
    EXPECT_EQ(options_.enable_db_messages_keyword_writes(), false);
    EXPECT_EQ(options_.disable_db_statistics_writes(), false);
    EXPECT_EQ(options_.disable_all_db_writes(), false);
    EXPECT_EQ(options_.cluster_id(), "");
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
    uint16_t structured_syslog_port(0);
    EXPECT_FALSE(options_.collector_structured_syslog_port(&structured_syslog_port));
}

TEST_F(OptionsTest, DefaultConfFile) {
    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/contrail-collector.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);
    vector<string> passed_conf_files;
    passed_conf_files.push_back("controller/src/analytics/contrail-collector.conf");

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.config_file(),
                               passed_conf_files);
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "/var/log/contrail/contrail-collector.log");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.analytics_data_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::GLOBAL_TTL)->second);
    EXPECT_EQ(options_.analytics_config_audit_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second);
    EXPECT_EQ(options_.analytics_statistics_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second);
    EXPECT_EQ(options_.analytics_flow_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.disable_flow_collection(), false);
    EXPECT_EQ(options_.disable_db_messages_writes(), false);
    EXPECT_EQ(options_.enable_db_messages_keyword_writes(), false);
    EXPECT_EQ(options_.disable_db_statistics_writes(), false);
    EXPECT_EQ(options_.disable_all_db_writes(), false);
    EXPECT_EQ(options_.cluster_id(), "");
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
    uint16_t structured_syslog_port(0);
    EXPECT_FALSE(options_.collector_structured_syslog_port(&structured_syslog_port));
}

TEST_F(OptionsTest, OverrideStringFromCommandLine) {
    int argc = 4;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/contrail-collector.conf";
    char argv_2[] = "--DEFAULT.log_file=test.log";
    char argv_3[] = "--DEFAULT.sandesh_send_rate_limit=5";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;

    options_.Parse(evm_, argc, argv);
    vector<string> passed_conf_files;
    passed_conf_files.push_back("controller/src/analytics/contrail-collector.conf");

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.config_file(),
                               passed_conf_files);
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "test.log"); // Overridden from cmd line.
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.analytics_data_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::GLOBAL_TTL)->second);
    EXPECT_EQ(options_.analytics_config_audit_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second);
    EXPECT_EQ(options_.analytics_statistics_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second);
    EXPECT_EQ(options_.analytics_flow_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.sandesh_send_rate_limit(), 5);
    EXPECT_EQ(options_.disable_flow_collection(), false);
    EXPECT_EQ(options_.cluster_id(), "");
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
    uint16_t structured_syslog_port(0);
    EXPECT_FALSE(options_.collector_structured_syslog_port(&structured_syslog_port));
}

TEST_F(OptionsTest, OverrideBooleanFromCommandLine) {
    int argc = 8;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/contrail-collector.conf";
    char argv_2[] = "--DEFAULT.test_mode";
    char argv_3[] = "--DEFAULT.disable_flow_collection";
    char argv_4[] = "--DATABASE.disable_all_writes";
    char argv_5[] = "--DATABASE.enable_message_keyword_writes";
    char argv_6[] = "--DATABASE.cluster_id";
    char argv_7[] = "C1";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;
    argv[5] = argv_5;
    argv[6] = argv_6;
    argv[7] = argv_7;

    options_.Parse(evm_, argc, argv);
    vector<string> passed_conf_files;
    passed_conf_files.push_back("controller/src/analytics/contrail-collector.conf");

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.config_file(),
                               passed_conf_files);
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "/var/log/contrail/contrail-collector.log");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.analytics_data_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::GLOBAL_TTL)->second);
    EXPECT_EQ(options_.analytics_config_audit_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second);
    EXPECT_EQ(options_.analytics_statistics_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second);
    EXPECT_EQ(options_.analytics_flow_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), true); // Overridden from command line.
    EXPECT_EQ(options_.disable_flow_collection(), true); // Overridden from command line.
    EXPECT_EQ(options_.disable_all_db_writes(), true); // Overridden from command line.
    EXPECT_EQ(options_.enable_db_messages_keyword_writes(), true); // Overriden from command line.
    EXPECT_EQ(options_.cluster_id(), "C1");
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
    uint16_t structured_syslog_port(0);
    EXPECT_FALSE(options_.collector_structured_syslog_port(&structured_syslog_port));
}

TEST_F(OptionsTest, CustomConfigFile) {
    string config = ""
        "[DEFAULT]\n"
        "cassandra_server_list=10.10.10.1:100\n"
        "cassandra_server_list=20.20.20.2:200\n"
        "cassandra_server_list=30.30.30.3:300\n"
        "dup=1\n"
        "hostip=1.2.3.4\n"
        "hostname=test\n"
        "http_server_port=800\n"
        "log_category=bgp\n"
        "log_disable=1\n"
        "log_file=test.log\n"
        "log_files_count=20\n"
        "log_file_size=1024\n"
        "log_level=SYS_DEBUG\n"
        "log_local=1\n"
        "test_mode=1\n"
        "syslog_port=101\n"
        "sandesh_send_rate_limit=5\n"
        "disable_flow_collection=1\n"
        "\n"
        "[COLLECTOR]\n"
        "port=100\n"
        "server=3.4.5.6\n"
        "protobuf_port=3333\n"
        "structured_syslog_port=3514\n"
        "\n"
        "[REDIS]\n"
        "server=1.2.3.4\n"
        "port=200\n"
        "\n"
    ;

    ofstream config_file;
    config_file.open("./options_test_collector_config_file.conf");
    config_file << config;
    config_file.close();

    string cassandra_config = ""
        "[CASSANDRA]\n"
        "cassandra_user=cassandra1\n"
        "cassandra_password=cassandra1\n"
        "compaction_strategy=LeveledCompactionStrategy\n"
        "flow_tables.compaction_strategy=SizeTieredCompactionStrategy\n";

    config_file.open("./options_test_cassandra_config_file.conf");
    config_file << cassandra_config;
    config_file.close();

    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_collector_config_file.conf";
    char argv_2[] = "--conf_file=./options_test_cassandra_config_file.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("10.10.10.1:100");
    cassandra_server_list.push_back("20.20.20.2:200");
    cassandra_server_list.push_back("30.30.30.3:300");

    vector<string> input_conf_files;
    input_conf_files.push_back("./options_test_collector_config_file.conf");
    input_conf_files.push_back("./options_test_cassandra_config_file.conf");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.cassandra_server_list(),
                     cassandra_server_list);

    EXPECT_EQ(options_.redis_server(), "1.2.3.4");
    EXPECT_EQ(options_.redis_port(), 200);
    EXPECT_EQ(options_.collector_server(), "3.4.5.6");
    EXPECT_EQ(options_.collector_port(), 100);

    TASK_UTIL_EXPECT_VECTOR_EQ(options_.config_file(),
                               input_conf_files);
    EXPECT_EQ(options_.hostname(), "test");
    EXPECT_EQ(options_.host_ip(), "1.2.3.4");
    EXPECT_EQ(options_.http_server_port(), 800);
    EXPECT_EQ(options_.log_category(), "bgp");
    EXPECT_EQ(options_.log_disable(), true);
    EXPECT_EQ(options_.log_file(), "test.log");
    EXPECT_EQ(options_.log_files_count(), 20);
    EXPECT_EQ(options_.log_file_size(), 1024);
    EXPECT_EQ(options_.log_level(), "SYS_DEBUG");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.analytics_data_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::GLOBAL_TTL)->second);
    EXPECT_EQ(options_.analytics_config_audit_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second);
    EXPECT_EQ(options_.analytics_statistics_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second);
    EXPECT_EQ(options_.analytics_flow_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second);
    EXPECT_EQ(options_.syslog_port(), 101);
    EXPECT_EQ(options_.dup(), true);
    EXPECT_EQ(options_.test_mode(), true);
    EXPECT_EQ(options_.disable_flow_collection(), true);
    uint16_t protobuf_port(0);
    EXPECT_TRUE(options_.collector_protobuf_port(&protobuf_port));
    EXPECT_EQ(protobuf_port, 3333);
    uint16_t structured_syslog_port(0);
    EXPECT_TRUE(options_.collector_structured_syslog_port(&structured_syslog_port));
    EXPECT_EQ(structured_syslog_port, 3514);
    Options::Cassandra cassandra_options(options_.get_cassandra_options());
    EXPECT_EQ(cassandra_options.user_, "cassandra1");
    EXPECT_EQ(cassandra_options.password_, "cassandra1");
    EXPECT_EQ(cassandra_options.compaction_strategy_,
        "LeveledCompactionStrategy");
    EXPECT_EQ(cassandra_options.flow_tables_compaction_strategy_,
        "SizeTieredCompactionStrategy");
    EXPECT_EQ(options_.cluster_id(), "");
    EXPECT_EQ(options_.sandesh_send_rate_limit(), 5);
}

TEST_F(OptionsTest, CustomConfigFileAndOverrideFromCommandLine) {
    string config = ""
        "[DEFAULT]\n"
        "analytics_data_ttl=30\n"
        "cassandra_server_list=10.10.10.1:100\n"
        "cassandra_server_list=20.20.20.2:200\n"
        "cassandra_server_list=30.30.30.3:300\n"
        "dup=1\n"
        "hostip=1.2.3.4\n"
        "hostname=test\n"
        "http_server_port=800\n"
        "log_category=bgp\n"
        "log_disable=1\n"
        "log_file=test.log\n"
        "log_files_count=20\n"
        "log_file_size=1024\n"
        "log_level=SYS_DEBUG\n"
        "log_local=0\n"
        "test_mode=1\n"
        "syslog_port=102\n"
        "sandesh_send_rate_limit=5\n"
        "\n"
        "[COLLECTOR]\n"
        "port=100\n"
        "server=3.4.5.6\n"
        "protobuf_port=3333\n"
        "structured_syslog_port=3514\n"
        "\n"
        "[REDIS]\n"
        "server=1.2.3.4\n"
        "port=200\n"
        "\n"
    ;

    int argc = 14;

    ofstream config_file;
    config_file.open("./options_test_collector_config_file.conf");
    config_file << config;
    config_file.close();

    string cassandra_config = ""
        "[CASSANDRA]\n"
        "cassandra_user=cassandra1\n"
        "cassandra_password=cassandra1\n";

    config_file.open("./options_test_cassandra_config_file.conf");
    config_file << cassandra_config;
    config_file.close();


    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_collector_config_file.conf";
    char argv_2[] = "--DEFAULT.log_file=new_test.log";
    char argv_3[] = "--DEFAULT.log_local";
    char argv_4[] = "--COLLECTOR.port=1000";
    char argv_5[] = "--DEFAULT.cassandra_server_list=11.10.10.1:100";
    char argv_6[] = "--DEFAULT.cassandra_server_list=21.20.20.2:200";
    char argv_7[] = "--DEFAULT.cassandra_server_list=31.30.30.3:300";
    char argv_8[] = "--COLLECTOR.protobuf_port=3334";
    char argv_9[] = "--CASSANDRA.cassandra_user=cassandra";
    char argv_10[] = "--CASSANDRA.cassandra_password=cassandra";
    char argv_11[] = "--conf_file=./options_test_cassandra_config_file.conf";
    char argv_12[] = "--DEFAULT.sandesh_send_rate_limit=7";
    char argv_13[] = "--COLLECTOR.structured_syslog_port=3515";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;
    argv[5] = argv_5;
    argv[6] = argv_6;
    argv[7] = argv_7;
    argv[8] = argv_8;
    argv[9] = argv_9;
    argv[10] = argv_10;
    argv[11] = argv_11;
    argv[12] = argv_12;
    argv[13] = argv_13;

    options_.Parse(evm_, argc, argv);

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("11.10.10.1:100");
    cassandra_server_list.push_back("21.20.20.2:200");
    cassandra_server_list.push_back("31.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.cassandra_server_list(),
                     cassandra_server_list);
    EXPECT_EQ(options_.redis_server(), "1.2.3.4");
    EXPECT_EQ(options_.redis_port(), 200);
    EXPECT_EQ(options_.collector_server(), "3.4.5.6");
    EXPECT_EQ(options_.collector_port(), 1000);

    vector<string> input_conf_files;
    input_conf_files.push_back("./options_test_collector_config_file.conf");
    input_conf_files.push_back("./options_test_cassandra_config_file.conf");

    TASK_UTIL_EXPECT_VECTOR_EQ(options_.config_file(),
                               input_conf_files);
    Options::Cassandra cassandra_options(options_.get_cassandra_options());
    EXPECT_EQ(cassandra_options.user_,"cassandra");
    EXPECT_EQ(cassandra_options.password_,"cassandra");
    EXPECT_EQ(options_.hostname(), "test");
    EXPECT_EQ(options_.host_ip(), "1.2.3.4");
    EXPECT_EQ(options_.http_server_port(), 800);
    EXPECT_EQ(options_.log_category(), "bgp");
    EXPECT_EQ(options_.log_disable(), true);
    EXPECT_EQ(options_.log_file(), "new_test.log");
    EXPECT_EQ(options_.log_files_count(), 20);
    EXPECT_EQ(options_.log_file_size(), 1024);
    EXPECT_EQ(options_.log_level(), "SYS_DEBUG");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.analytics_data_ttl(), 30);
    EXPECT_EQ(options_.analytics_config_audit_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::CONFIGAUDIT_TTL)->second);
    EXPECT_EQ(options_.analytics_statistics_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::STATSDATA_TTL)->second);
    EXPECT_EQ(options_.analytics_flow_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second);
    EXPECT_EQ(options_.syslog_port(), 102);
    EXPECT_EQ(options_.dup(), true);
    EXPECT_EQ(options_.test_mode(), true);
    uint16_t protobuf_port(0);
    EXPECT_TRUE(options_.collector_protobuf_port(&protobuf_port));
    EXPECT_EQ(protobuf_port, 3334);
    uint16_t structured_syslog_port(0);
    EXPECT_TRUE(options_.collector_structured_syslog_port(&structured_syslog_port));
    EXPECT_EQ(structured_syslog_port, 3515);
    EXPECT_EQ(options_.sandesh_send_rate_limit(), 7);
}

TEST_F(OptionsTest, MultitokenVector) {
    int argc = 5;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--DEFAULT.cassandra_server_list=10.10.10.1:100 20.20.20.2:200";
    char argv_2[] = "--DEFAULT.cassandra_server_list=30.30.30.3:300";
    char argv_3[] = "--conf_file=controller/src/analytics/contrail-collector.conf"
                    " controller/src/analytics/contrail-database.conf";
    char argv_4[] = "--conf_file=controller/src/analytics/test-conf.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;

    options_.Parse(evm_, argc, argv);

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("10.10.10.1:100");
    cassandra_server_list.push_back("20.20.20.2:200");
    cassandra_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.cassandra_server_list(),
                     cassandra_server_list);
    vector<string> option_file_list;
    option_file_list.push_back("controller/src/analytics/contrail-collector.conf");
    option_file_list.push_back("controller/src/analytics/contrail-database.conf");
    option_file_list.push_back("controller/src/analytics/test-conf.conf");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.config_file(),
                     option_file_list);
}

TEST_F(OptionsTest, MultiTtlOption) {
    int argc = 4;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--DEFAULT.analytics_data_ttl=2";
    char argv_2[] = "--DEFAULT.analytics_config_audit_ttl=240";
    char argv_3[] = "--DEFAULT.analytics_statistics_ttl=4";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.analytics_data_ttl(), 2);
    EXPECT_EQ(options_.analytics_config_audit_ttl(), 240);
    EXPECT_EQ(options_.analytics_statistics_ttl(), 4);
    EXPECT_EQ(options_.analytics_flow_ttl(), g_viz_constants.TtlValuesDefault.find(TtlType::FLOWDATA_TTL)->second);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
