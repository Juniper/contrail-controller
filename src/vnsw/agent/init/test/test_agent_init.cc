/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include <boost/uuid/string_generator.hpp>
#include <boost/program_options.hpp>
#include <base/logging.h>
#include <base/contrail_ports.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"

#include <pugixml/pugixml.hpp>

#include <base/task.h>
#include <io/event_manager.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <base/misc_utils.h>

#include <cmn/agent_cmn.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>
#include <cfg/discovery_agent.h>

#include <init/agent_param.h>
#include <init/agent_init.h>

#include <oper/operdb_init.h>
#include <oper/vrf.h>
#include <oper/multicast.h>
#include <oper/mirror_table.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <uve/agent_uve.h>
#include <ksync/ksync_init.h>
#include <kstate/kstate.h>
#include <pkt/proto.h>
#include <pkt/proto_handler.h>
#include <diag/diag.h>
#include <vgw/vgw.h>
#include <uve/agent_uve.h>

namespace opt = boost::program_options;

class FlowTest : public ::testing::Test {
public:
    virtual void SetUp() {
        desc.add_options()
            ("help", "help message")
            ("config-file", opt::value<string>(), "Configuration file")
            ("disable-vhost", "Create vhost interface")
            ("disable-ksync", "Disable kernel synchronization")
            ("disable-services", "Disable services")
            ("disable-packet", "Disable packet services")
            ("log-local", "Enable local logging of sandesh messages")
            ("log-level", opt::value<string>()->default_value("SYS_DEBUG"),
             "Severity level for local logging of sandesh messages")
            ("log-category", opt::value<string>()->default_value(""),
             "Category filter for local logging of sandesh messages")
            ("collector", opt::value<string>(), "IP address of sandesh collector")
            ("collector-port", opt::value<int>(), "Port of sandesh collector")
            ("http-server-port",
             opt::value<int>()->default_value(ContrailPorts::HttpPortAgent),
             "Sandesh HTTP listener port")
            ("host-name", opt::value<string>(), "Specific Host Name")
            ("log-file", opt::value<string>(),
             "Filename for the logs to be written to")
            ("hypervisor", opt::value<string>(), "Type of hypervisor <kvm|xen>")
            ("xen-ll-port", opt::value<string>(), 
             "Port name on host for link-local network")
            ("xen-ll-ip-address", opt::value<string>(),
             "IP Address for the link local port")
            ("xen-ll-prefix-len", opt::value<int>(),
             "Prefix for link local IP Address")
            ("version", "Display version information")
            ;
    }

    virtual void TearDown() {
    }

    opt::options_description desc;
    opt::variables_map var_map;
};

void RouterIdDepInit() {
}

TEST_F(FlowTest, Agent_Conf_file_1) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg.xml", "test-param",
               var_map);

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
    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.xml");
    EXPECT_STREQ(param.program_name().c_str(), "test-param");
}

TEST_F(FlowTest, Agent_Conf_Xen_1) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg-xen.xml", "test-param",
               var_map);

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
        (char *) "",
        (char *) "--config-file", 
                        (char *)"controller/src/vnsw/agent/init/test/cfg.xml",
        (char *) "--log-local",
        (char *) "--log-level",     (char *)"SYS_DEBUG",
        (char *) "--log-category",  (char *)"Test",
        (char *) "--collector",     (char *)"1.1.1.1",
        (char *) "--collector-port",(char *)"1000",
        (char *) "--http-server-port", (char *)"8000",
        (char *) "--host-name",     (char *)"vhost-1",
    };

    try {
        opt::store(opt::parse_command_line(argc, argv, desc), var_map);
        opt::notify(var_map);
    } catch (...) {
        cout << "Invalid arguments. ";
        cout << desc << endl;
        exit(0);
    }

    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg-xen.xml", "test-param",
               var_map);

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
    int argc = 8;
    char *argv[] = {
        (char *) "--config-file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg.xml",
        (char *) "--hypervisor",    (char *)"xen", 
        (char *) "--xen-ll-port",   (char *)"xenport",
        (char *) "--xen-ll-ip-address", (char *)"1.1.1.2",
        (char *) "--xen-ll-prefix-len", (char *)"16",
    };

    try {
        opt::store(opt::parse_command_line(argc, argv, desc), var_map);
        opt::notify(var_map);
    } catch (...) {
        cout << "Invalid arguments. ";
        cout << desc << endl;
        exit(0);
    }

    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg.xml", "test-param",
               var_map);

    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.xml");
    EXPECT_EQ(param.mode(), AgentParam::MODE_XEN);
    EXPECT_STREQ(param.xen_ll_name().c_str(), "xenport");
    EXPECT_EQ(param.xen_ll_addr().to_ulong(),
              Ip4Address::from_string("1.1.1.2").to_ulong());
    EXPECT_EQ(param.xen_ll_prefix().to_ulong(),
              Ip4Address::from_string("1.1.1.0").to_ulong());
    EXPECT_EQ(param.xen_ll_plen(), 24);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}
