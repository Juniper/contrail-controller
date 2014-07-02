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
#include "query_engine/options.h"
#include "io/event_manager.h"

using namespace std;
using namespace boost::asio::ip;

static uint16_t default_redis_port = ContrailPorts::RedisQueryPort;
static uint16_t default_collector_port = ContrailPorts::CollectorPort;
static uint16_t default_http_server_port = ContrailPorts::HttpPortQueryEngine;
static uint16_t default_discovery_port = ContrailPorts::DiscoveryServerPort;

class OptionsTest : public ::testing::Test {
protected:
    OptionsTest() { }

    virtual void SetUp() {
        boost::system::error_code error;
        hostname_ = host_name(error);
        host_ip_ = GetHostIp(evm_.io_service(), hostname_);
        default_cassandra_server_list_.push_back("127.0.0.1:9160");
        default_collector_server_list_.push_back("127.0.0.1:8086");
    }

    virtual void TearDown() {
        remove("./options_test_query_engine.conf");
    }

    EventManager evm_;
    std::string hostname_;
    std::string host_ip_;
    vector<string> default_cassandra_server_list_;
    vector<string> default_collector_server_list_;
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
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.config_file(), "/etc/contrail/query-engine.conf");
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
    EXPECT_EQ(options_.start_time(), 0);
    EXPECT_EQ(options_.max_tasks(), 16);
    EXPECT_EQ(options_.max_slice(), 100);
    EXPECT_EQ(options_.test_mode(), false);
}

TEST_F(OptionsTest, DefaultConfFile) {
    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/query-engine.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/analytics/query-engine.conf");
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
    EXPECT_EQ(options_.start_time(), 0);
    EXPECT_EQ(options_.max_tasks(), 16);
    EXPECT_EQ(options_.max_slice(), 100);
    EXPECT_EQ(options_.test_mode(), false);
}

TEST_F(OptionsTest, OverrideStringFromCommandLine) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/query-engine.conf";
    char argv_2[] = "--DEFAULT.log_file=test.log";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/analytics/query-engine.conf");
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
    EXPECT_EQ(options_.start_time(), 0);
    EXPECT_EQ(options_.max_tasks(), 16);
    EXPECT_EQ(options_.max_slice(), 100);
    EXPECT_EQ(options_.test_mode(), false);
}

TEST_F(OptionsTest, OverrideBooleanFromCommandLine) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/analytics/query-engine.conf";
    char argv_2[] = "--DEFAULT.test_mode";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_cassandra_server_list_,
                     options_.cassandra_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.redis_server(), "127.0.0.1");
    EXPECT_EQ(options_.redis_port(), default_redis_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/analytics/query-engine.conf");
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
    EXPECT_EQ(options_.start_time(), 0);
    EXPECT_EQ(options_.max_tasks(), 16);
    EXPECT_EQ(options_.max_slice(), 100);
    EXPECT_EQ(options_.test_mode(), true); // Overridden from command line.
}

TEST_F(OptionsTest, CustomConfigFile) {
    string config = ""
        "[DEFAULT]\n"
        "cassandra_server_list=10.10.10.1:100\n"
        "cassandra_server_list=20.20.20.2:200\n"
        "cassandra_server_list=30.30.30.3:300\n"
        "collectors=10.10.10.1:100\n"
        "collectors=20.20.20.2:200\n"
        "collectors=30.30.30.3:300\n"
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
        "start_time=123456\n"
        "max_tasks=200\n"
        "max_slice=500\n"
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
    config_file.open("./options_test_query_engine.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_query_engine.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("10.10.10.1:100");
    cassandra_server_list.push_back("20.20.20.2:200");
    cassandra_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(cassandra_server_list,
                     options_.cassandra_server_list());

    vector<string> collector_server_list;
    collector_server_list.push_back("10.10.10.1:100");
    collector_server_list.push_back("20.20.20.2:200");
    collector_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(collector_server_list,
                     options_.collector_server_list());

    EXPECT_EQ(options_.redis_server(), "1.2.3.4");
    EXPECT_EQ(options_.redis_port(), 200);
    EXPECT_EQ(options_.config_file(),
              "./options_test_query_engine.conf");
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
    EXPECT_EQ(options_.start_time(), 123456);
    EXPECT_EQ(options_.max_tasks(), 200);
    EXPECT_EQ(options_.max_slice(), 500);
    EXPECT_EQ(options_.test_mode(), true);
}

TEST_F(OptionsTest, CustomConfigFileAndOverrideFromCommandLine) {
    string config = ""
        "[DEFAULT]\n"
        "analytics_data_ttl=30\n"
        "cassandra_server_list=10.10.10.1:100\n"
        "cassandra_server_list=20.20.20.2:200\n"
        "cassandra_server_list=30.30.30.3:300\n"
        "collectors=10.10.10.1:100\n"
        "collectors=20.20.20.2:200\n"
        "collectors=30.30.30.3:300\n"
        "hostip=1.2.3.4\n"
        "hostname=test\n"
        "http_server_port=800\n"
        "log_category=bgp\n"
        "log_disable=0\n"
        "log_file=test.log\n"
        "log_files_count=20\n"
        "log_file_size=1024\n"
        "log_level=SYS_DEBUG\n"
        "log_local=0\n"
        "test_mode=1\n"
        "start_time=543457\n"
        "max_tasks=900\n"
        "max_slice=800\n"
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
    config_file.open("./options_test_query_engine.conf");
    config_file << config;
    config_file.close();

    int argc = 15;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_query_engine.conf";
    char argv_2[] = "--DEFAULT.log_file=new_test.log";
    char argv_3[] = "--DEFAULT.log_local";
    char argv_4[] = "--DEFAULT.log_disable";
    char argv_5[] = "--DEFAULT.cassandra_server_list=11.10.10.1:100";
    char argv_6[] = "--DEFAULT.cassandra_server_list=21.20.20.2:200";
    char argv_7[] = "--DEFAULT.cassandra_server_list=31.30.30.3:300";
    char argv_8[] = "--DEFAULT.start_time=543457";
    char argv_9[] = "--DEFAULT.max_slice=800";
    char argv_10[] = "--DEFAULT.max_tasks=900";
    char argv_11[] = "--DEFAULT.collectors=11.10.10.1:100";
    char argv_12[] = "--DEFAULT.collectors=21.20.20.2:200";
    char argv_13[] = "--DEFAULT.collectors=31.30.30.3:300";
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
    argv[14] = argv_13;

    options_.Parse(evm_, argc, argv);

    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("11.10.10.1:100");
    cassandra_server_list.push_back("21.20.20.2:200");
    cassandra_server_list.push_back("31.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(cassandra_server_list,
                     options_.cassandra_server_list());

    vector<string> collector_server_list;
    collector_server_list.push_back("11.10.10.1:100");
    collector_server_list.push_back("21.20.20.2:200");
    collector_server_list.push_back("31.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(collector_server_list,
                     options_.collector_server_list());

    EXPECT_EQ(options_.redis_server(), "1.2.3.4");
    EXPECT_EQ(options_.redis_port(), 200);
    EXPECT_EQ(options_.config_file(),
              "./options_test_query_engine.conf");
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
    EXPECT_EQ(options_.start_time(), 543457);
    EXPECT_EQ(options_.max_tasks(), 900);
    EXPECT_EQ(options_.max_slice(), 800);
    EXPECT_EQ(options_.test_mode(), true);
}

TEST_F(OptionsTest, MultitokenVector) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--DEFAULT.collectors=10.10.10.1:100 20.20.20.2:200";
    char argv_2[] = "--DEFAULT.cassandra_server_list=30.30.30.3:300";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    vector<string> collector_server_list;
    collector_server_list.push_back("10.10.10.1:100");
    collector_server_list.push_back("20.20.20.2:200");
    vector<string> cassandra_server_list;
    cassandra_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.collector_server_list(),
                     collector_server_list);
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.cassandra_server_list(),
                     cassandra_server_list);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
