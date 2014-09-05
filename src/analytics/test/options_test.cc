/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include "analytics/viz_constants.h"
#include "base/contrail_ports.h"
#include "base/test/task_test_util.h"
#include "base/util.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "analytics/options.h"
#include "io/event_manager.h"

using namespace std;
using namespace boost::asio::ip;

static uint16_t default_redis_port = ContrailPorts::RedisUvePort();
static uint16_t default_collector_port = ContrailPorts::CollectorPort();
static uint16_t default_http_server_port = ContrailPorts::HttpPortCollector();
static uint16_t default_discovery_port = ContrailPorts::DiscoveryServerPort();

class OptionsTest : public ::testing::Test {
protected:
    OptionsTest() { }

    virtual void SetUp() {
        boost::system::error_code error;
        hostname_ = host_name(error);
        host_ip_ = GetHostIp(evm_.io_service(), hostname_);
        default_cassandra_server_list_.push_back("127.0.0.1:9160");
    }

    virtual void TearDown() {
        remove("./options_test_collector_config_file.conf");
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

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(), "/etc/contrail/contrail-collector.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
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
    EXPECT_EQ(options_.analytics_data_ttl(), ANALYTICS_DATA_TTL_DEFAULT);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), false);
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
}

TEST_F(OptionsTest, DefaultConfFile) {
    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/contrail-collector.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/analytics/contrail-collector.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
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
    EXPECT_EQ(options_.analytics_data_ttl(), ANALYTICS_DATA_TTL_DEFAULT);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), false);
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
}

TEST_F(OptionsTest, OverrideStringFromCommandLine) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/contrail-collector.conf";
    char argv_2[] = "--DEFAULT.log_file=test.log";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/analytics/contrail-collector.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "test.log"); // Overridden from cmd line.
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), false);
    EXPECT_EQ(options_.analytics_data_ttl(), ANALYTICS_DATA_TTL_DEFAULT);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), false);
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
}

TEST_F(OptionsTest, OverrideBooleanFromCommandLine) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/contrail-collector.conf";
    char argv_2[] = "--DEFAULT.test_mode";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.collector_server(), "0.0.0.0");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/analytics/contrail-collector.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
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
    EXPECT_EQ(options_.analytics_data_ttl(), ANALYTICS_DATA_TTL_DEFAULT);
    EXPECT_EQ(options_.syslog_port(), -1);
    EXPECT_EQ(options_.dup(), false);
    EXPECT_EQ(options_.test_mode(), true); // Overridden from command line.
    uint16_t protobuf_port(0);
    EXPECT_FALSE(options_.collector_protobuf_port(&protobuf_port));
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
        "\n"
        "[COLLECTOR]\n"
        "port=100\n"
        "server=3.4.5.6\n"
        "protobuf_port=3333\n"
        "\n"
        "[DISCOVERY]\n"
        "port=100\n"
        "server=1.0.0.1 # discovery_server IP address\n"
        "[REDIS]\n"
        "server=1.2.3.4\n"
        "port=200\n"
        "\n"
    ;

    ofstream config_file;
    config_file.open("./options_test_collector_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_collector_config_file.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("10.10.10.1:100");
    cassandra_server_list.push_back("20.20.20.2:200");
    cassandra_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.cassandra_server_list(),
                     cassandra_server_list);

    EXPECT_EQ(options_.redis_server(), "1.2.3.4");
    EXPECT_EQ(options_.redis_port(), 200);
    EXPECT_EQ(options_.collector_server(), "3.4.5.6");
    EXPECT_EQ(options_.collector_port(), 100);
    EXPECT_EQ(options_.config_file(),
              "./options_test_collector_config_file.conf");
    EXPECT_EQ(options_.discovery_server(), "1.0.0.1");
    EXPECT_EQ(options_.discovery_port(), 100);
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
    EXPECT_EQ(options_.analytics_data_ttl(), ANALYTICS_DATA_TTL_DEFAULT);
    EXPECT_EQ(options_.syslog_port(), 101);
    EXPECT_EQ(options_.dup(), true);
    EXPECT_EQ(options_.test_mode(), true);
    uint16_t protobuf_port(0);
    EXPECT_TRUE(options_.collector_protobuf_port(&protobuf_port));
    EXPECT_EQ(protobuf_port, 3333);
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
        "\n"
        "[COLLECTOR]\n"
        "port=100\n"
        "server=3.4.5.6\n"
        "protobuf_port=3333\n"
        "\n"
        "[DISCOVERY]\n"
        "port=100\n"
        "server=1.0.0.1 # discovery_server IP address\n"
        "[REDIS]\n"
        "server=1.2.3.4\n"
        "port=200\n"
        "\n"
    ;

    ofstream config_file;
    config_file.open("./options_test_collector_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 9;
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
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;
    argv[5] = argv_5;
    argv[6] = argv_6;
    argv[7] = argv_7;
    argv[8] = argv_8;

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
    EXPECT_EQ(options_.config_file(),
              "./options_test_collector_config_file.conf");
    EXPECT_EQ(options_.discovery_server(), "1.0.0.1");
    EXPECT_EQ(options_.discovery_port(), 100);
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
    EXPECT_EQ(options_.syslog_port(), 102);
    EXPECT_EQ(options_.dup(), true);
    EXPECT_EQ(options_.test_mode(), true);
    uint16_t protobuf_port(0);
    EXPECT_TRUE(options_.collector_protobuf_port(&protobuf_port));
    EXPECT_EQ(protobuf_port, 3334);
}

TEST_F(OptionsTest, MultitokenVector) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--DEFAULT.cassandra_server_list=10.10.10.1:100 20.20.20.2:200";
    char argv_2[] = "--DEFAULT.cassandra_server_list=30.30.30.3:300";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("10.10.10.1:100");
    cassandra_server_list.push_back("20.20.20.2:200");
    cassandra_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.cassandra_server_list(),
                     cassandra_server_list);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
