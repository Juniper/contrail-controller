/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap_agent_parser.h>
#include <ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <uve/agent_uve.h>
#include <uve/stats_interval_types.h>
#include "vr_types.h"

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "xmpp/test/xmpp_test_util.h"
#include <uve/test/vn_uve_table_test.h>
#include <uve/test/vm_uve_table_test.h>
#include "ksync/ksync_sock_user.h"

int vrf_array[] = {1, 2};

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

class AgentStatsCollectorTask : public Task {
public:
    AgentStatsCollectorTask(int count) : 
        Task((TaskScheduler::GetInstance()->GetTaskId
            ("Agent::StatsCollector")), StatsCollector::AgentStatsCollector),
        count_(count) {
    }
    virtual bool Run() {
        for (int i = 0; i < count_; i++)
            Agent::GetInstance()->uve()->agent_stats_collector()->Run();
        return true;
    }
private:
    int count_;
};

class UveTest : public ::testing::Test {
public:
    static void TestSetup(int *vrf, int count) {
        char vrf_name[80];
        for (int i = 0; i < count; i++) {
            sprintf(vrf_name, "vrf%d", vrf[i]);
            AddVrf(vrf_name);
        }
    }
    void EnqueueAgentStatsCollectorTask(int count) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        AgentStatsCollectorTask *task = new AgentStatsCollectorTask(count);
        scheduler->Enqueue(task);
    }

    void SetAgentStatsIntervalReq(int interval) {
        SetAgentStatsInterval_InSeconds *req = new SetAgentStatsInterval_InSeconds();
        req->set_interval(interval);
        Sandesh::set_response_callback(
            boost::bind(&UveTest::SetStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void SetFlowStatsIntervalReq(int interval) {
        SetFlowStatsInterval_InSeconds *req = new SetFlowStatsInterval_InSeconds();
        req->set_interval(interval);
        Sandesh::set_response_callback(
            boost::bind(&UveTest::SetStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void SetStatsResponse(Sandesh *sandesh) {
        if (memcmp(sandesh->Name(), "StatsCfgResp", strlen("StatsCfgResp")) == 0) {
            success_responses_++;
        } else if (memcmp(sandesh->Name(), "StatsCfgErrResp", strlen("StatsCfgErrResp")) == 0) {
            error_responses_++;
        }
    }

    void GetStatsIntervalReq() {
        GetStatsInterval *req = new GetStatsInterval();
        Sandesh::set_response_callback(
            boost::bind(&UveTest::GetStatsResponse, this, _1));
        req->HandleRequest();
        client->WaitForIdle();
        req->Release();
    }

    void GetStatsResponse(Sandesh *sandesh) {
        if (memcmp(sandesh->Name(), "StatsIntervalResp_InSeconds", 
                   strlen("StatsIntervalResp_InSeconds")) == 0) {
            success_responses_++;
            StatsIntervalResp_InSeconds *resp = static_cast<StatsIntervalResp_InSeconds *>(sandesh);
            agent_stats_interval_ = resp->get_agent_stats_interval();
            flow_stats_interval_ = resp->get_flow_stats_interval();
        }
    }

    static void TestTearDown() {
    }

    static void CreateVmPorts(struct PortInfo *input, int count) {
        CreateVmportEnv(input, count);
    }

    void ClearCounters() {
        success_responses_ = error_responses_ = 0;
    }

    int success_responses_;
    int error_responses_;
    int agent_stats_interval_;
    int flow_stats_interval_;
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
    EXPECT_EQ(2U, Agent::GetInstance()->GetIntfCfgTable()->Size());

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
    
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetIntfCfgTable()->Size() == 0));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetVmTable()->Size() == 0));
    WAIT_FOR(100, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 0));
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
    EXPECT_EQ(2U, Agent::GetInstance()->GetIntfCfgTable()->Size());

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
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm1")) == 0U);

    DelLink("virtual-network", "vn2", "virtual-machine-interface", "vnet2");
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelNode("virtual-network", "vn2");
    client->WaitForIdle(2);
    client->VnDelNotifyWait(2);
    //Second Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 1));
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 0U);
    WAIT_FOR(500, 1000, (vmut->GetVmUveInterfaceCount("vm2")) == 0U);

    //Cleanup other things created by this test-case
    DeleteVmportEnv(input, 2, 1);
    client->WaitForIdle(2);
    client->VmDelNotifyWait(2);
    client->PortDelNotifyWait(2);

    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetIntfCfgTable()->Size() == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetVmTable()->Size() == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 0));
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
    EXPECT_EQ(2U, Agent::GetInstance()->GetIntfCfgTable()->Size());
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

    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetIntfCfgTable()->Size() == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetVmTable()->Size() == 0));
    WAIT_FOR(500, 1000, (Agent::GetInstance()->GetVnTable()->Size() == 0));
}

//To verify VRF addition/removal keeps the vrf stats tree in correct state
TEST_F(UveTest, VrfAddDelTest_1) {
    //Create vrf - vrf11
    VrfAddReq("vrf11");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf11")== true));
    EXPECT_TRUE(DBTableFind("vrf11.uc.route.0"));
 
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName("vrf11");
    EXPECT_TRUE(vrf != NULL);
    int vrf11_id = vrf->vrf_id();

    //Verify that vrf_stats entry is added vrf_stats_tree of 
    //agent_stats_collector
    EXPECT_TRUE(VrfStatsMatch(vrf11_id, string("vrf11"), true, 0, 0, 0, 0, 0, 
                              0, 0, 0, 0, 0, 0, 0, 0));

    //Delete vrf - vrf11
    VrfDelReq("vrf11");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf11")== false));
    EXPECT_FALSE(DBTableFind("vrf11.uc.route.0"));

    //Verify that vrf_stats entry is not removed from vrf_stats_tree of 
    //agent_stats_collector after deletion of vrf
    EXPECT_TRUE(VrfStatsMatch(vrf11_id, string("vrf11"), true, 0, 0, 0, 0, 0, 
                              0, 0, 0, 0, 0, 0, 0, 0));

    //Create vrf - vrf21
    VrfAddReq("vrf21");
    client->WaitForIdle();
    EXPECT_TRUE(DBTableFind("vrf21.uc.route.0"));
 
    vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName("vrf21");
    EXPECT_TRUE(vrf != NULL);
    int vrf21_id = vrf->vrf_id();
    LOG(DEBUG, "vrf 11 " << vrf11_id << " vrf 21 " << vrf21_id);
    if (vrf11_id == vrf21_id) {
        //When vrf-id is re-used for different vrf verify that vrf-name
        //is updated correctly in the vrf_stats_entry of agent_stats_collector
        EXPECT_TRUE(VrfStatsMatch(vrf11_id, string("vrf21"), true, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0));
    }

    //Delete vrf - vrf21
    VrfDelReq("vrf21");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (VrfFind("vrf21")== false));
    EXPECT_FALSE(DBTableFind("vrf21.uc.route.0"));
    EXPECT_TRUE(VrfStatsMatch(vrf21_id, string("vrf21"), true, 0, 0, 0, 0, 0, 
                              0, 0, 0, 0, 0, 0, 0, 0));
}

TEST_F(UveTest, StatsCollectorTest) {
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->uve()->agent_stats_collector());
    collector->interface_stats_responses_ = 0;
    EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

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
    EnqueueAgentStatsCollectorTask(1);

    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Verify that Error handlers for agent_stats_collector is invoked.
    EXPECT_EQ(1, collector->interface_stats_errors_);
    EXPECT_EQ(1, collector->vrf_stats_errors_);
    EXPECT_EQ(1, collector->drop_stats_errors_);
    EXPECT_EQ(1, collector->interface_stats_responses_);
    EXPECT_EQ(1, collector->vrf_stats_responses_);
    EXPECT_EQ(1, collector->drop_stats_responses_);

    //Reset the error code in mock code
    KSyncSockTypeMap::set_error_code(0);
    collector->interface_stats_responses_ = 0;
    collector->interface_stats_errors_ = 0;
    collector->vrf_stats_responses_ = 0;
    collector->vrf_stats_errors_ = 0;
    collector->drop_stats_responses_ = 0;
    collector->drop_stats_errors_ = 0;

    //Enqueue stats-collector-task
    EnqueueAgentStatsCollectorTask(1);

    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));

    //Verify that Error handlers for agent_stats_collector is NOT invoked.
    EXPECT_EQ(0, collector->interface_stats_errors_);
    EXPECT_EQ(0, collector->vrf_stats_errors_);
    EXPECT_EQ(0, collector->drop_stats_errors_);
    EXPECT_EQ(1, collector->interface_stats_responses_);
    EXPECT_EQ(1, collector->vrf_stats_responses_);
    EXPECT_EQ(1, collector->drop_stats_responses_);

    //cleanup
    collector->interface_stats_responses_ = 0;
    collector->vrf_stats_responses_ = 0;
    collector->drop_stats_responses_ = 0;
}

TEST_F(UveTest, SandeshTest) {
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->uve()->agent_stats_collector());

    int flow_stats_interval = Agent::GetInstance()->uve()->flow_stats_collector()->expiry_time();
    int agent_stats_interval = collector->expiry_time();

    //Set Flow stats interval to invalid value
    ClearCounters();
    SetFlowStatsIntervalReq(-1);

    //Verify that flow stats interval has not changed
    EXPECT_EQ(1, error_responses_);
    EXPECT_EQ(0, success_responses_);
    EXPECT_EQ(flow_stats_interval, Agent::GetInstance()->uve()->flow_stats_collector()->expiry_time());

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
    EXPECT_EQ((3 * 1000), Agent::GetInstance()->uve()->flow_stats_collector()->expiry_time());

    //Set Agent stats interval to a valid value
    ClearCounters();
    SetAgentStatsIntervalReq(40);

    //Verify that agent stats interval has been updated
    EXPECT_EQ(0, error_responses_);
    EXPECT_EQ(1, success_responses_);
    EXPECT_EQ((40 * 1000), collector->expiry_time());

    //Fetch agent and flow stats interval via Sandesh
    ClearCounters();
    GetStatsIntervalReq();

    //Verify the fetched agent/flow stats interval
    EXPECT_EQ(0, error_responses_);
    EXPECT_EQ(1, success_responses_);
    EXPECT_EQ(40, agent_stats_interval_);
    EXPECT_EQ(3, flow_stats_interval_);
}

int main(int argc, char **argv) {
    int ret;
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    UveTest::TestSetup(vrf_array, 2);

    ret = RUN_ALL_TESTS();
    UveTest::TestTearDown();
    Agent::GetInstance()->GetEventManager()->Shutdown();
    AsioStop();

    return ret;
}
