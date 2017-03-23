/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <base/connection_info.h>
#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <uve/agent_uve.h>
#include <uve/stats_interval_types.h>
#include <uve/agent_stats_interval_types.h>
#include <vrouter/flow_stats/flow_stats_types.h>
#include "vr_types.h"

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "xmpp/test/xmpp_test_util.h"
#include <uve/test/vn_uve_table_test.h>
#include <uve/test/vm_uve_table_test.h>
#include "ksync/ksync_sock_user.h"
#include "uve/test/test_uve_util.h"
#include "agent_param_test.h"

int vrf_array[] = {1, 2};

using namespace std;
using namespace process;
using process::g_process_info_constants;

struct ConnectionInfoInput {
    string type;
    string name;
    string server_addrs;
    string status;
};

void RouterIdDepInit(Agent *agent) {
}

class UveTest : public ::testing::Test {
public:
    UveTest() : agent_(Agent::GetInstance()) { }
    ~UveTest() { }
    static void TestSetup(int *vrf, int count) {
        char vrf_name[80];
        for (int i = 0; i < count; i++) {
            sprintf(vrf_name, "vrf%d", vrf[i]);
            AddVrf(vrf_name);
        }
    }

    void BuildConnectionInfo(std::vector<ConnectionInfo> &infos,
                             ConnectionInfoInput *input, int size) {
        for (int i = 0; i < size; i++) {
            ConnectionInfo info;
            std::vector<string> addr_list;
            info.set_type(input[i].type);
            info.set_name(input[i].name);
            addr_list.push_back(input[i].server_addrs);
            info.set_server_addrs(addr_list);
            info.set_status(input[i].status);
            infos.push_back(info);
        }
    }

    void GetProcessState(const std::vector<ConnectionInfo> &infos,
                         ProcessState::type &pstate, string &msg) {
        AgentUveBase *uve = Agent::GetInstance()->uve();
        uve->VrouterAgentProcessState(infos, pstate, msg);
    }

    void SetAgentStatsIntervalReq(int interval) {
        SetAgentStatsInterval_InSeconds *req = new SetAgentStatsInterval_InSeconds();
        req->set_interval(interval);
        Sandesh::set_response_callback(
            boost::bind(&UveTest::SetAgentStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void SetFlowStatsIntervalReq(int interval) {
        SetFlowStatsInterval_InSeconds *req = new SetFlowStatsInterval_InSeconds();
        req->set_interval(interval);
        Sandesh::set_response_callback(
            boost::bind(&UveTest::SetFlowStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void SetAgentStatsResponse(Sandesh *sandesh) {
        if (memcmp(sandesh->Name(), "AgentStatsCfgResp",
                   strlen("AgentStatsCfgResp")) == 0) {
            success_responses_++;
        } else if (memcmp(sandesh->Name(), "AgentStatsCfgErrResp",
                   strlen("AgentStatsCfgErrResp")) == 0) {
            error_responses_++;
        }
    }

    void SetFlowStatsResponse(Sandesh *sandesh) {
        if (memcmp(sandesh->Name(), "FlowStatsCfgResp",
                   strlen("FlowStatsCfgResp")) == 0) {
            success_responses_++;
        } else if (memcmp(sandesh->Name(), "FlowStatsCfgErrResp",
                   strlen("FlowStatsCfgErrResp")) == 0) {
            error_responses_++;
        }
    }

    //void GetStatsIntervalReq() {
    void GetAgentStatsIntervalReq() {
        GetAgentStatsInterval *req = new GetAgentStatsInterval();
        Sandesh::set_response_callback(
            boost::bind(&UveTest::GetAgentStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void GetAgentStatsResponse(Sandesh *sandesh) {
        if (memcmp(sandesh->Name(), "AgentStatsIntervalResp_InSeconds", 
                   strlen("AgentStatsIntervalResp_InSeconds")) == 0) {
            success_responses_++;
            AgentStatsIntervalResp_InSeconds *resp = static_cast
                <AgentStatsIntervalResp_InSeconds *>(sandesh);
            agent_stats_interval_ = resp->get_agent_stats_interval();
        }
    }

    void GetFlowStatsIntervalReq() {
        GetFlowStatsInterval *req = new GetFlowStatsInterval();
        Sandesh::set_response_callback(
            boost::bind(&UveTest::GetFlowStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void GetFlowStatsResponse(Sandesh *sandesh) {
        if (memcmp(sandesh->Name(), "FlowStatsIntervalResp_InSeconds", 
                   strlen("FlowStatsIntervalResp_InSeconds")) == 0) {
            success_responses_++;
            FlowStatsIntervalResp_InSeconds *resp = static_cast
                <FlowStatsIntervalResp_InSeconds *>(sandesh);
            flow_stats_interval_ = resp->get_flow_stats_interval();
        }
    }

    static void CreateVmPorts(struct PortInfo *input, int count) {
        CreateVmportEnv(input, count);
    }

    void ClearCounters() {
        success_responses_ = error_responses_ = 0;
    }

    Agent *agent_;
    int success_responses_;
    int error_responses_;
    int agent_stats_interval_;
    int flow_stats_interval_;
    TestUveUtil util_;
};

TEST_F(UveTest, VmAddDelTest1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", vrf_array[0], 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", vrf_array[1], 2},
    };
    client->Reset();
    //Create VM, VN, VRF and Vmport
    CreateVmPorts(input, 2);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, (VmPortActive(input, 0) == true));
    WAIT_FOR(500, 1000, (VmPortActive(input, 1) == true));
    EXPECT_EQ(2U, PortSubscribeSize(agent_));

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Two entries in VM and VN map
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 1U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm1")) == 1U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm2")) == 1U);

    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine", "vm1");
    client->VmDelNotifyWait(1);
    client->WaitForIdle(2);
    //One Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 0));
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 0U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm1")) == 0U);

    DelLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet2");
    DelLink("virtual-network", "vn2", "virtual-machine-interface", "vnet2");
    DelNode("virtual-machine", "vm2");
    client->VmDelNotifyWait(2);
    client->WaitForIdle(2);
    //second Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 1));
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 0U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm2")) == 0U);
    client->WaitForIdle(2);

    //Cleanup other things created by this test-case
    DeleteVmportEnv(input, 2, 1);
    client->WaitForIdle(2);
    client->VnDelNotifyWait(2);
    client->PortDelNotifyWait(2);
    
    WAIT_FOR(100, 1000, (PortSubscribeSize(agent_) == 0));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->vm_table()->Size() == 0));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->vn_table()->Size() == 0));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") == NULL));
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn2") == NULL));
}

TEST_F(UveTest, VnAddDelTest1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", vrf_array[0], 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", vrf_array[1], 2},
    };
    client->Reset();
    //Create VM, VN, VRF and Vmport
    CreateVmportEnv(input, 2);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, (VmPortActive(input, 0) == true));
    WAIT_FOR(500, 1000, (VmPortActive(input, 1) == true));
    EXPECT_EQ(2U, PortSubscribeSize(agent_));

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Two entries in VM and VN map
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 1U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm1")) == 1U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm2")) == 1U);

    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle(2);
    client->VnDelNotifyWait(1);
    //One Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 0));
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 0U);

    DelLink("virtual-network", "vn2", "virtual-machine-interface", "vnet2");
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelNode("virtual-network", "vn2");
    client->WaitForIdle(2);
    client->VnDelNotifyWait(2);
    //Second Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 1));
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 0U);

    //Cleanup other things created by this test-case
    DeleteVmportEnv(input, 2, 1);
    client->WaitForIdle(2);
    client->VmDelNotifyWait(2);
    client->PortDelNotifyWait(2);

    WAIT_FOR(500, 1000, (PortSubscribeSize(agent_) == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vn_table()->Size() == 0));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") == NULL));
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn2") == NULL));
}

TEST_F(UveTest, IntAddDelTest1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", vrf_array[0], 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", vrf_array[1], 2},
    };
    client->Reset();

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Create VM, VN, VRF and Vmport
    CreateVmPorts(input, 2);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, (VmPortActive(input, 0) == true));
    WAIT_FOR(500, 1000, (VmPortActive(input, 1) == true));
    EXPECT_EQ(2U, PortSubscribeSize(agent_));
    //Two entries in VM and VN map
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 1U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm1")) == 1U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm2")) == 1U);

    IntfCfgDel(input, 0);
    client->WaitForIdle(2);
    client->PortDelNotifyWait(1);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 0U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm1")) == 0U);

    IntfCfgDel(input, 1);
    client->WaitForIdle(2);
    client->PortDelNotifyWait(2);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 0U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm2")) == 0U);

    //Cleanup other things created by this test-case
    DeleteVmportEnv(input, 2, 1);
    client->WaitForIdle(2);
    client->VnDelNotifyWait(2);
    client->VmDelNotifyWait(2);

    WAIT_FOR(500, 1000, (PortSubscribeSize(agent_) == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vm_table()->Size() == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->vn_table()->Size() == 0));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") == NULL));
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn2") == NULL));
}

//To verify VRF addition/removal keeps the vrf stats tree in correct state
TEST_F(UveTest, VrfAddDelTest_1) {
    //Create vrf - vrf11
    VrfAddReq("vrf11");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf11")== true));
    EXPECT_TRUE(DBTableFind("vrf11.uc.route.0"));
 
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf11");
    EXPECT_TRUE(vrf != NULL);
    int vrf11_id = vrf->vrf_id();

    vr_vrf_stats_req zero_stats;
    //Verify that vrf_stats entry is added vrf_stats_tree of 
    //agent_stats_collector
    EXPECT_TRUE(VrfStatsMatch(vrf11_id, string("vrf11"), true, zero_stats));

    //Delete vrf - vrf11
    VrfDelReq("vrf11");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf11")== false));
    EXPECT_FALSE(DBTableFind("vrf11.uc.route.0"));

    //Verify that vrf_stats entry is not removed from vrf_stats_tree of 
    //agent_stats_collector after deletion of vrf
    EXPECT_TRUE(VrfStatsMatch(vrf11_id, string("vrf11"), true, zero_stats));

    //Create vrf - vrf21
    VrfAddReq("vrf21");
    client->WaitForIdle();
    EXPECT_TRUE(DBTableFind("vrf21.uc.route.0"));
 
    vrf = Agent::GetInstance()->vrf_table()->FindVrfFromName("vrf21");
    EXPECT_TRUE(vrf != NULL);
    int vrf21_id = vrf->vrf_id();
    LOG(DEBUG, "vrf 11 " << vrf11_id << " vrf 21 " << vrf21_id);
    if (vrf11_id == vrf21_id) {
        //When vrf-id is re-used for different vrf verify that vrf-name
        //is updated correctly in the vrf_stats_entry of agent_stats_collector
        EXPECT_TRUE(VrfStatsMatch(vrf11_id, string("vrf21"), true, zero_stats));
    }

    //Delete vrf - vrf21
    VrfDelReq("vrf21");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf21")== false));
    EXPECT_FALSE(DBTableFind("vrf21.uc.route.0"));
    EXPECT_TRUE(VrfStatsMatch(vrf21_id, string("vrf21"), true, zero_stats));
}

TEST_F(UveTest, StatsCollectorTest) {
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->stats_collector());
    collector->interface_stats_responses_ = 0;
    collector->vrf_stats_responses_ = 0;
    collector->drop_stats_responses_ = 0;
    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);

    EXPECT_EQ(0, collector->interface_stats_errors_);
    EXPECT_EQ(0, collector->vrf_stats_errors_);
    EXPECT_EQ(0, collector->drop_stats_errors_);
    EXPECT_EQ(1, collector->interface_stats_responses_);
    EXPECT_EQ(1, collector->vrf_stats_responses_);
    EXPECT_EQ(1, collector->drop_stats_responses_);

    //Set error_code in mock code so that all dump requests will return this code.
    KSyncSockTypeMap::set_error_code(EBUSY);
    collector->interface_stats_responses_ = 0;
    collector->vrf_stats_responses_ = 0;
    collector->drop_stats_responses_ = 0;

    //Enqueue stats-collector-task
    util_.EnqueueAgentStatsCollectorTask(1);

    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Verify that Error handlers for agent_stats_collector is invoked.
    WAIT_FOR(100, 1000, (collector->interface_stats_errors_ == 1));
    WAIT_FOR(100, 1000, (collector->vrf_stats_errors_ == 1));
    WAIT_FOR(100, 1000, (collector->drop_stats_errors_ == 1));
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ == 1));
    WAIT_FOR(100, 1000, (collector->vrf_stats_responses_ == 1));
    WAIT_FOR(100, 1000, (collector->drop_stats_responses_ == 1));

    //Reset the error code in mock code
    KSyncSockTypeMap::set_error_code(0);
    collector->interface_stats_responses_ = 0;
    collector->interface_stats_errors_ = 0;
    collector->vrf_stats_responses_ = 0;
    collector->vrf_stats_errors_ = 0;
    collector->drop_stats_responses_ = 0;
    collector->drop_stats_errors_ = 0;

    //Enqueue stats-collector-task
    util_.EnqueueAgentStatsCollectorTask(1);

    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Verify that Error handlers for agent_stats_collector is NOT invoked.
    WAIT_FOR(100, 1000, (collector->interface_stats_errors_ == 0));
    WAIT_FOR(100, 1000, (collector->vrf_stats_errors_ == 0));
    WAIT_FOR(100, 1000, (collector->drop_stats_errors_ == 0));
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ == 1));
    WAIT_FOR(100, 1000, (collector->vrf_stats_responses_ == 1));
    WAIT_FOR(100, 1000, (collector->drop_stats_responses_ == 1));

    //cleanup
    collector->interface_stats_responses_ = 0;
    collector->vrf_stats_responses_ = 0;
    collector->drop_stats_responses_ = 0;
}

TEST_F(UveTest, SandeshTest) {
    Agent *agent = Agent::GetInstance();
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->stats_collector());

    int agent_stats_interval = collector->expiry_time();
    int flow_stats_interval = agent->flow_stats_manager()->
        default_flow_stats_collector_obj()->GetExpiryTime();

    //Set Flow stats interval to invalid value
    ClearCounters();
    SetFlowStatsIntervalReq(-1);

    //Verify that flow stats interval has not changed
    EXPECT_EQ(1, error_responses_);
    EXPECT_EQ(0, success_responses_);
    EXPECT_EQ(flow_stats_interval, agent->flow_stats_manager()->
              default_flow_stats_collector_obj()->GetExpiryTime());

    //Set Agent stats interval to invalid value
    ClearCounters();
    SetAgentStatsIntervalReq(0);

    //Verify that agent stats interval has not changed
    EXPECT_EQ(1, error_responses_);
    EXPECT_EQ(0, success_responses_);
    EXPECT_EQ(agent_stats_interval, collector->expiry_time());

    //Set Flow stats interval to a valid value
    ClearCounters();
    SetFlowStatsIntervalReq(3);

    //Verify that flow stats interval has been updated
    EXPECT_EQ(0, error_responses_);
    EXPECT_EQ(1, success_responses_);
    EXPECT_EQ((3 * 1000), agent->flow_stats_manager()->
                          default_flow_stats_collector_obj()->GetExpiryTime());

    //Set Agent stats interval to a valid value
    ClearCounters();
    SetAgentStatsIntervalReq(40);

    //Verify that agent stats interval has been updated
    EXPECT_EQ(0, error_responses_);
    EXPECT_EQ(1, success_responses_);
    EXPECT_EQ((40 * 1000), collector->expiry_time());

    //Fetch agent stats interval via Sandesh
    ClearCounters();
    GetAgentStatsIntervalReq();

    //Verify the fetched agent/flow stats interval
    EXPECT_EQ(0, error_responses_);
    EXPECT_EQ(1, success_responses_);
    EXPECT_EQ(40, agent_stats_interval_);

    //Fetch agent stats interval via Sandesh
    ClearCounters();
    GetFlowStatsIntervalReq();

    //Verify the fetched agent/flow stats interval
    EXPECT_EQ(0, error_responses_);
    EXPECT_EQ(1, success_responses_);
    EXPECT_EQ(3, flow_stats_interval_);
}

/* Service IPs are not configured */
TEST_F(UveTest, NodeStatus_ExpectedConnections_0) {
    Agent *agent = Agent::GetInstance();
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent->uve());

    uint8_t num_c_nodes, num_d_servers;
    int expected_conns = uve->ExpectedConnections(num_c_nodes, num_d_servers);
    EXPECT_EQ(expected_conns, 0);
}

/* All service IPs are configured */
TEST_F(UveTest, NodeStatus_ExpectedConnections_1) {
    Agent *agent = Agent::GetInstance();
    AgentParamTest params(agent->params());
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent->uve());
    params.set_controller_server_list("1.1.1.1:5269");
    params.set_dns_server_list("1.1.1.1:53");
    params.set_collector_server_list("1.1.1.1:8086");
    agent->CopyConfig(agent->params());

    uint8_t num_c_nodes, num_d_servers;
    int expected_conns = uve->ExpectedConnections(num_c_nodes, num_d_servers);
    EXPECT_EQ(expected_conns, 3);
}

/* All service IPs are configured */
TEST_F(UveTest, NodeStatus_ExpectedConnections_3) {
    Agent *agent = Agent::GetInstance();
    AgentParamTest params(agent->params());
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent->uve());
    params.set_controller_server_list("1.1.1.1:5269");
    params.set_controller_server_list("1.1.1.2:5269");
    params.set_dns_server_list("1.1.1.1:53");
    params.set_dns_server_list("1.1.1.2:53");
    agent->CopyConfig(agent->params());

    uint8_t num_c_nodes, num_d_servers;
    int expected_conns = uve->ExpectedConnections(num_c_nodes, num_d_servers);
    EXPECT_EQ(expected_conns, 5);
}

/* Both control-node connections up. Agent should be functional */
TEST_F(UveTest, NodeStatus_Functional_1) {
    ConnectionInfoInput input[] = {
        {g_process_info_constants.ConnectionTypeNames.find(ConnectionType::XMPP)->second,
         "control-node:1.1.1.1", "1.1.1.1:0",
         g_process_info_constants.ConnectionStatusNames.find(ConnectionStatus::UP)->second},
        {g_process_info_constants.ConnectionTypeNames.find(ConnectionType::XMPP)->second,
         "control-node:1.1.1.2", "1.1.1.2:0",
         g_process_info_constants.ConnectionStatusNames.find(ConnectionStatus::UP)->second},
    };
    /* DelLinkLocalConfig will add emply global-vrouter-config stanza.
     * global-vrouter-config is required to make process state as functional
     */
    DelLinkLocalConfig();
    client->WaitForIdle();
    std::vector<ConnectionInfo> cinfos;
    ProcessState::type pstate;
    std::string msg;
    BuildConnectionInfo(cinfos, input, 2);
    EXPECT_EQ(2U, cinfos.size());
    GetProcessState(cinfos, pstate, msg);
    EXPECT_EQ(pstate, ProcessState::FUNCTIONAL);

    //cleanup
    DeleteGlobalVrouterConfig();
    client->WaitForIdle();
}

/* Only one control-node connection up. Agent should be functional */
TEST_F(UveTest, NodeStatus_Functional_2) {
    ConnectionInfoInput input[] = {
        {g_process_info_constants.ConnectionTypeNames.find(ConnectionType::XMPP)->second,
         "control-node:1.1.1.1", "1.1.1.1:0",
         g_process_info_constants.ConnectionStatusNames.find(ConnectionStatus::DOWN)->second},
        {g_process_info_constants.ConnectionTypeNames.find(ConnectionType::XMPP)->second,
         "control-node:1.1.1.2", "1.1.1.2:0",
         g_process_info_constants.ConnectionStatusNames.find(ConnectionStatus::UP)->second},
    };
    /* DelLinkLocalConfig will add emply global-vrouter-config stanza.
     * global-vrouter-config is required to make process state as functional
     */
    DelLinkLocalConfig();
    client->WaitForIdle();
    std::vector<ConnectionInfo> cinfos;
    ProcessState::type pstate;
    std::string msg;
    BuildConnectionInfo(cinfos, input, 2);
    EXPECT_EQ(2U, cinfos.size());
    GetProcessState(cinfos, pstate, msg);
    EXPECT_EQ(pstate, ProcessState::FUNCTIONAL);

    //cleanup
    DeleteGlobalVrouterConfig();
    client->WaitForIdle();
}

/* Both control-node connections down. Agent should be non-functional */
TEST_F(UveTest, NodeStatus_Functional_3) {
    ConnectionInfoInput input[] = {
        {g_process_info_constants.ConnectionTypeNames.find(ConnectionType::XMPP)->second,
         "control-node:1.1.1.1", "1.1.1.1:0",
         g_process_info_constants.ConnectionStatusNames.find(ConnectionStatus::DOWN)->second},
        {g_process_info_constants.ConnectionTypeNames.find(ConnectionType::XMPP)->second,
         "control-node:1.1.1.2", "1.1.1.2:0",
         g_process_info_constants.ConnectionStatusNames.find(ConnectionStatus::DOWN)->second},
    };
    std::vector<ConnectionInfo> cinfos;
    ProcessState::type pstate;
    std::string msg;
    BuildConnectionInfo(cinfos, input, 2);
    EXPECT_EQ(2U, cinfos.size());
    GetProcessState(cinfos, pstate, msg);
    EXPECT_EQ(pstate, ProcessState::NON_FUNCTIONAL);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    /* Services Module should be initialized because test cases which start with
     * NodeStatus_ExpectedConnections_* name check for expected connections and
     * these expected connections are dependent on services module to check for
     * number of dns-xmpp connections */
    client = TestInit("controller/src/vnsw/agent/uve/test/vnswa_cfg.ini",
                      ksync_init, true, true);
    UveTest::TestSetup(vrf_array, 2);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
