/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include <init/agent_param.h>

class FlowTest : public ::testing::Test {
};

void RouterIdDepInit() {
}

TEST_F(FlowTest, Agent_Conf_file_1) {
    int argc = 3;
    char *argv[] = {
        (char *) "test-param",
        (char *) "--conf-file", (char *)"controller/src/vnsw/agent/init/test/cfg.ini"
    };

    AgentParam param;
    param.Init(argc, argv);

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
    EXPECT_EQ(param.dns_server_2().to_ulong(), 0);
    EXPECT_EQ(param.discovery_server().to_ulong(),
              Ip4Address::from_string("10.3.1.1").to_ulong());
    EXPECT_EQ(param.mgmt_ip().to_ulong(), 0);
    EXPECT_EQ(param.xmpp_instance_count(), 2);
    EXPECT_STREQ(param.tunnel_type().c_str(), "MPLSoGRE");
    EXPECT_STREQ(param.metadata_shared_secret().c_str(), "contrail");
    EXPECT_EQ(param.linklocal_system_flows(), 1024);
    EXPECT_EQ(param.linklocal_vm_flows(), 512);
    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_STREQ(param.program_name().c_str(), "test-param");
}

TEST_F(FlowTest, Agent_Conf_file_2) {
    int argc = 3;
    char *argv[] = {
        (char *) "test-param",
        (char *) "--conf-file", (char *)"controller/src/vnsw/agent/init/test/cfg1.ini"
    };

    AgentParam param;
    param.Init(argc, argv);

    EXPECT_EQ(param.linklocal_system_flows(), 2048);
    EXPECT_EQ(param.linklocal_vm_flows(), 2048);
}

// Check that linklocal flows are updated when the system limits are lowered
TEST_F(FlowTest, Agent_Conf_file_3) {
    struct rlimit rl;
    rl.rlim_max = 128;
    rl.rlim_cur = 64;
    int result = setrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        int argc = 3;
        char *argv[] = {
            (char *) "test-param",
            (char *) "--conf-file", (char *)"controller/src/vnsw/agent/init/test/cfg.ini"
        };

        AgentParam param;
        param.Init(argc, argv);

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
        int argc = 3;
        char *argv[] = {
            (char *) "test-param",
            (char *) "--conf-file", (char *)"controller/src/vnsw/agent/init/test/cfg.ini"
        };

        AgentParam param;
        param.Init(argc, argv);

        EXPECT_EQ(param.linklocal_system_flows(), 0);
        EXPECT_EQ(param.linklocal_vm_flows(), 0);
    }
}

TEST_F(FlowTest, Agent_Conf_Xen_1) {
    int argc = 3;
    char *argv[] = {
        (char *) "test-param",
        (char *) "--conf-file", (char *)"controller/src/vnsw/agent/init/test/cfg-xen.ini"
    };

    AgentParam param;
    param.Init(argc, argv);

    EXPECT_STREQ(param.xen_ll_name().c_str(), "xenapi");
    EXPECT_EQ(param.xen_ll_addr().to_ulong(),
              Ip4Address::from_string("169.254.0.1").to_ulong());
    EXPECT_EQ(param.xen_ll_prefix().to_ulong(),
              Ip4Address::from_string("169.254.0.0").to_ulong());
    EXPECT_EQ(param.xen_ll_plen(), 24);
}

TEST_F(FlowTest, Agent_Param_1) {
    int argc = 16;
    char *argv[] = {
        (char *) "test-param",
        (char *) "--conf-file", 
                        (char *)"controller/src/vnsw/agent/init/test/cfg-xen.ini",
        (char *) "--DEFAULT.log_local",
        (char *) "--DEFAULT.log_level",     (char *)"SYS_DEBUG",
        (char *) "--DEFAULT.log_category",  (char *)"Test",
        (char *) "--COLLECTOR.server",     (char *)"1.1.1.1",
        (char *) "--COLLECTOR.port",(char *)"1000",
        (char *) "--DEFAULT.http-server-port", (char *)"8000",
        (char *) "--DEFAULT.hostname",     (char *)"vhost-1",
    };

    AgentParam param;
    param.Init(argc, argv);

    EXPECT_TRUE(param.log_local());
    EXPECT_STREQ(param.log_level().c_str(), "SYS_DEBUG");
    EXPECT_STREQ(param.log_category().c_str(), "Test");
    EXPECT_EQ(param.collector().to_ulong(),
              Ip4Address::from_string("1.1.1.1").to_ulong());
    EXPECT_EQ(param.collector_port(), 1000);
    EXPECT_EQ(param.http_server_port(), 8000);
    EXPECT_STREQ(param.host_name().c_str(), "vhost-1");

}

TEST_F(FlowTest, Agen_Arg_Override_Config_1) {
    int argc = 11;
    char *argv[] = {
        (char *) "test-param",
        (char *) "--conf-file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg.ini",
        (char *) "--HYPERVISOR.type",    (char *)"xen", 
        (char *) "--HYPERVISOR.xen-ll-port",   (char *)"xenport",
        (char *) "--HYPERVISOR.xen-ll-ip-address", (char *)"1.1.1.2",
        (char *) "--HYPERVISOR.xen-ll-prefix-len", (char *)"16",
    };

    AgentParam param;
    param.Init(argc, argv);

    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_EQ(param.mode(), AgentParam::MODE_XEN);
    EXPECT_STREQ(param.xen_ll_name().c_str(), "xenport");
    EXPECT_EQ(param.xen_ll_addr().to_ulong(),
              Ip4Address::from_string("1.1.1.2").to_ulong());
    EXPECT_EQ(param.xen_ll_plen(), 16);
}

TEST_F(FlowTest, Agen_Arg_Override_Config_2) {
    int argc = 5;
    char *argv[] = {
        (char *) "test-param",
        (char *) "--conf-file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg.ini",
        (char *) "--VHOST.name",    (char *)"vhost1", 
    };

    AgentParam param;
    param.Init(argc, argv);

    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_STREQ(param.vhost_name().c_str(), "vhost1");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}
