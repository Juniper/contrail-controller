/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/resource.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"

#include <base/test/task_test_util.h>
#include <cfg/cfg_init.h>

#include <sandesh/sandesh_trace.h>

namespace opt = boost::program_options;
using std::map;
using std::string;

class AgentParamTest : public ::testing::Test {
public:
    virtual void SetUp()  { }

    virtual void TearDown() { }
};

TEST_F(AgentParamTest, Agent_Conf_file_1) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

    // QOS.priorty_tagging is not configured in cfg.ini. Verify the default
    // value of true is set
    EXPECT_TRUE(param.qos_priority_tagging());
    EXPECT_STREQ(param.vhost_name().c_str(), "vhost0");
    EXPECT_EQ(param.vhost_addr().to_ulong(),
              Ip4Address::from_string("10.1.1.1").to_ulong());
    EXPECT_EQ(param.vhost_plen(), 24);
    EXPECT_EQ(param.vhost_prefix().to_ulong(),
              Ip4Address::from_string("10.1.1.0").to_ulong());
    EXPECT_EQ(param.vhost_gw().to_ulong(),
              Ip4Address::from_string("10.1.1.254").to_ulong());
    EXPECT_STREQ(param.eth_port().c_str(), "vnet0");

    EXPECT_EQ(param.controller_server_list().size(), 1);
    std::vector<string>servers;
    boost::split(servers, param.controller_server_list()[0], boost::is_any_of(":"));
    EXPECT_STREQ("127.0.0.1", servers[0].c_str());

    EXPECT_EQ(param.dns_server_list().size(), 1);
    boost::split(servers, param.dns_server_list()[0], boost::is_any_of(":"));
    EXPECT_STREQ("127.0.0.1", servers[0].c_str());

    EXPECT_EQ(param.mgmt_ip().to_ulong(), 0);
    EXPECT_STREQ(param.tunnel_type().c_str(), "MPLSoGRE");
    EXPECT_EQ(param.dhcp_relay_mode(), true);
    EXPECT_STREQ(param.metadata_shared_secret().c_str(), "contrail");
    EXPECT_EQ(param.metadata_proxy_port(), 8998);
    EXPECT_EQ(param.dns_client_port(), 8997);
    EXPECT_EQ(param.mirror_client_port(), 8999);
    EXPECT_EQ(param.max_vm_flows(), 50.5);
    EXPECT_EQ(param.linklocal_system_flows(), 1024);
    EXPECT_EQ(param.linklocal_vm_flows(), 512);
    EXPECT_EQ(param.flow_cache_timeout(), 30);
    EXPECT_EQ(param.stale_interface_cleanup_timeout(), 120);
    EXPECT_STREQ(param.config_file().c_str(),
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_STREQ(param.program_name().c_str(), "test-param");
    EXPECT_EQ(param.agent_mode(), AgentParam::VROUTER_AGENT);
    EXPECT_STREQ(param.agent_base_dir().c_str(), "/var/lib/contrail");
    EXPECT_EQ(param.subnet_hosts_resolvable(), true);

    const std::vector<uint16_t> &ports = param.bgp_as_a_service_port_range_value();
    EXPECT_EQ(ports[0], 100);
    EXPECT_EQ(ports[1], 199);
    EXPECT_STREQ(param.config_file().c_str(),
                 "controller/src/vnsw/agent/init/test/cfg.ini");
    EXPECT_STREQ(param.program_name().c_str(), "test-param");
    EXPECT_EQ(param.agent_mode(), AgentParam::VROUTER_AGENT);
    EXPECT_STREQ(param.agent_base_dir().c_str(), "/var/lib/contrail");
    EXPECT_EQ(param.subnet_hosts_resolvable(), true);

    const std::vector<uint16_t> &ports2 = param.bgp_as_a_service_port_range_value();
    EXPECT_EQ(ports2[0], 100);
    EXPECT_EQ(ports2[1], 199);
    EXPECT_EQ(param.services_queue_limit(), 8192);
    EXPECT_EQ(param.bgpaas_max_shared_sessions(), 4);

    // By default, flow-tracing must be enabled
    EXPECT_TRUE(param.flow_trace_enable());
    EXPECT_EQ(param.pkt0_tx_buffer_count(), 2000);
    EXPECT_EQ(param.pkt0_tx_buffer_count(), 2000);
    EXPECT_EQ(param.get_nic_queue(1), 1);
    EXPECT_EQ(param.get_nic_queue(3), 1);
    EXPECT_EQ(param.get_nic_queue(8), 2);
    EXPECT_EQ(param.get_nic_queue(105), 8);
    EXPECT_FALSE(param.sandesh_config().disable_object_logs);

    // Logging parameters
    EXPECT_EQ(param.log_files_count(), kLogFilesCount);
    EXPECT_EQ(param.log_file_size(), kLogFileSize);
    EXPECT_STREQ(param.log_level().c_str(), "SYS_NOTICE");

    // watermark parameter
    EXPECT_EQ(param.vr_object_high_watermark(), 75.5);
}

TEST_F(AgentParamTest, Agent_Conf_file_2) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg1.ini", "test-param");

    // QOS.priorty_tagging is configured as true in cfg1.ini.
    EXPECT_TRUE(param.qos_priority_tagging());
    EXPECT_EQ(param.max_vm_flows(), 100);
    EXPECT_EQ(param.linklocal_system_flows(), 2048);
    EXPECT_EQ(param.linklocal_vm_flows(), 2048);

    std::vector<string>servers;
    EXPECT_EQ(param.controller_server_list().size(), 2);
    boost::split(servers, param.controller_server_list()[0], boost::is_any_of(":"));
    EXPECT_STREQ("11.1.1.1", servers[0].c_str());
    boost::split(servers, param.controller_server_list()[1], boost::is_any_of(":"));
    EXPECT_STREQ("12.1.1.1", servers[0].c_str());

    EXPECT_EQ(param.dns_server_list().size(), 2);
    boost::split(servers, param.dns_server_list()[0], boost::is_any_of(":"));
    EXPECT_STREQ("13.1.1.1", servers[0].c_str());
    boost::split(servers, param.dns_server_list()[1], boost::is_any_of(":"));
    EXPECT_STREQ("14.1.1.1", servers[0].c_str());

    EXPECT_EQ(param.agent_mode(), AgentParam::VROUTER_AGENT);
    EXPECT_EQ(param.dhcp_relay_mode(), false);
    EXPECT_EQ(param.subnet_hosts_resolvable(), false);
    EXPECT_EQ(param.metadata_proxy_port(), 8097);
    EXPECT_EQ(param.dns_client_port(), 8098);
    EXPECT_EQ(param.mirror_client_port(), 8097);
    // Default value for pkt0_tx_buffer_count
    EXPECT_EQ(param.pkt0_tx_buffer_count(), 1000);
    EXPECT_EQ(param.services_queue_limit(), 1024);
    EXPECT_TRUE(param.sandesh_config().disable_object_logs);

    EXPECT_EQ(param.huge_page_file_1G(0), "/var/lib/contrail/vrouter_flow_1G");
    EXPECT_EQ(param.huge_page_file_1G(1), "/var/lib/contrail/vrouter_bridge_1G");
    EXPECT_EQ(param.huge_page_file_2M(0), "/var/lib/contrail/vrouter_flow_2M");
    EXPECT_EQ(param.huge_page_file_2M(1), "/var/lib/contrail/vrouter_bridge_2M");

    // Logging parameters
    EXPECT_EQ(param.log_files_count(), 5);
    EXPECT_EQ(param.log_file_size(), 2048);
    EXPECT_STREQ(param.log_level().c_str(), "SYS_NOTICE");

    // watermark parameter range check
    EXPECT_EQ(param.vr_object_high_watermark(), 95);
}

TEST_F(AgentParamTest, Agent_Flows_Option_1) {
    int argc = 1;
    char *argv[] = {
        (char *) "",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/flows.ini", "test-param");
    EXPECT_EQ(param.flow_thread_count(), 4);
    EXPECT_EQ(param.max_vm_flows(), 50);
    EXPECT_EQ(param.linklocal_system_flows(), 1024);
    EXPECT_EQ(param.linklocal_vm_flows(), 512);
    EXPECT_FALSE(param.flow_trace_enable());
    EXPECT_TRUE(param.flow_use_rid_in_hash());
    EXPECT_EQ(param.flow_add_tokens(), 1000);
    EXPECT_EQ(param.flow_ksync_tokens(), 1000);
    EXPECT_EQ(param.flow_del_tokens(), 1000);
    EXPECT_EQ(param.flow_update_tokens(), 500);
}

TEST_F(AgentParamTest, Agent_Flows_Option_Arguments) {
    int argc = 21;
    char *argv[] = {
        (char *) "",
        (char *) "--FLOWS.thread_count",                   (char *)"8",
        (char *) "--FLOWS.max_vm_flows",                   (char *)"100",
        (char *) "--FLOWS.max_system_linklocal_flows",     (char *)"24",
        (char *) "--FLOWS.max_vm_linklocal_flows",         (char *)"20",
        (char *) "--FLOWS.trace_enable",                   (char *)"true",
        (char *) "--FLOWS.add_tokens",                     (char *)"2000",
        (char *) "--FLOWS.ksync_tokens",                   (char *)"2000",
        (char *) "--FLOWS.del_tokens",                     (char *)"2000",
        (char *) "--FLOWS.update_tokens",                  (char *)"1000",
        (char *) "--FLOWS.hash_exclude_router_id",         (char *)"true",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/flows.ini", "test-param");

    EXPECT_EQ(param.flow_thread_count(), 8);
    EXPECT_EQ(param.max_vm_flows(), 100);
    EXPECT_EQ(param.linklocal_system_flows(), 24);
    EXPECT_EQ(param.linklocal_vm_flows(), 20);
    EXPECT_TRUE(param.flow_trace_enable());
    EXPECT_FALSE(param.flow_use_rid_in_hash());
    EXPECT_EQ(param.flow_add_tokens(), 2000);
    EXPECT_EQ(param.flow_ksync_tokens(), 2000);
    EXPECT_EQ(param.flow_del_tokens(), 2000);
    EXPECT_EQ(param.flow_update_tokens(), 1000);
}

TEST_F(AgentParamTest, Agent_Tbb_Option_1) {
    int argc = 1;
    char *argv[] = {
        (char *) "",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/tbb.ini", "test-param");

    EXPECT_EQ(param.tbb_thread_count(), 8);
    EXPECT_EQ(param.tbb_exec_delay(), 10);
    EXPECT_EQ(param.tbb_schedule_delay(), 25);
    EXPECT_EQ(param.tbb_keepawake_timeout(), 50);
    EXPECT_STREQ(param.ksync_thread_cpu_pin_policy().c_str(), "last");
}

TEST_F(AgentParamTest, Agent_Tbb_Option_Arguments) {
    int argc = 11;
    char *argv[] = {
        (char *) "",
        (char *) "--TASK.thread_count",                 (char *)"4",
        (char *) "--TASK.log_exec_threshold",           (char *)"100",
        (char *) "--TASK.log_schedule_threshold",      (char *)"200",
        (char *) "--TASK.tbb_keepawake_timeout",      (char *)"300",
        (char *) "--TASK.ksync_thread_cpu_pin_policy", (char *)"2",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/tbb.ini", "test-param");

    EXPECT_EQ(param.tbb_thread_count(), 4);
    EXPECT_EQ(param.tbb_exec_delay(), 100);
    EXPECT_EQ(param.tbb_schedule_delay(), 200);
    EXPECT_EQ(param.tbb_keepawake_timeout(), 300);
    EXPECT_STREQ(param.ksync_thread_cpu_pin_policy().c_str(), "2");
}

// Check that linklocal flows are updated when the system limits are lowered
TEST_F(AgentParamTest, Agent_Conf_file_3) {
    struct rlimit rl;
    rl.rlim_max = 1024;
    rl.rlim_cur = 512;
    int result = setrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        AgentParam param;
        param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

        const std::vector<uint16_t> &ports =
            param.bgp_as_a_service_port_range_value();
        EXPECT_EQ(ports[0], 100);
        EXPECT_EQ(ports[1], 199);
        EXPECT_EQ(param.bgpaas_max_shared_sessions(), 4);

        EXPECT_EQ(param.linklocal_system_flows(), 511);
        EXPECT_EQ(param.linklocal_vm_flows(), 511);
    }
}

TEST_F(AgentParamTest, Agent_Conf_file_4) {
    struct rlimit rl;
    rl.rlim_max = 32;
    rl.rlim_cur = 32;
    int result = setrlimit(RLIMIT_NOFILE, &rl);
    if (result == 0) {
        AgentParam param;
        param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

        EXPECT_EQ(param.linklocal_system_flows(), 0);
        EXPECT_EQ(param.linklocal_vm_flows(), 0);
    }
}

TEST_F(AgentParamTest, Agent_Conf_Xen_1) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg-xen.ini", "test-param");

    EXPECT_STREQ(param.xen_ll_name().c_str(), "xenapi");
    EXPECT_EQ(param.xen_ll_addr().to_ulong(),
              Ip4Address::from_string("169.254.0.1").to_ulong());
    EXPECT_EQ(param.xen_ll_prefix().to_ulong(),
              Ip4Address::from_string("169.254.0.0").to_ulong());
    EXPECT_EQ(param.xen_ll_plen(), 24);
}

TEST_F(AgentParamTest, Agent_Param_1) {
    int argc = 24;
    char *argv[] = {
        (char *) "",
        (char *) "--config_file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg.ini",
        (char *) "--DEFAULT.collectors",     (char *)"1.1.1.1:1000",
        (char *) "--DEFAULT.derived_stats",  (char *)"DSStruct.dsattr:dsparam",
        (char *) "--DEFAULT.log_local",
        (char *) "--DEFAULT.log_flow",
        (char *) "--DEFAULT.log_level",     (char *)"SYS_DEBUG",
        (char *) "--DEFAULT.log_category",  (char *)"Test",
        (char *) "--DEFAULT.http_server_port", (char *)"8000",
        (char *) "--DEFAULT.hostname",     (char *)"vhost-1",
        (char *) "--DEFAULT.dhcp_relay_mode",     (char *)"true",
        (char *) "--DEFAULT.agent_base_directory",     (char *)"/var/run/contrail",
        (char *) "--DEFAULT.pkt0_tx_buffers",  (char *)"3000",
        (char *) "--SANDESH.disable_object_logs",
    };

    AgentParam param;
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

    EXPECT_EQ(param.derived_stats_map().size(), 1);
    map<string, map<string, string> > dsmap = param.derived_stats_map();
    string dsparam = dsmap.at("DSStruct").at("dsattr");
    EXPECT_STREQ(dsparam.c_str(), "dsparam");

    EXPECT_EQ(param.http_server_port(), 8000);
    EXPECT_STREQ(param.host_name().c_str(), "vhost-1");
    EXPECT_EQ(param.dhcp_relay_mode(), true);
    EXPECT_STREQ(param.agent_base_dir().c_str(), "/var/run/contrail");
    EXPECT_EQ(param.pkt0_tx_buffer_count(), 3000);
    EXPECT_TRUE(param.sandesh_config().disable_object_logs);
}

TEST_F(AgentParamTest, Agent_Arg_Override_Config_1) {
    int argc = 9;
    char *argv[] = {
        (char *) "",
        (char *) "--config_file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg.ini",
        (char *) "--HYPERVISOR.type",    (char *)"xen",
        (char *) "--HYPERVISOR.xen_ll_interface",   (char *)"xenport",
        (char *) "--HYPERVISOR.xen_ll_ip", (char *)"1.1.1.2/16",
    };

    AgentParam param;
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

TEST_F(AgentParamTest, Agent_Arg_Override_Config_2) {
    int argc = 7;
    char *argv[] = {
        (char *) "",
        (char *) "--CONTROL-NODE.servers",    (char *)"20.1.1.1:500", (char *)"21.1.1.1:15001",
        (char *) "--DNS.servers",   (char *)"22.1.1.1:53", (char *)"23.1.1.1:53",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

    std::vector<string>servers;
    EXPECT_EQ(param.controller_server_list().size(), 2);
    boost::split(servers, param.controller_server_list()[0], boost::is_any_of(":"));
    EXPECT_STREQ("20.1.1.1", servers[0].c_str());
    boost::split(servers, param.controller_server_list()[1], boost::is_any_of(":"));
    EXPECT_STREQ("21.1.1.1", servers[0].c_str());

    EXPECT_EQ(param.dns_server_list().size(), 2);
    boost::split(servers, param.dns_server_list()[0], boost::is_any_of(":"));
    EXPECT_STREQ("22.1.1.1", servers[0].c_str());
    boost::split(servers, param.dns_server_list()[1], boost::is_any_of(":"));
    EXPECT_STREQ("23.1.1.1", servers[0].c_str());
}

/* Some command line args have default values. If user has not passed these
 * command line args, but has specified values in config file, then values
 * specified config file should be taken */
TEST_F(AgentParamTest, Default_Cmdline_arg1) {
    int argc = 3;
    char *argv[] = {
        (char *) "",
        (char *) "--config_file",
                        (char *)"controller/src/vnsw/agent/init/test/cfg-default1.ini",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg-default1.ini",
               "test-param");
    EXPECT_EQ(param.flow_cache_timeout(), 60);
    EXPECT_EQ(param.stale_interface_cleanup_timeout(), 60);
    EXPECT_EQ(param.http_server_port(), 10001);
    EXPECT_STREQ(param.log_category().c_str(), "abc");
    EXPECT_STREQ(param.log_file().c_str(), "/var/log/contrail/vrouter2.log");
    EXPECT_STREQ(param.log_level().c_str(), "SYS_ERR");
    EXPECT_TRUE(param.isXenMode());
    EXPECT_EQ(param.agent_mode(), AgentParam::TSN_AGENT);
    EXPECT_FALSE(param.use_syslog());
    EXPECT_FALSE(param.log_flow());
    EXPECT_FALSE(param.log_local());
    EXPECT_EQ(param.vmi_vm_vn_uve_interval(), Agent::kDefaultVmiVmVnUveInterval);
    EXPECT_STREQ(param.subcluster_name().c_str(), "");
}

/* Some command line args have default values. If user has not passed these
 * command line args, and has NOT specified values in config file, then
 * verify that default value from command line args is picked up */
TEST_F(AgentParamTest, Default_Cmdline_arg2) {
    uint16_t http_server_port = ContrailPorts::HttpPortAgent();
    uint16_t flow_timeout = Agent::kDefaultFlowCacheTimeout;
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg-default2.ini",
               "test-param");
    EXPECT_EQ(param.flow_cache_timeout(), flow_timeout);
    EXPECT_EQ(param.stale_interface_cleanup_timeout(), 60);
    EXPECT_EQ(param.http_server_port(), http_server_port);
    EXPECT_STREQ(param.log_category().c_str(), "");
    EXPECT_STREQ(param.log_file().c_str(),
                 Agent::GetInstance()->log_file().c_str());
    EXPECT_STREQ(param.log_level().c_str(), "SYS_NOTICE");
    EXPECT_TRUE(param.isKvmMode());
    EXPECT_EQ(param.agent_mode(), AgentParam::TOR_AGENT);
    EXPECT_TRUE(param.use_syslog());
    EXPECT_TRUE(param.log_flow());
    EXPECT_TRUE(param.log_local());
    // Default for high watermark is 80
    EXPECT_EQ(param.vr_object_high_watermark(), 80);
}

/* Some command line args have default values. If user has explicitly passed
 * values for these command line args and has also specified values in config
 * file, then values specified on command line should be taken */
TEST_F(AgentParamTest, Default_Cmdline_arg3) {
    int argc = 11;
    char *argv[] = {
        (char *) "",
        (char *) "--DEFAULT.flow_cache_timeout", (char *)"100",
        (char *) "--DEFAULT.stale_interface_cleanup_timeout", (char *)"200",
        (char *) "--DEFAULT.http_server_port", (char *)"20001",
        (char *) "--DEFAULT.log_file", (char *)"3.log",
        (char *) "--HYPERVISOR.type", (char *)"vmware",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg-default1.ini",
               "test-param");
    EXPECT_EQ(param.flow_cache_timeout(), 100);
    EXPECT_EQ(param.stale_interface_cleanup_timeout(), 200);
    EXPECT_EQ(param.http_server_port(), 20001);
    EXPECT_STREQ(param.log_file().c_str(), "3.log");
    EXPECT_TRUE(param.isVmwareMode());
    EXPECT_EQ(param.agent_mode(), AgentParam::TSN_AGENT);
    // QOS.priorty_tagging is configured as false in cfg-default1.ini.
    EXPECT_FALSE(param.qos_priority_tagging());
    EXPECT_EQ(param.min_aap_prefix_len(), 24);
    EXPECT_EQ(param.max_sessions_per_aggregate(), 100);
    EXPECT_EQ(param.max_aggregates_per_session_endpoint(), 8);
    EXPECT_EQ(param.max_endpoints_per_session_msg(), 5);
}

TEST_F(AgentParamTest, MultitokenVector) {
    int argc = 3;
    char *argv[argc];
    char argv_0[] = "";
    char argv_1[] = "--DEFAULT.collectors=10.10.10.1:100 20.20.20.2:200";
    char argv_2[] = "--DEFAULT.collectors=30.30.30.3:300";
    argv[0] = argv_0;
    argv[1] = argv_1;
    argv[2] = argv_2;

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");

    vector<string> collector_server_list;
    collector_server_list.push_back("10.10.10.1:100");
    collector_server_list.push_back("20.20.20.2:200");
    collector_server_list.push_back("30.30.30.3:300");
    TASK_UTIL_EXPECT_VECTOR_EQ(param.collector_server_list(),
                     collector_server_list);
    EXPECT_EQ(param.min_aap_prefix_len(), 20);
    EXPECT_EQ(param.max_sessions_per_aggregate(), 80);
    EXPECT_EQ(param.max_aggregates_per_session_endpoint(), 4);
    EXPECT_EQ(param.max_endpoints_per_session_msg(), 2);
    EXPECT_EQ(param.vmi_vm_vn_uve_interval(), 120);
    EXPECT_STREQ(param.subcluster_name().c_str(), "id123");
}

TEST_F(AgentParamTest, Restart_1) {
    int argc = 13;
    char *argv[] = {
        (char *) "",
        (char *) "--RESTART.backup_enable", (char *)"true",
        (char *) "--RESTART.backup_idle_timeout", (char *)"20",
        (char *) "--RESTART.backup_dir", (char *)"/tmp/2",
        (char *) "--RESTART.backup_count", (char *)"20",
        (char *) "--RESTART.restore_enable", (char *)"true",
        (char *) "--RESTART.restore_audit_timeout", (char *)"20"
    };

    // Config file without RESTART section
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini",
               "test-param");
    EXPECT_TRUE(param.restart_backup_enable() == false);
    EXPECT_EQ(param.restart_backup_idle_timeout(), CFG_BACKUP_IDLE_TIMEOUT);
    EXPECT_STREQ(param.restart_backup_dir().c_str(), CFG_BACKUP_DIR);
    EXPECT_EQ(param.restart_backup_count(), CFG_BACKUP_COUNT);
    EXPECT_TRUE(param.restart_restore_enable());
    EXPECT_EQ(param.restart_restore_audit_timeout(), CFG_RESTORE_AUDIT_TIMEOUT);

    // Parameters from config-file
    param.Init("controller/src/vnsw/agent/init/test/restart.ini",
               "test-param");
    EXPECT_FALSE(param.restart_backup_enable());
    EXPECT_EQ(param.restart_backup_idle_timeout(), 10);
    EXPECT_STREQ(param.restart_backup_dir().c_str(), "/tmp/1");
    EXPECT_EQ(param.restart_backup_count(), 10);
    EXPECT_FALSE(param.restart_restore_enable());
    EXPECT_EQ(param.restart_restore_audit_timeout(), 10);

    // Parameters from command line arguments
    AgentParam param1;
    param1.ParseArguments(argc, argv);
    param1.Init("controller/src/vnsw/agent/init/test/restart.ini", "test-param");
    param1.ParseArguments(argc, argv);
    EXPECT_TRUE(param1.restart_backup_enable());
    EXPECT_EQ(param1.restart_backup_idle_timeout(), 20);
    EXPECT_STREQ(param1.restart_backup_dir().c_str(), "/tmp/2");
    EXPECT_EQ(param1.restart_backup_count(), 20);
    EXPECT_TRUE(param1.restart_restore_enable());
    EXPECT_EQ(param1.restart_restore_audit_timeout(), 20);
}

TEST_F(AgentParamTest, Agent_Mac_Learning_Option_1) {
    int argc = 1;
    char *argv[] = {
        (char *) "",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/mac_learning.ini", "test-param");
    EXPECT_EQ(param.mac_learning_thread_count(), 10);
    EXPECT_EQ(param.mac_learning_add_tokens(), 500);
    EXPECT_EQ(param.mac_learning_update_tokens(), 510);
    EXPECT_EQ(param.mac_learning_delete_tokens(), 520);
}

TEST_F(AgentParamTest, Agent_Crypt_Config) {
    AgentParam param;
    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");
    EXPECT_STREQ(param.crypt_port().c_str(), "ipsec0");
}

TEST_F(AgentParamTest, Agent_Session_Destination_Option_Arguments) {
    int argc = 3;
    char *argv[] = {
        (char *) "",
        (char *) "--SESSION.slo_destination", (char *) "collector file",
        (char *) "--SESSION.sample_destination", (char *) "file",
    };

    AgentParam param;
    param.ParseArguments(argc, argv);
    param.Init("controller/src/vnsw/agent/init/test/flows.ini", "test-param");
    vector<string> slo_destination_list;
    slo_destination_list.push_back(string("collector"));
    slo_destination_list.push_back(string("file"));
    TASK_UTIL_EXPECT_VECTOR_EQ(param.get_slo_destination(),
                     slo_destination_list);
    // when options are unspecified, the sessions are sent only to collector
    argc = 1;
    char *argv1[] = {
        (char *) "",
    };
    AgentParam p;
    p.ParseArguments(argc, argv1);
    p.Init("controller/src/vnsw/agent/init/test/flows.ini", "test-param");
    slo_destination_list.pop_back();
    TASK_UTIL_EXPECT_VECTOR_EQ(p.get_slo_destination(),
                     slo_destination_list);
    // when empty list is passed for destination, slo_destn_list is empty
    argc = 2;
    char *argv2[] = {
        (char *) "",
        (char *) "--SESSION.slo_destination", (char *) "",
    };
    AgentParam p1;
    p1.ParseArguments(argc, argv2);
    p1.Init("controller/src/vnsw/agent/init/test/flows.ini", "test-param");
    slo_destination_list.pop_back();
    TASK_UTIL_EXPECT_VECTOR_EQ(p1.get_slo_destination(),
                     slo_destination_list);

}

TEST_F(AgentParamTest, Agent_Debug_Trace_Options) {
    AgentParam param;
    SandeshTraceBufferPtr trace_buf;

    param.Init("controller/src/vnsw/agent/init/test/cfg.ini", "test-param");
    param.DebugInit();

    //Verify if the default value is set for parameter not configured in
    //config file
    EXPECT_EQ(SandeshTraceBufferCapacityGet("Config"), 2000);
    trace_buf = SandeshTraceBufferGet("Config");
    EXPECT_EQ(SandeshTraceBufferSizeGet(trace_buf), 2000);

    //Verify if the increased configured value is set for parameter configured
    //in config file
    EXPECT_EQ(SandeshTraceBufferCapacityGet("Xmpp"), 50000);
    trace_buf = SandeshTraceBufferGet("Xmpp");
    EXPECT_EQ(SandeshTraceBufferSizeGet(trace_buf), 50000);

    //Reduce the value of the tracebuffer for Xmpp
    param.trace_buff_size_map["xmpp"] = 5000;
    trace_buf = SandeshTraceBufferResetSize("Xmpp",
                                            param.trace_buff_size_map["xmpp"]);

    //Verify if the change is reflected in sandesh library.
    EXPECT_EQ(SandeshTraceBufferCapacityGet("Xmpp"), 5000);
    trace_buf = SandeshTraceBufferGet("Xmpp");
    EXPECT_EQ(SandeshTraceBufferSizeGet(trace_buf), 5000);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}
