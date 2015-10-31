/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
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
#include <oper/mirror_table.h>
#include <oper/physical_device.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "pkt/test/test_flow_util.h"
#include "vr_types.h"
#include <vector>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <sandesh/common/flow_types.h>

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define vm3_ip "11.1.1.3"

struct PortInfo input[] = {
        {"flow0", IPPROTO_UDP, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
        {"flow2", 8, vm3_ip, "00:00:00:01:01:03", 5, 3},
};

class FlowStatsManagerTest : public ::testing::Test {
public:
    FlowStatsManagerTest(): agent_(Agent::GetInstance()) {
        fsm_ = agent_->flow_stats_manager();
    }

protected:
    virtual void SetUp() {
        CreateVmportEnv(input, 3, 1);
        client->WaitForIdle(5);
        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 3, true, 1);
        client->WaitForIdle(3);
    }

    Agent *agent() { return agent_;}
    Agent *agent_;
    const VmInterface *flow0;
    FlowStatsManager *fsm_;
};

TEST_F(FlowStatsManagerTest, AddDelete) {
    fsm_->Add(FlowAgingTableKey(IPPROTO_UDP, 30), 1, 1);
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 30) != NULL);
    fsm_->Delete(FlowAgingTableKey(IPPROTO_UDP,30));
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 30) == NULL);
}

TEST_F(FlowStatsManagerTest, DeleteDefaultStatsCollector) {
    fsm_->Delete(FlowAgingTableKey(255, 0));
    client->WaitForIdle(); 
    EXPECT_TRUE(fsm_->Find(255, 0) != NULL);
}

TEST_F(FlowStatsManagerTest, AddTcpCollector) {
    fsm_->Add(FlowAgingTableKey(IPPROTO_TCP, 30), 1, 1);
    client->WaitForIdle(); 
    EXPECT_TRUE(fsm_->Find(IPPROTO_TCP, 30) == NULL);
}

TEST_F(FlowStatsManagerTest, AddDeleteWithFlow) {
    fsm_->Add(FlowAgingTableKey(IPPROTO_UDP, 30), 10, 10);
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 30) != NULL);

    //Add a flow
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_UDP, 30, 0, "vrf5",
                       flow0->id()),
        {   
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    
    fsm_->Delete(FlowAgingTableKey(IPPROTO_UDP, 30));
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 30) != NULL);

    //Delete the flow and ensure flow_stats_manager also
    //gets deleted.
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 30) == NULL);
}

TEST_F(FlowStatsManagerTest, AddDeleteStatsCollectorWithPort) {
    fsm_->Add(FlowAgingTableKey(IPPROTO_UDP, 0), 10, 10);
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0) != NULL);

    //Add a flow
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_UDP, 30, 0, "vrf5",
                       flow0->id()),
        {   
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());

    //Add more explicit flow stats collector
    //Nothin should change
    fsm_->Add(FlowAgingTableKey(IPPROTO_UDP, 30), 10, 10);
    CreateFlow(flow, 1);
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 30)->size() == 0);
    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0)->size() == 2);
     
    //Delete the flow and ensure flow_stats_manager also
    //gets deleted.
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    
    fsm_->Delete(FlowAgingTableKey(IPPROTO_UDP, 30));
    fsm_->Delete(FlowAgingTableKey(IPPROTO_UDP, 0));
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 30) == NULL);
    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0) == NULL);
}

TEST_F(FlowStatsManagerTest, FlowAging) {
    fsm_->Add(FlowAgingTableKey(IPPROTO_UDP, 0), 1, 1);
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0) != NULL);

    //Add a flow
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_UDP, 30, 0, "vrf5",
                       flow0->id()),
        {   
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());
    client->WaitForIdle();
    
    sleep(2);
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0)->size() == 0);
    client->WaitForIdle();
    
    fsm_->Delete(FlowAgingTableKey(IPPROTO_UDP, 0));
    client->WaitForIdle();
    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0) == NULL);
}

TEST_F(FlowStatsManagerTest, FlowAgingTimeChange) {
    fsm_->Add(FlowAgingTableKey(IPPROTO_UDP, 0), 2, 1);
    client->WaitForIdle();


    fsm_->Add(FlowAgingTableKey(IPPROTO_UDP, 0), 1, 1);
    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0) != NULL);

    //Add a flow
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_UDP, 30, 0, "vrf5",
                       flow0->id()),
        {   
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, agent()->pkt()->flow_table()->Size());
    client->WaitForIdle();
    
    sleep(1);
    client->WaitForIdle();

    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0)->size() == 0);
    client->WaitForIdle();
    
    fsm_->Delete(FlowAgingTableKey(IPPROTO_UDP, 0));
    client->WaitForIdle();
    EXPECT_TRUE(fsm_->Find(IPPROTO_UDP, 0) == NULL);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, false, false, false);
    int ret = RUN_ALL_TESTS();
    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;
    return ret;
}

