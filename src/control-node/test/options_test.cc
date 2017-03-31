/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include <boost/asio/ip/host_name.hpp>

#include "base/contrail_ports.h"
#include "base/test/task_test_util.h"
#include "base/util.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/options.h"
#include "io/event_manager.h"
#include "net/address_util.h"

using namespace std;
using namespace boost::asio::ip;

static uint16_t default_bgp_port = ContrailPorts::ControlBgp();
static uint16_t default_http_server_port = ContrailPorts::HttpPortControl();
static uint16_t default_xmpp_port = ContrailPorts::ControlXmpp();

class OptionsTest : public ::testing::Test {
protected:
    OptionsTest() { }

    virtual void SetUp() {
        boost::system::error_code error;
        hostname_ = host_name(error);
        host_ip_ = GetHostIp(evm_.io_service(), hostname_);
        default_collector_server_list_.push_back("127.0.0.1:8086");
        default_rabbitmq_server_list_.push_back(host_ip_ + ":5672");
        default_configdb_server_list_.push_back(host_ip_ + ":9042");
    }

    virtual void TearDown() {
        remove("./options_test_config_file.conf");
    }

    EventManager evm_;
    std::string hostname_;
    std::string host_ip_;
    std::vector<std::string> default_collector_server_list_;
    std::vector<std::string> default_rabbitmq_server_list_;
    std::vector<std::string> default_configdb_server_list_;
    Options options_;
};

TEST_F(OptionsTest, NoArguments) {
    int argc = 1;
    char *argv[argc];
    char argv_0[] = "options_test";
    argv[0] = argv_0;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "bgp_config.xml");
    EXPECT_EQ(options_.bgp_port(), default_bgp_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.config_file(), "/etc/contrail/contrail-control.conf");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "<stdout>");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 10*1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), false);
    EXPECT_EQ(options_.rabbitmq_user(), "guest");
    EXPECT_EQ(options_.rabbitmq_password(), "guest");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_rabbitmq_server_list_,
                     options_.rabbitmq_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_configdb_server_list_,
                     options_.config_db_server_list());
  
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.sandesh_send_rate_limit(),
              g_sandesh_constants.DEFAULT_SANDESH_SEND_RATELIMIT);
    EXPECT_EQ(options_.gr_helper_bgp_disable(), false);
    EXPECT_EQ(options_.gr_helper_xmpp_disable(), false);
}

TEST_F(OptionsTest, DefaultConfFile) {
    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/contrail-control.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "bgp_config.xml");
    EXPECT_EQ(options_.bgp_port(), default_bgp_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.config_file(),
              "controller/src/control-node/contrail-control.conf");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "/var/log/contrail/contrail-control.log");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 10*1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.gr_helper_bgp_disable(), false);
    EXPECT_EQ(options_.gr_helper_xmpp_disable(), false);
}

TEST_F(OptionsTest, OverrideStringFromCommandLine) {
    int argc = 4;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/contrail-control.conf";
    char argv_2[] = "--DEFAULT.log_file=test.log";
    char argv_3[] = "--DEFAULT.sandesh_send_rate_limit=5";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "bgp_config.xml");
    EXPECT_EQ(options_.bgp_port(), default_bgp_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.config_file(),
              "controller/src/control-node/contrail-control.conf");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "test.log"); // Overridden from cmd line.
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 10*1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.rabbitmq_user(), "guest");
    EXPECT_EQ(options_.rabbitmq_password(), "guest");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_rabbitmq_server_list_,
                     options_.rabbitmq_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_configdb_server_list_,
                     options_.config_db_server_list());
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.sandesh_send_rate_limit(), 5);
    EXPECT_EQ(options_.gr_helper_bgp_disable(), false);
    EXPECT_EQ(options_.gr_helper_xmpp_disable(), false);
}

TEST_F(OptionsTest, OverrideBooleanFromCommandLine) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/contrail-control.conf";
    char argv_2[] = "--DEFAULT.test_mode";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "bgp_config.xml");
    EXPECT_EQ(options_.bgp_port(), default_bgp_port);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.config_file(),
              "controller/src/control-node/contrail-control.conf");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "/var/log/contrail/contrail-control.log");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 10*1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.rabbitmq_user(), "guest");
    EXPECT_EQ(options_.rabbitmq_password(), "guest");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_rabbitmq_server_list_,
                     options_.rabbitmq_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_configdb_server_list_,
                     options_.config_db_server_list());
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), true); // Overridden from command line.
    EXPECT_EQ(options_.gr_helper_bgp_disable(), false);
    EXPECT_EQ(options_.gr_helper_xmpp_disable(), false);
}

TEST_F(OptionsTest, CustomConfigFile) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "bgp_port=200\n"
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
        "log_local=false\n"
        "test_mode=0\n"
        "task_track_run_time=0\n"
        "optimize_snat=1\n"
        "gr_helper_bgp_disable=1\n"
        "gr_helper_xmpp_disable=1\n"
        "xmpp_auth_enable=true\n"
        "xmpp_server_cert=/etc/server.pem\n"
        "xmpp_server_key=/etc/server.key\n"
        "xmpp_ca_cert=/etc/ca-cert.pem\n"
        "xmpp_server_port=100\n"
        "sandesh_send_rate_limit=5\n"
        "\n"
        "\n"
        "[CONFIGDB]\n"
        "rabbitmq_user=test-user\n"
        "rabbitmq_password=test-password\n"
        "\n";

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_config_file.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "test.xml");
    EXPECT_EQ(options_.bgp_port(), 200);

    vector<string> collector_server_list;
    collector_server_list.push_back("10.10.10.1:100");
    collector_server_list.push_back("20.20.20.2:200");
    collector_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(collector_server_list,
                     options_.collector_server_list());
    EXPECT_EQ(options_.config_file(),
              "./options_test_config_file.conf");
    EXPECT_EQ(options_.hostname(), "test");
    EXPECT_EQ(options_.host_ip(), "1.2.3.4");
    EXPECT_EQ(options_.http_server_port(), 800);
    EXPECT_EQ(options_.log_category(), "bgp");
    EXPECT_EQ(options_.log_disable(), true);
    EXPECT_EQ(options_.log_file(), "test.log");
    EXPECT_EQ(options_.log_files_count(), 20);
    EXPECT_EQ(options_.log_file_size(), 1024);
    EXPECT_EQ(options_.log_level(), "SYS_DEBUG");
    EXPECT_EQ(options_.log_local(), false);
    EXPECT_EQ(options_.xmpp_port(), 100);
    EXPECT_EQ(options_.task_track_run_time(), false);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.optimize_snat(), true);
    EXPECT_EQ(options_.gr_helper_bgp_disable(), true);
    EXPECT_EQ(options_.gr_helper_xmpp_disable(), true);
    EXPECT_EQ(options_.xmpp_auth_enabled(), true);
    EXPECT_EQ(options_.xmpp_server_cert(), "/etc/server.pem");
    EXPECT_EQ(options_.xmpp_server_key(), "/etc/server.key");
    EXPECT_EQ(options_.xmpp_ca_cert(), "/etc/ca-cert.pem");
    EXPECT_EQ(options_.sandesh_send_rate_limit(), 5);
    EXPECT_EQ(options_.rabbitmq_user(), "test-user");
    EXPECT_EQ(options_.rabbitmq_password(), "test-password");
}

TEST_F(OptionsTest, CustomConfigFileAndOverrideFromCommandLine) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "bgp_port=200\n"
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
        "log_local=0\n"
        "test_mode=1\n"
        "xmpp_server_port=100\n"
        "sandesh_send_rate_limit=5\n"
        "\n"
        "[CONFIGDB]\n"
        "rabbitmq_user=test-user\n"
        "rabbitmq_password=test-password\n"
        "\n";

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 10;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_config_file.conf";
    char argv_2[] = "--DEFAULT.log_file=new_test.log";
    char argv_3[] = "--DEFAULT.log_local";
    char argv_4[] = "--DEFAULT.collectors=11.10.10.1:100";
    char argv_5[] = "--DEFAULT.collectors=21.20.20.2:200";
    char argv_6[] = "--DEFAULT.collectors=31.30.30.3:300";
    char argv_7[] = "--DEFAULT.sandesh_send_rate_limit=7";
    char argv_8[] = "--DEFAULT.gr_helper_bgp_disable";
    char argv_9[] = "--DEFAULT.gr_helper_xmpp_disable";
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

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "test.xml");
    EXPECT_EQ(options_.bgp_port(), 200);

    vector<string> collector_server_list;
    collector_server_list.push_back("11.10.10.1:100");
    collector_server_list.push_back("21.20.20.2:200");
    collector_server_list.push_back("31.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(collector_server_list,
                     options_.collector_server_list());

    EXPECT_EQ(options_.config_file(),
              "./options_test_config_file.conf");
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

    EXPECT_EQ(options_.rabbitmq_user(), "test-user");
    EXPECT_EQ(options_.rabbitmq_password(), "test-password");
    EXPECT_EQ(options_.xmpp_port(), 100);
    EXPECT_EQ(options_.test_mode(), true);
    EXPECT_EQ(options_.sandesh_send_rate_limit(), 7);
    EXPECT_EQ(options_.gr_helper_bgp_disable(), true);
    EXPECT_EQ(options_.gr_helper_xmpp_disable(), true);
}

TEST_F(OptionsTest, CustomConfigFileWithInvalidHostIp) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "hostip=1000.200.1.1\n" // Invalid ip address string
        ;

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_config_file.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    EXPECT_FALSE(options_.Parse(evm_, argc, argv));
}

TEST_F(OptionsTest, CustomConfigFileWithInvalidHostIpFromCommandLine) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "hostip=1.200.1.1\n"
        ;

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--DEFAULT.hostip=--invalid-value";
    argv[0] = argv_0;
    argv[1] = argv_1;

    EXPECT_FALSE(options_.Parse(evm_, argc, argv));
}

TEST_F(OptionsTest, CustomConfigFileWithInvalidCollectors) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "collectors=foo:2000\n"
        "collectors=300.10.10.1:100\n"
        "collectors=30.30.30.3:300\n"
        ;

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_config_file.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    EXPECT_FALSE(options_.Parse(evm_, argc, argv));
}

TEST_F(OptionsTest, CustomConfigFileWithInvalidCollectorsFromCommandLine) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "collectors=10.10.10.1:100\n"
        "collectors=20.20.20.2:200\n"
        "collectors=30.30.30.3:300\n"
        ;

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--DEFAULT.collectors=--invalid-value";
    argv[0] = argv_0;
    argv[1] = argv_1;

    EXPECT_FALSE(options_.Parse(evm_, argc, argv));
}

TEST_F(OptionsTest, MultitokenVector) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--DEFAULT.collectors=10.10.10.1:100 20.20.20.2:200";
    char argv_2[] = "--DEFAULT.collectors=30.30.30.3:300";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);
    vector<string> collector_server_list;
    collector_server_list.push_back("10.10.10.1:100");
    collector_server_list.push_back("20.20.20.2:200");
    collector_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(options_.collector_server_list(),
                     collector_server_list);
}

TEST_F(OptionsTest, DISABLED_UnresolvableHostName) {
    hostname_ = "uresolvable_host";
    host_ip_ = GetHostIp(evm_.io_service(), hostname_);
    EXPECT_EQ("", host_ip_);
}

TEST_F(OptionsTest, OverrideConfigdbOptionsFromCommandLine) {
    int argc = 7;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/contrail-control.conf";
    char argv_2[] = "--CONFIGDB.rabbitmq_user=myuser";
    char argv_3[] = "--CONFIGDB.rabbitmq_password=mynewpassword";
    char argv_4[] = "--CONFIGDB.rabbitmq_use_ssl=false";
    char argv_5[] = "--CONFIGDB.rabbitmq_server_list=10.1.1.1:100 10.1.1.2:100";
    char argv_6[] = "--CONFIGDB.config_db_server_list=20.1.1.1:100 20.1.1.2:100";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;
    argv[5] = argv_5;
    argv[6] = argv_6;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.rabbitmq_user(), "myuser");
    EXPECT_EQ(options_.rabbitmq_password(), "mynewpassword");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);

    vector<string> rabbitmq_server_list;
    rabbitmq_server_list.push_back("10.1.1.1:100");
    rabbitmq_server_list.push_back("10.1.1.2:100");
    TASK_UTIL_EXPECT_VECTOR_EQ(rabbitmq_server_list,
                     options_.rabbitmq_server_list());
    vector<string> config_db_server_list;
    config_db_server_list.push_back("20.1.1.1:100");
    config_db_server_list.push_back("20.1.1.2:100");
    TASK_UTIL_EXPECT_VECTOR_EQ(config_db_server_list,
                     options_.config_db_server_list());
}

TEST_F(OptionsTest, CustomConfigDBFileAndOverrideFromCommandLine) {
    string config = ""
        "[CONFIGDB]\n"
        "rabbitmq_user=test-user\n"
        "rabbitmq_password=test-password\n"
        "rabbitmq_use_ssl=true\n"
        "rabbitmq_server_list=10.1.1.1:100 10.1.1.2:100\n"
        "config_db_server_list=20.1.1.1:100 20.1.1.2:100\n";

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 7;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_config_file.conf";
    char argv_2[] = "--CONFIGDB.rabbitmq_user=myuser";
    char argv_3[] = "--CONFIGDB.rabbitmq_password=mynewpassword";
    char argv_4[] = "--CONFIGDB.rabbitmq_use_ssl=false";
    char argv_5[] = "--CONFIGDB.rabbitmq_server_list=30.1.1.1:100 30.1.1.2:100";
    char argv_6[] = "--CONFIGDB.config_db_server_list=40.1.1.1:100 40.1.1.2:100";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;
    argv[5] = argv_5;
    argv[6] = argv_6;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.rabbitmq_user(), "myuser");
    EXPECT_EQ(options_.rabbitmq_password(), "mynewpassword");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);

    vector<string> rabbitmq_server_list;
    rabbitmq_server_list.push_back("30.1.1.1:100");
    rabbitmq_server_list.push_back("30.1.1.2:100");
    TASK_UTIL_EXPECT_VECTOR_EQ(rabbitmq_server_list,
                     options_.rabbitmq_server_list());
    vector<string> config_db_server_list;
    config_db_server_list.push_back("40.1.1.1:100");
    config_db_server_list.push_back("40.1.1.2:100");
    TASK_UTIL_EXPECT_VECTOR_EQ(config_db_server_list,
                     options_.config_db_server_list());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
