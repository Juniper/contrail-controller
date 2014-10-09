/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/resource.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"

#include <base/test/task_test_util.h>
#include <cfg/cfg_init.h>

namespace opt = boost::program_options;

class FlowTest : public ::testing::Test {
public:
    virtual void SetUp()  { }

    virtual void TearDown() { }
};

void RouterIdDepInit(Agent *agent) {
}

TEST_F(FlowTest, Agent_Conf_file_1) {
    AgentParam param(Agent::GetInstance());
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

    EXPECT_STREQ(param.vhost_name().c_str(), "vhost0");
    EXPECT_EQ(param.vhost_addr().to_ulong(),
              Ip4Address::from_string("10.1.1.1").to_ulong());
    EXPECT_EQ(param.vhost_plen(), 24);
    EXPECT_EQ(param.vhost_prefix().to_ulong(),
              Ip4Address::from_string("10.1.1.0").to_ulong());
    EXPECT_EQ(param.vhost_gw().to_ulong(),
              Ip4Address::from_string("10.1.1.254").to_ulong());
    EXPECT_STREQ(param.eth_port().c_str(), "vnet0");
    EXPECT_EQ(param.xmpp_server_1().to_ulong(),
              Ip4Address::from_string("127.0.0.1").to_ulong());
    EXPECT_EQ(param.xmpp_server_2().to_ulong(), 0);
    EXPECT_EQ(param.dns_server_1().to_ulong(),
              Ip4Address::from_string("127.0.0.1").to_ulong());
    EXPECT_EQ(param.dns_port_1(), 53);
    EXPECT_EQ(param.dns_server_2().to_ulong(), 0);
    EXPECT_EQ(param.dns_port_2(), 53);
    EXPECT_EQ(param.discovery_server().to_ulong(),
              Ip4Address::from_string("10.3.1.1").to_ulong());
    EXPECT_EQ(param.mgmt_ip().to_ulong(), 0);
    EXPECT_EQ(param.xmpp_instance_count(), 2);
    EXPECT_STREQ(param.tunnel_type().c_str(), "MPLSoGRE");
    EXPECT_STREQ(param.metadata_shared_secret().c_str(), "contrail");
    EXPECT_EQ(param.max_vm_flows(), 50);
    EXPECT_EQ(param.linklocal_system_flows(), 1024);
    EXPECT_EQ(param.linklocal_vm_flows(), 512);
    EXPECT_EQ(param.flow_cache_timeout(), 30);
    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_STREQ(param.program_name().c_str(), "test-param");
}

TEST_F(FlowTest, Agent_Conf_file_2) {
    AgentParam param(Agent::GetInstance());
    param.Init("controller/src/vnsw/agent/init/test/cfg1.ini", "test-param");

    EXPECT_EQ(param.max_vm_flows(), 100);
    EXPECT_EQ(param.linklocal_system_flows(), 2048);
    EXPECT_EQ(param.linklocal_vm_flows(), 2048);
    EXPECT_EQ(param.xmpp_server_1().to_ulong(),
              Ip4Address::from_string("11.1.1.1").to_ulong());
    EXPECT_EQ(param.xmpp_server_2().to_ulong(),
              Ip4Address::from_string("12.1.1.1").to_ulong());
    EXPECT_EQ(param.dns_server_1().to_ulong(),
              Ip4Address::from_string("13.1.1.1").to_ulong());
    EXPECT_EQ(param.dns_port_1(), 9001);
    EXPECT_EQ(param.dns_server_2().to_ulong(),
              Ip4Address::from_string("14.1.1.1").to_ulong());
    EXPECT_EQ(param.dns_port_2(), 12999);
}

// Check that linklocal flows are updated when the system limits are lowered
TEST_F(FlowTest, Agent_Conf_file_3) {
    struct rlimit rl;
    rl.rlim_max = 128;
    rl.rlim_cur = 64;
    int result = setrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        AgentParam param(Agent::GetInstance());
        param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

        EXPECT_EQ(param.linklocal_system_flows(), 63);
        EXPECT_EQ(param.linklocal_vm_flows(), 63);
    }
}

TEST_F(FlowTest, Agent_Conf_file_4) {
    struct rlimit rl;
    rl.rlim_max = 32;
    rl.rlim_cur = 32;
    int result = setrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        AgentParam param(Agent::GetInstance());
        param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

        EXPECT_EQ(param.linklocal_system_flows(), 0);
        EXPECT_EQ(param.linklocal_vm_flows(), 0);
    }
}

TEST_F(FlowTest, Agent_Conf_Xen_1) {
    AgentParam param(Agent::GetInstance());
    param.Init("controller/src/vnsw/agent/init/test/cfg-xen.ini", "test-param");

    EXPECT_STREQ(param.xen_ll_name().c_str(), "xenapi");
    EXPECT_EQ(param.xen_ll_addr().to_ulong(),
              Ip4Address::from_string("169.254.0.1").to_ulong());
    EXPECT_EQ(param.xen_ll_prefix().to_ulong(),
              Ip4Address::from_string("169.254.0.0").to_ulong());
    EXPECT_EQ(param.xen_ll_plen(), 24);
}

TEST_F(FlowTest, Agent_Param_1) {
    int argc = 15;
    char *argv[] = {
        (char *) "",
        (char *) "--config_file", 
                        (char *)"controller/src/vnsw/agent/init/test/cfg.ini",
        (char *) "--DEFAULT.collectors",     (char *)"1.1.1.1:1000",
        (char *) "--DEFAULT.log_local",
        (char *) "--DEFAULT.log_flow",
        (char *) "--DEFAULT.log_level",     (char *)"SYS_DEBUG",
        (char *) "--DEFAULT.log_category",  (char *)"Test",
        (char *) "--DEFAULT.http_server_port", (char *)"8000",
        (char *) "--DEFAULT.hostname",     (char *)"vhost-1",
    };

    AgentParam param(Agent::GetInstance());
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg-xen.ini", "test-param");

    EXPECT_TRUE(param.log_local());
    EXPECT_TRUE(param.log_flow());
    EXPECT_STREQ(param.log_level().c_str(), "SYS_DEBUG");
    EXPECT_STREQ(param.log_category().c_str(), "Test");
    EXPECT_EQ(param.collector_server_list().size(), 1);
    vector<string> collector_list = param.collector_server_list();
    string first_collector = collector_list.at(0);
    EXPECT_STREQ(first_collector.c_str(), "1.1.1.1:1000");
    EXPECT_EQ(param.http_server_port(), 8000);
    EXPECT_STREQ(param.host_name().c_str(), "vhost-1");

}

TEST_F(FlowTest, Agent_Arg_Override_Config_1) {
    int argc = 9;
    char *argv[] = {
        (char *) "",
        (char *) "--config_file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg.ini",
        (char *) "--HYPERVISOR.type",    (char *)"xen", 
        (char *) "--HYPERVISOR.xen_ll_interface",   (char *)"xenport",
        (char *) "--HYPERVISOR.xen_ll_ip", (char *)"1.1.1.2/16",
    };

    AgentParam param(Agent::GetInstance());
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_EQ(param.mode(), AgentParam::MODE_XEN);
    EXPECT_STREQ(param.xen_ll_name().c_str(), "xenport");
    EXPECT_EQ(param.xen_ll_addr().to_ulong(),
              Ip4Address::from_string("1.1.1.2").to_ulong());
    EXPECT_EQ(param.xen_ll_prefix().to_ulong(),
              Ip4Address::from_string("1.1.0.0").to_ulong());
    EXPECT_EQ(param.xen_ll_plen(), 16);
}

TEST_F(FlowTest, Agent_Arg_Override_Config_2) {
    int argc = 9;
    char *argv[] = {
        (char *) "",
        (char *) "--DNS.server",    (char *)"20.1.1.1:500", (char *)"21.1.1.1:15001", 
        (char *) "--CONTROL-NODE.server",   (char *)"22.1.1.1", (char *)"23.1.1.1",
        (char *) "--DEFAULT.debug",   (char *)"0",
    };

    AgentParam param(Agent::GetInstance());
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");
    EXPECT_EQ(param.xmpp_server_1().to_ulong(),
              Ip4Address::from_string("22.1.1.1").to_ulong());
    EXPECT_EQ(param.xmpp_server_2().to_ulong(),
              Ip4Address::from_string("23.1.1.1").to_ulong());
    EXPECT_EQ(param.dns_server_1().to_ulong(),
              Ip4Address::from_string("20.1.1.1").to_ulong());
    EXPECT_EQ(param.dns_port_1(), 500);
    EXPECT_EQ(param.dns_server_2().to_ulong(),
              Ip4Address::from_string("21.1.1.1").to_ulong());
    EXPECT_EQ(param.dns_port_2(), 15001);
}

/* Some command line args have default values. If user has not passed these
 * command line args, but has specified values in config file, then values
 * specified config file should be taken */
TEST_F(FlowTest, Default_Cmdline_arg1) {
    int argc = 3;
    char *argv[] = {
        (char *) "",
        (char *) "--config_file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg-default1.ini",
    };

    AgentParam param(Agent::GetInstance());
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg-default1.ini",
               "test-param");
    EXPECT_EQ(param.flow_cache_timeout(), 60);
    EXPECT_EQ(param.http_server_port(), 10001);
    EXPECT_STREQ(param.log_category().c_str(), "abc");
    EXPECT_STREQ(param.log_file().c_str(), "/var/log/contrail/vrouter2.log");
    EXPECT_STREQ(param.log_level().c_str(), "SYS_ERR");
    EXPECT_TRUE(param.isXenMode());
}

/* Some command line args have default values. If user has not passed these
 * command line args, and has NOT specified values in config file, then
 * verify that default value from command line args is picked up */
TEST_F(FlowTest, Default_Cmdline_arg2) {
    uint16_t http_server_port = ContrailPorts::HttpPortAgent();
    uint16_t flow_timeout = Agent::kDefaultFlowCacheTimeout;
    AgentParam param(Agent::GetInstance());
    param.Init("controller/src/vnsw/agent/init/test/cfg-default2.ini",
               "test-param");
    EXPECT_EQ(param.flow_cache_timeout(), flow_timeout);
    EXPECT_EQ(param.http_server_port(), http_server_port);
    EXPECT_STREQ(param.log_category().c_str(), "*");
    EXPECT_STREQ(param.log_file().c_str(),
                 Agent::GetInstance()->log_file().c_str());
    EXPECT_STREQ(param.log_level().c_str(), "SYS_DEBUG");
    EXPECT_TRUE(param.isKvmMode());
}

/* Some command line args have default values. If user has explicitly passed 
 * values for these command line args and has also specified values in config 
 * file, then values specified on command line should be taken */
TEST_F(FlowTest, Default_Cmdline_arg3) {
    int argc = 9;
    char *argv[] = {
        (char *) "",
        (char *) "--DEFAULT.flow_cache_timeout", (char *)"100",
        (char *) "--DEFAULT.http_server_port", (char *)"20001",
        (char *) "--DEFAULT.log_file", (char *)"3.log",
        (char *) "--HYPERVISOR.type", (char *)"vmware",
    };

    AgentParam param(Agent::GetInstance());
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg-default1.ini",
               "test-param");
    EXPECT_EQ(param.flow_cache_timeout(), 100);
    EXPECT_EQ(param.http_server_port(), 20001);
    EXPECT_STREQ(param.log_file().c_str(), "3.log");
    EXPECT_TRUE(param.isVmwareMode());
}

TEST_F(FlowTest, MultitokenVector) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "";
    char argv_1[] = "--DEFAULT.collectors=10.10.10.1:100 20.20.20.2:200";
    char argv_2[] = "--DEFAULT.collectors=30.30.30.3:300";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    AgentParam param(Agent::GetInstance());
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

    vector<string> collector_server_list;
    collector_server_list.push_back("10.10.10.1:100");
    collector_server_list.push_back("20.20.20.2:200");
    collector_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(param.collector_server_list(),
                     collector_server_list);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}
