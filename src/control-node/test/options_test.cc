/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include "base/contrail_ports.h"
#include "base/util.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/options.h"
#include "io/event_manager.h"

using namespace std;
using namespace boost::asio::ip;

static uint16_t default_bgp_port = ContrailPorts::ControlBgp;
static uint16_t default_collector_port = ContrailPorts::CollectorPort;
static uint16_t default_http_server_port = ContrailPorts::HttpPortControl;
static uint16_t default_xmpp_port = ContrailPorts::ControlXmpp;
static uint16_t default_discovery_port = ContrailPorts::DiscoveryServerPort;

class OptionsTest : public ::testing::Test {
protected:
    OptionsTest() { }

    virtual void SetUp() {
        boost::system::error_code error;
        hostname_ = host_name(error);
        host_ip_ = GetHostIp(evm_.io_service(), hostname_);
    }

    EventManager evm_;
    std::string hostname_;
    std::string host_ip_;
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
    EXPECT_EQ(options_.collector_server(), "");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(), "/etc/contrail/control-node.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
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
    EXPECT_EQ(options_.ifmap_server_url(), "");
    EXPECT_EQ(options_.ifmap_password(), "control_user_passwd");
    EXPECT_EQ(options_.ifmap_user(), "control_user");
    EXPECT_EQ(options_.ifmap_certs_store(), "");
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), false);
}

TEST_F(OptionsTest, DefaultConfFile) {
    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/control-node.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "bgp_config.xml");
    EXPECT_EQ(options_.bgp_port(), default_bgp_port);
    EXPECT_EQ(options_.collector_server(), "");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/control-node/control-node.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
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
    EXPECT_EQ(options_.ifmap_server_url(), "");
    EXPECT_EQ(options_.ifmap_password(), "control_user_passwd");
    EXPECT_EQ(options_.ifmap_user(), "control_user");
    EXPECT_EQ(options_.ifmap_certs_store(), "");
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), false);
}

TEST_F(OptionsTest, OverrideStringFromCommandLine) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/control-node.conf";
    char argv_2[] = "--DEFAULT.log_file=test.log";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "bgp_config.xml");
    EXPECT_EQ(options_.bgp_port(), default_bgp_port);
    EXPECT_EQ(options_.collector_server(), "");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/control-node/control-node.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "test.log"); // Overridden from cmd line.
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 10*1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), false);
    EXPECT_EQ(options_.ifmap_server_url(), "");
    EXPECT_EQ(options_.ifmap_password(), "control_user_passwd");
    EXPECT_EQ(options_.ifmap_user(), "control_user");
    EXPECT_EQ(options_.ifmap_certs_store(), "");
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), false);
}

TEST_F(OptionsTest, OverrideBooleanFromCommandLine) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/control-node.conf";
    char argv_2[] = "--DEFAULT.test_mode";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "bgp_config.xml");
    EXPECT_EQ(options_.bgp_port(), default_bgp_port);
    EXPECT_EQ(options_.collector_server(), "");
    EXPECT_EQ(options_.collector_port(), default_collector_port);
    EXPECT_EQ(options_.config_file(),
              "controller/src/control-node/control-node.conf");
    EXPECT_EQ(options_.discovery_server(), "");
    EXPECT_EQ(options_.discovery_port(), default_discovery_port);
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
    EXPECT_EQ(options_.ifmap_server_url(), "");
    EXPECT_EQ(options_.ifmap_password(), "control_user_passwd");
    EXPECT_EQ(options_.ifmap_user(), "control_user");
    EXPECT_EQ(options_.ifmap_certs_store(), "");
    EXPECT_EQ(options_.xmpp_port(), default_xmpp_port);
    EXPECT_EQ(options_.test_mode(), true); // Overridden from command line.
}

TEST_F(OptionsTest, CustomConfigFile) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "bgp_port=200\n"
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
        "xmpp_server_port=100\n"
        "\n"
        "[COLLECTOR]\n"
        "port=100\n"
        "server=3.4.5.6\n"
        "\n"
        "[DISCOVERY]\n"
        "port=100\n"
        "server=1.0.0.1 # discovery_server IP address\n"
        "\n"
        "[IFMAP]\n"
        "certs_store=test-store\n"
        "password=test-password\n"
        "server_url=https://127.0.0.1:100\n"
        "user=test-user\n";

    ofstream config_file;
    config_file.open("/tmp/options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=/tmp/options_test_config_file.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "test.xml");
    EXPECT_EQ(options_.bgp_port(), 200);
    EXPECT_EQ(options_.collector_server(), "3.4.5.6");
    EXPECT_EQ(options_.collector_port(), 100);
    EXPECT_EQ(options_.config_file(),
              "/tmp/options_test_config_file.conf");
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
    EXPECT_EQ(options_.ifmap_server_url(), "https://127.0.0.1:100");
    EXPECT_EQ(options_.ifmap_password(), "test-password");
    EXPECT_EQ(options_.ifmap_user(), "test-user");
    EXPECT_EQ(options_.ifmap_certs_store(), "test-store");
    EXPECT_EQ(options_.xmpp_port(), 100);
    EXPECT_EQ(options_.test_mode(), true);
}

TEST_F(OptionsTest, CustomConfigFileAndOverrideFromCommandLine) {
    string config = ""
        "[DEFAULT]\n"
        "bgp_config_file=test.xml\n"
        "bgp_port=200\n"
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
        "\n"
        "[COLLECTOR]\n"
        "port=100\n"
        "server=3.4.5.6\n"
        "\n"
        "[DISCOVERY]\n"
        "port=100\n"
        "server=1.0.0.1 # discovery_server IP address\n"
        "\n"
        "[IFMAP]\n"
        "certs_store=test-store\n"
        "password=test-password\n"
        "server_url=https://127.0.0.1:100\n"
        "user=test-user\n";

    ofstream config_file;
    config_file.open("/tmp/options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 5;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=/tmp/options_test_config_file.conf";
    char argv_2[] = "--DEFAULT.log_file=new_test.log";
    char argv_3[] = "--DEFAULT.log_local";
    char argv_4[] = "--COLLECTOR.port=1000";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.bgp_config_file(), "test.xml");
    EXPECT_EQ(options_.bgp_port(), 200);
    EXPECT_EQ(options_.collector_server(), "3.4.5.6");
    EXPECT_EQ(options_.collector_port(), 1000);
    EXPECT_EQ(options_.config_file(),
              "/tmp/options_test_config_file.conf");
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
    EXPECT_EQ(options_.ifmap_server_url(), "https://127.0.0.1:100");
    EXPECT_EQ(options_.ifmap_password(), "test-password");
    EXPECT_EQ(options_.ifmap_user(), "test-user");
    EXPECT_EQ(options_.ifmap_certs_store(), "test-store");
    EXPECT_EQ(options_.xmpp_port(), 100);
    EXPECT_EQ(options_.test_mode(), true);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
