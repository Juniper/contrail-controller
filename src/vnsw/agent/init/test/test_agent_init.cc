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
        ("conf_file", opt::value<string>()->default_value(Agent::DefaultConfigFile()), 
         "Configuration file")
        ("disable-vhost", "Create vhost interface")
        ("disable-ksync", "Disable kernel synchronization")
        ("disable-services", "Disable services")
        ("disable-packet", "Disable packet services")
        ("COLLECTOR.server", opt::value<string>(), 
         "IP address of sandesh collector")
        ("COLLECTOR.port", opt::value<uint16_t>(), "Port of sandesh collector")
        ("CONTROL-NODE.server1", opt::value<string>(), 
         "IP address of first control node")
        ("CONTROL-NODE.server2", opt::value<string>(), 
         "IP address of second control node")
        ("DEFAULT.flow_cache_timeout", opt::value<uint16_t>(), 
         "Flow aging time in seconds")
        ("DEFAULT.hostname", opt::value<string>(), 
         "Hostname of compute-node")
        ("DEFAULT.http_server_port", opt::value<uint16_t>(), 
         "Sandesh HTTP listener port")
        ("DEFAULT.log_category", opt::value<string>(),
         "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_file", opt::value<string>(),
         "Filename for the logs to be written to")
        ("DEFAULT.log_level", opt::value<string>(),
         "Severity level for local logging of sandesh messages")
        ("DEFAULT.log_local", "Enable local logging of sandesh messages")
        ("DEFAULT.tunnel_type", opt::value<string>(),
         "Tunnel Encapsulation type <MPLSoGRE|MPLSoUDP|VXLAN>")
        ("DISCOVERY.server", opt::value<string>(), 
         "IP address of discovery server")
        ("DISCOVERY.max_control_nodes", opt::value<uint16_t>(), 
         "Maximum number of control node info to be provided by discovery service <1|2>")
        ("DNS.server1", opt::value<string>(), "IP address of first dns node")
        ("DNS.server2", opt::value<string>(), "IP address of second dns node")
        ("host-name", opt::value<string>(), "Specific Host Name")
        ("HYPERVISOR.type", opt::value<string>(), 
         "Type of hypervisor <kvm|xen|vmware>")
        ("HYPERVISOR.xen_ll_interface", opt::value<string>(), 
         "Port name on host for link-local network")
        ("HYPERVISOR.xen_ll_ip", opt::value<string>(),
         "IP Address and prefix or the link local port in ip/prefix format")
        ("HYPERVISOR.vmware_physical_port", opt::value<string>(),
         "Physical port used to connect to VMs in VMWare environment")
        ("LINK-LOCAL.max_system_flows", opt::value<uint16_t>(), 
         "Maximum number of link-local flows allowed across all VMs")
        ("LINK-LOCAL.max_vm_flows", opt::value<uint16_t>(), 
         "Maximum number of link-local flows allowed per VM")
        ("METADATA.metadata_proxy_secret", opt::value<string>(),
         "Shared secret for metadata proxy service")
        ("NETWORKS.control_network_ip", opt::value<string>(),
         "control-channel IP address used by WEB-UI to connect to vnswad")
        ("VIRTUAL-HOST-INTERFACE.name", opt::value<string>(),
         "Name of virtual host interface")
        ("VIRTUAL-HOST-INTERFACE.ip", opt::value<string>(), 
         "IP address and prefix in ip/prefix_len format")
        ("VIRTUAL-HOST-INTERFACE.gateway", opt::value<string>(), 
         "Gateway IP address for virtual host")
        ("VIRTUAL-HOST-INTERFACE.physical_interface", opt::value<string>(), 
         "Physical interface name to which virtual host interface maps to")
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
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param",
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
    EXPECT_EQ(param.linklocal_system_flows(), 1024);
    EXPECT_EQ(param.linklocal_vm_flows(), 512);
    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_STREQ(param.program_name().c_str(), "test-param");
}

TEST_F(FlowTest, Agent_Conf_file_2) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg1.ini", "test-param",
               var_map);

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
        AgentParam param;
        param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param",
                   var_map);

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
        AgentParam param;
        param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param",
                   var_map);

        EXPECT_EQ(param.linklocal_system_flows(), 0);
        EXPECT_EQ(param.linklocal_vm_flows(), 0);
    }
}

TEST_F(FlowTest, Agent_Conf_Xen_1) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg-xen.ini", "test-param",
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
        (char *) "--conf-file", 
                        (char *)"controller/src/vnsw/agent/init/test/cfg.ini",
        (char *) "--DEFAULT.log_local",
        (char *) "--DEFAULT.log_level",     (char *)"SYS_DEBUG",
        (char *) "--DEFAULT.log_category",  (char *)"Test",
        (char *) "--COLLECTOR.server",     (char *)"1.1.1.1",
        (char *) "--COLLECTOR.port",(char *)"1000",
        (char *) "--DEFAULT.http_server_port", (char *)"8000",
        (char *) "--DEFAULT.hostname",     (char *)"vhost-1",
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
    param.Init("controller/src/vnsw/agent/init/test/cfg-xen.ini", "test-param",
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
        (char *) "--conf_file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg.xml",
        (char *) "--HYPERVISOR.type",    (char *)"xen", 
        (char *) "--HYPERVISOR.xen_ll_interface",   (char *)"xenport",
        (char *) "--HYPERVISOR.xen_ll_ip", (char *)"1.1.1.2/16",
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
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param",
               var_map);

    EXPECT_STREQ(param.config_file().c_str(), 
                 "controller/src/vnsw/agent/init/test/cfg.ini");
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
