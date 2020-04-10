/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include <boost/asio/ip/host_name.hpp>
#include "base/contrail_ports.h"
#include "base/util.h"
#include "base/logging.h"
#include "base/address_util.h"
#include "base/test/task_test_util.h"
#include "cmn/dns_options.h"
#include "io/event_manager.h"

using namespace std;
using namespace boost::asio::ip;

static uint16_t default_http_server_port = ContrailPorts::HttpPortDns();
static uint16_t default_dns_server_port = ContrailPorts::DnsServerPort();

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
    char argv_0[] = "dns_options_test";
    argv[0] = argv_0;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.dns_config_file(), "dns_config.xml");
    EXPECT_EQ(options_.config_file(), "/etc/contrail/contrail-dns.conf");
    EXPECT_EQ(options_.named_config_file(), "contrail-named.conf");
    EXPECT_EQ(options_.named_config_dir(), "/etc/contrail/dns");
    EXPECT_EQ(options_.named_log_file(), "/var/log/contrail/contrail-named.log");
    EXPECT_EQ(options_.rndc_config_file(), "contrail-rndc.conf");
    EXPECT_EQ(options_.rndc_secret(), "xvysmOR8lnUQRBcunkC6vg==");
    EXPECT_EQ(options_.named_max_cache_size(), "32M");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.dns_server_port(), default_dns_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "<stdout>");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), false);
    EXPECT_EQ(options_.config_db_user(), "");
    EXPECT_EQ(options_.config_db_password(), "");
    EXPECT_EQ(options_.config_db_use_ssl(), false);
    EXPECT_EQ(options_.configdb_options().config_db_ca_certs, "");
    EXPECT_EQ(options_.rabbitmq_user(), "guest");
    EXPECT_EQ(options_.rabbitmq_password(), "guest");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_rabbitmq_server_list_,
                               options_.rabbitmq_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_configdb_server_list_,
                               options_.config_db_server_list());

    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.sandesh_config().system_logs_rate_limit,
              g_sandesh_constants.DEFAULT_SANDESH_SEND_RATELIMIT);
    EXPECT_FALSE(options_.sandesh_config().disable_object_logs);
}

TEST_F(OptionsTest, DefaultConfFile) {
    int argc = 2;
    char *argv[argc];
    char argv_0[] = "dns_options_test";
    char argv_1[] = "--conf_file=controller/src/dns/contrail-dns.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.dns_config_file(), "dns_config.xml");
    EXPECT_EQ(options_.config_file(),
              "controller/src/dns/contrail-dns.conf");
    EXPECT_EQ(options_.named_config_file(), "contrail-named.conf");
    EXPECT_EQ(options_.named_config_dir(), "/etc/contrail/dns");
    EXPECT_EQ(options_.named_log_file(), "/var/log/contrail/contrail-named.log");
    EXPECT_EQ(options_.rndc_config_file(), "contrail-rndc.conf");
    EXPECT_EQ(options_.rndc_secret(), "xvysmOR8lnUQRBcunkC6vg==");
    EXPECT_EQ(options_.named_max_cache_size(), "32M");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.dns_server_port(), default_dns_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "/var/log/contrail/contrail-dns.log");
    EXPECT_EQ(options_.log_property_file(), "");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.config_db_user(), "");
    EXPECT_EQ(options_.config_db_password(), "");
    EXPECT_EQ(options_.config_db_use_ssl(), false);
    EXPECT_EQ(options_.configdb_options().config_db_ca_certs, "");
    EXPECT_EQ(options_.rabbitmq_user(), "guest");
    EXPECT_EQ(options_.rabbitmq_password(), "guest");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    TASK_UTIL_EXPECT_VECTOR_EQ(default_rabbitmq_server_list_,
                               options_.rabbitmq_server_list());
    TASK_UTIL_EXPECT_VECTOR_EQ(default_configdb_server_list_,
                               options_.config_db_server_list());
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_FALSE(options_.sandesh_config().disable_object_logs);
}

TEST_F(OptionsTest, OverrideStringFromCommandLine) {
    int argc = 7;
    char *argv[argc];
    char argv_0[] = "dns_options_test";
    char argv_1[] = "--conf_file=controller/src/dns/contrail-dns.conf";
    char argv_2[] = "--DEFAULT.log_file=test.log";
    char argv_3[] = "--DEFAULT.rndc_config_file=test.rndc";
    char argv_4[] = "--DEFAULT.rndc_secret=secret123";
    char argv_5[] = "--DEFAULT.log_property_file=log4cplus.prop";
    char argv_6[] = "--DEFAULT.sandesh_send_rate_limit=5";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;
    argv[5] = argv_5;
    argv[6] = argv_6;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.dns_config_file(), "dns_config.xml");
    EXPECT_EQ(options_.config_file(),
              "controller/src/dns/contrail-dns.conf");
    EXPECT_EQ(options_.named_config_file(), "contrail-named.conf");
    EXPECT_EQ(options_.named_config_dir(), "/etc/contrail/dns");
    EXPECT_EQ(options_.named_log_file(), "/var/log/contrail/contrail-named.log");
    EXPECT_EQ(options_.rndc_config_file(), "test.rndc");
    EXPECT_EQ(options_.rndc_secret(), "secret123");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.dns_server_port(), default_dns_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "test.log"); // Overridden from cmd line.
    EXPECT_EQ(options_.log_property_file(), "log4cplus.prop"); // Overridden from cmd line.
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.config_db_user(), "");
    EXPECT_EQ(options_.config_db_password(), "");
    EXPECT_EQ(options_.config_db_use_ssl(), false);
    EXPECT_EQ(options_.configdb_options().config_db_ca_certs, "");
    EXPECT_EQ(options_.rabbitmq_user(), "guest");
    EXPECT_EQ(options_.rabbitmq_password(), "guest");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    EXPECT_EQ(options_.test_mode(), false);
    EXPECT_EQ(options_.sandesh_config().system_logs_rate_limit, 5U);
}

TEST_F(OptionsTest, OverrideBooleanFromCommandLine) {
    int argc = 4;
    char *argv[argc];
    char argv_0[] = "dns_options_test";
    char argv_1[] = "--conf_file=controller/src/dns/contrail-dns.conf";
    char argv_2[] = "--DEFAULT.test_mode";
    char argv_3[] = "--SANDESH.disable_object_logs";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;

    options_.Parse(evm_, argc, argv);

    TASK_UTIL_EXPECT_VECTOR_EQ(default_collector_server_list_,
                     options_.collector_server_list());
    EXPECT_EQ(options_.dns_config_file(), "dns_config.xml");
    EXPECT_EQ(options_.config_file(),
              "controller/src/dns/contrail-dns.conf");
    EXPECT_EQ(options_.hostname(), hostname_);
    EXPECT_EQ(options_.host_ip(), host_ip_);
    EXPECT_EQ(options_.http_server_port(), default_http_server_port);
    EXPECT_EQ(options_.dns_server_port(), default_dns_server_port);
    EXPECT_EQ(options_.log_category(), "");
    EXPECT_EQ(options_.log_disable(), false);
    EXPECT_EQ(options_.log_file(), "/var/log/contrail/contrail-dns.log");
    EXPECT_EQ(options_.log_files_count(), 10);
    EXPECT_EQ(options_.log_file_size(), 1024*1024);
    EXPECT_EQ(options_.log_level(), "SYS_NOTICE");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.config_db_user(), "");
    EXPECT_EQ(options_.config_db_password(), "");
    EXPECT_EQ(options_.config_db_use_ssl(), false);
    EXPECT_EQ(options_.configdb_options().config_db_ca_certs, "");
    EXPECT_EQ(options_.rabbitmq_user(), "guest");
    EXPECT_EQ(options_.rabbitmq_password(), "guest");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    EXPECT_EQ(options_.test_mode(), true); // Overridden from command line.
    EXPECT_TRUE(options_.sandesh_config().disable_object_logs);
}

TEST_F(OptionsTest, CustomConfigFile) {
    string config = ""
        "[DEFAULT]\n"
        "collectors=10.10.10.1:100\n"
        "collectors=20.20.20.2:200\n"
        "collectors=30.30.30.3:300\n"
        "dns_config_file=test.xml\n"
        "named_config_file=named.test\n"
        "named_config_directory=/var/log/dns\n"
        "named_log_file=/etc/contrail/dns/named.log\n"
        "rndc_config_file=file.rndc\n"
        "rndc_secret=abcd123\n"
        "hostip=1.2.3.4\n"
        "hostname=test\n"
        "http_server_port=800\n"
        "dns_server_port=9009\n"
        "log_category=dns\n"
        "log_disable=1\n"
        "log_file=test.log\n"
        "log_files_count=20\n"
        "log_file_size=1024\n"
        "log_level=SYS_DEBUG\n"
        "log_local=1\n"
        "test_mode=1\n"
        "log_property_file=log4cplus.prop\n"
        "sandesh_send_rate_limit=5\n"
        "xmpp_dns_auth_enable=1\n"
        "xmpp_server_cert=/etc/server.pem\n"
        "xmpp_server_key=/etc/server-privkey.pem\n"
        "xmpp_ca_cert=/etc/ca-cert.pem\n"
        "\n"
        "[CONFIGDB]\n"
        "config_db_username=test-db-user\n"
        "config_db_password=test-db-password\n"
        "config_db_use_ssl=true\n"
        "rabbitmq_user=test-user\n"
        "rabbitmq_password=test-password\n"
        "\n"
        "[SANDESH]\n"
        "disable_object_logs=0\n"
        "\n";

    ofstream config_file;
    config_file.open("./dns_options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 2;
    char *argv[argc];
    char argv_0[] = "dns_options_test";
    char argv_1[] = "--conf_file=./dns_options_test_config_file.conf";
    argv[0] = argv_0;
    argv[1] = argv_1;

    options_.Parse(evm_, argc, argv);

    vector<string> collector_server_list;
    collector_server_list.push_back("10.10.10.1:100");
    collector_server_list.push_back("20.20.20.2:200");
    collector_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(collector_server_list,
                     options_.collector_server_list());
    EXPECT_EQ(options_.dns_config_file(), "test.xml");
    EXPECT_EQ(options_.config_file(),
              "./dns_options_test_config_file.conf");
    EXPECT_EQ(options_.named_config_file(), "named.test");
    EXPECT_EQ(options_.named_config_dir(), "/var/log/dns");
    EXPECT_EQ(options_.named_log_file(), "/etc/contrail/dns/named.log");
    EXPECT_EQ(options_.rndc_config_file(), "file.rndc");
    EXPECT_EQ(options_.rndc_secret(), "abcd123");
    EXPECT_EQ(options_.hostname(), "test");
    EXPECT_EQ(options_.host_ip(), "1.2.3.4");
    EXPECT_EQ(options_.http_server_port(), 800);
    EXPECT_EQ(options_.dns_server_port(), 9009);
    EXPECT_EQ(options_.log_category(), "dns");
    EXPECT_EQ(options_.log_disable(), true);
    EXPECT_EQ(options_.log_file(), "test.log");
    EXPECT_EQ(options_.log_property_file(), "log4cplus.prop");
    EXPECT_EQ(options_.log_files_count(), 20);
    EXPECT_EQ(options_.log_file_size(), 1024);
    EXPECT_EQ(options_.log_level(), "SYS_DEBUG");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.config_db_user(), "test-db-user");
    EXPECT_EQ(options_.config_db_password(), "test-db-password");
    EXPECT_EQ(options_.config_db_use_ssl(), true);
    EXPECT_EQ(options_.rabbitmq_user(), "test-user");
    EXPECT_EQ(options_.rabbitmq_password(), "test-password");
    EXPECT_EQ(options_.test_mode(), true);
    EXPECT_EQ(options_.sandesh_config().system_logs_rate_limit, 5U);
    EXPECT_EQ(options_.xmpp_auth_enabled(), true);
    EXPECT_EQ(options_.xmpp_server_cert(), "/etc/server.pem");
    EXPECT_EQ(options_.xmpp_server_key(), "/etc/server-privkey.pem");
    EXPECT_EQ(options_.xmpp_ca_cert(), "/etc/ca-cert.pem");
    EXPECT_FALSE(options_.sandesh_config().disable_object_logs);
    std::remove("./dns_options_test_config_file.conf");
}

TEST_F(OptionsTest, CustomConfigFileAndOverrideFromCommandLine) {
    string config = ""
        "[DEFAULT]\n"
        "collectors=10.10.10.1:100\n"
        "collectors=20.20.20.2:200\n"
        "collectors=30.30.30.3:300\n"
        "dns_config_file=test.xml\n"
        "named_config_file=named.test\n"
        "named_config_directory=/var/log/dns\n"
        "named_log_file=/etc/contrail/dns/named.log\n"
        "rndc_config_file=rndc.test\n"
        "rndc_secret=abcd123\n"
        "hostip=1.2.3.4\n"
        "hostname=test\n"
        "http_server_port=800\n"
        "dns_server_port=9009\n"
        "log_category=dns\n"
        "log_disable=1\n"
        "log_file=test.log\n"
        "log_files_count=20\n"
        "log_file_size=1024\n"
        "log_level=SYS_DEBUG\n"
        "log_local=0\n"
        "test_mode=1\n"
        "sandesh_send_rate_limit=5\n"
        "\n"
        "[CONFIGDB]\n"
        "config_db_username=test-db-user\n"
        "config_db_password=test-db-password\n"
        "rabbitmq_user=test-user\n"
        "rabbitmq_password=test-password\n"
        "\n"
        "[SANDESH]\n"
        "disable_object_logs=0\n"
        "\n";

    ofstream config_file;
    config_file.open("./dns_options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 12;
    char *argv[argc];
    char argv_0[] = "dns_options_test";
    char argv_1[] = "--conf_file=./dns_options_test_config_file.conf";
    char argv_2[] = "--DEFAULT.log_file=new_test.log";
    char argv_3[] = "--DEFAULT.log_local";
    char argv_4[] = "--DEFAULT.collectors=11.10.10.1:100";
    char argv_5[] = "--DEFAULT.collectors=21.20.20.2:200";
    char argv_6[] = "--DEFAULT.collectors=31.30.30.3:300";
    char argv_7[] = "--DEFAULT.named_config_directory=/etc/contrail/dns/test";
    char argv_8[] = "--DEFAULT.rndc_config_file=new.rndc";
    char argv_9[] = "--DEFAULT.rndc_secret=new-secret-123";
    char argv_10[] = "--DEFAULT.sandesh_send_rate_limit=7";
    char argv_11[] = "--SANDESH.disable_object_logs";
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

    options_.Parse(evm_, argc, argv);

    vector<string> collector_server_list;
    collector_server_list.push_back("11.10.10.1:100");
    collector_server_list.push_back("21.20.20.2:200");
    collector_server_list.push_back("31.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(collector_server_list,
                     options_.collector_server_list());
    EXPECT_EQ(options_.dns_config_file(), "test.xml");
    EXPECT_EQ(options_.config_file(),
              "./dns_options_test_config_file.conf");
    EXPECT_EQ(options_.named_config_file(), "named.test");
    EXPECT_EQ(options_.named_config_dir(), "/etc/contrail/dns/test");
    EXPECT_EQ(options_.named_log_file(), "/etc/contrail/dns/named.log");
    EXPECT_EQ(options_.rndc_config_file(), "new.rndc");
    EXPECT_EQ(options_.rndc_secret(), "new-secret-123");
    EXPECT_EQ(options_.hostname(), "test");
    EXPECT_EQ(options_.host_ip(), "1.2.3.4");
    EXPECT_EQ(options_.http_server_port(), 800);
    EXPECT_EQ(options_.dns_server_port(), 9009);
    EXPECT_EQ(options_.log_category(), "dns");
    EXPECT_EQ(options_.log_disable(), true);
    EXPECT_EQ(options_.log_file(), "new_test.log");
    EXPECT_EQ(options_.log_files_count(), 20);
    EXPECT_EQ(options_.log_file_size(), 1024);
    EXPECT_EQ(options_.log_level(), "SYS_DEBUG");
    EXPECT_EQ(options_.log_local(), true);
    EXPECT_EQ(options_.config_db_user(), "test-db-user");
    EXPECT_EQ(options_.config_db_password(), "test-db-password");
    EXPECT_EQ(options_.rabbitmq_user(), "test-user");
    EXPECT_EQ(options_.rabbitmq_password(), "test-password");
    EXPECT_EQ(options_.test_mode(), true);
    EXPECT_EQ(options_.sandesh_config().system_logs_rate_limit, 7U);
    EXPECT_TRUE(options_.sandesh_config().disable_object_logs);
    std::remove("./dns_options_test_config_file.conf");
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

TEST_F(OptionsTest, OverrideConfigdbOptionsFromCommandLine) {
    int argc = 11;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=controller/src/control-node/contrail-control.conf";
    char argv_2[] = "--CONFIGDB.rabbitmq_user=myuser";
    char argv_3[] = "--CONFIGDB.rabbitmq_password=mynewpassword";
    char argv_4[] = "--CONFIGDB.rabbitmq_use_ssl=false";
    char argv_5[] = "--CONFIGDB.rabbitmq_server_list=10.1.1.1:100 10.1.1.2:100";
    char argv_6[] = "--CONFIGDB.config_db_server_list=20.1.1.1:100 20.1.1.2:100";
    char argv_7[] = "--CONFIGDB.config_db_username=dbuser";
    char argv_8[] = "--CONFIGDB.config_db_password=dbpassword";
    char argv_9[] = "--CONFIGDB.config_db_use_ssl=true";
    char argv_10[] = "--CONFIGDB.config_db_ca_certs=/tmp/null";
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

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.config_db_user(), "dbuser");
    EXPECT_EQ(options_.config_db_password(), "dbpassword");
    EXPECT_EQ(options_.rabbitmq_user(), "myuser");
    EXPECT_EQ(options_.rabbitmq_password(), "mynewpassword");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    EXPECT_EQ(options_.config_db_use_ssl(), true);
    EXPECT_EQ(options_.configdb_options().config_db_ca_certs, "/tmp/null");

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
        "config_db_server_list=20.1.1.1:100 20.1.1.2:100\n"
        "config_db_use_ssl=true\n";

    ofstream config_file;
    config_file.open("./options_test_config_file.conf");
    config_file << config;
    config_file.close();

    int argc = 8;
    char *argv[argc];
    char argv_0[] = "options_test";
    char argv_1[] = "--conf_file=./options_test_config_file.conf";
    char argv_2[] = "--CONFIGDB.rabbitmq_user=myuser";
    char argv_3[] = "--CONFIGDB.rabbitmq_password=mynewpassword";
    char argv_4[] = "--CONFIGDB.rabbitmq_use_ssl=false";
    char argv_5[] = "--CONFIGDB.rabbitmq_server_list=30.1.1.1:100 30.1.1.2:100";
    char argv_6[] = "--CONFIGDB.config_db_server_list=40.1.1.1:100 40.1.1.2:100";
    char argv_7[] = "--CONFIGDB.config_db_use_ssl=false";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;
    argv[3] = argv_3;
    argv[4] = argv_4;
    argv[5] = argv_5;
    argv[6] = argv_6;
    argv[7] = argv_7;

    options_.Parse(evm_, argc, argv);

    EXPECT_EQ(options_.rabbitmq_user(), "myuser");
    EXPECT_EQ(options_.rabbitmq_password(), "mynewpassword");
    EXPECT_EQ(options_.rabbitmq_ssl_enabled(), false);
    EXPECT_EQ(options_.config_db_use_ssl(), false);

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
