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
#include <oper/interface.h>
#include <uve/uve_client.h>
#include "vr_types.h"

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "xmpp/test/xmpp_test_util.h"

int vrf_array[] = {1, 2};

using namespace std;

void RouterIdDepInit() {
}

class UveTest : public ::testing::Test {
public:
    static void TestSetup(int *vrf, int count) {
        char vrf_name[80];
        for (int i = 0; i < count; i++) {
            sprintf(vrf_name, "vrf%d", vrf[i]);
            AddVrf(vrf_name);
        }
    }

    static void TestTearDown() {
    }

    static void CreateVmPorts(struct PortInfo *input, int count) {
        CreateVmportEnv(input, count);
    }
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
    //Two entries in VM and VN map
    WAIT_FOR(500, 1000, (2 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (2 == UveClient::GetInstance()->VmIntfMapSize()));

    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine", "vm1");
    client->VmDelNotifyWait(1);
    client->WaitForIdle(2);
    //One Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 0));
    WAIT_FOR(500, 1000, (1 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (1 == UveClient::GetInstance()->VmIntfMapSize()));

    DelLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet2");
    DelLink("virtual-network", "vn2", "virtual-machine-interface", "vnet2");
    DelNode("virtual-machine", "vm2");
    client->VmDelNotifyWait(2);
    client->WaitForIdle(2);
    //second Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 1));
    WAIT_FOR(500, 1000, (0 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (0 == UveClient::GetInstance()->VmIntfMapSize()));
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

    //Two entries in VM and VN map
    WAIT_FOR(500, 1000, (2 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (2 == UveClient::GetInstance()->VmIntfMapSize()));

    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle(2);
    client->VnDelNotifyWait(1);
    //One Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 0));
    WAIT_FOR(500, 1000, (1 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (1 == UveClient::GetInstance()->VmIntfMapSize()));

    DelLink("virtual-network", "vn2", "virtual-machine-interface", "vnet2");
    DelLink("virtual-network", "vn2", "routing-instance", "vrf2");
    DelNode("virtual-network", "vn2");
    client->WaitForIdle(2);
    client->VnDelNotifyWait(2);
    //Second Vmport inactive
    EXPECT_TRUE(VmPortInactive(input, 1));
    WAIT_FOR(500, 1000, (0 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (0 == UveClient::GetInstance()->VmIntfMapSize()));

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
    //Create VM, VN, VRF and Vmport
    CreateVmPorts(input, 2);
    client->WaitForIdle(2);
    WAIT_FOR(500, 1000, (VmPortActive(input, 0) == true));
    WAIT_FOR(500, 1000, (VmPortActive(input, 1) == true));
    EXPECT_EQ(2U, Agent::GetInstance()->GetIntfCfgTable()->Size());
    //Two entries in VM and VN map
    WAIT_FOR(500, 1000, (2 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (2 == UveClient::GetInstance()->VmIntfMapSize()));

    IntfCfgDel(input, 0);
    client->WaitForIdle(2);
    client->PortDelNotifyWait(1);
    EXPECT_EQ(1U, UveClient::GetInstance()->VnIntfMapSize());
    EXPECT_EQ(1U, UveClient::GetInstance()->VmIntfMapSize());

    IntfCfgDel(input, 1);
    client->WaitForIdle(2);
    client->PortDelNotifyWait(2);
    WAIT_FOR(500, 1000, (0 == UveClient::GetInstance()->VnIntfMapSize()));
    WAIT_FOR(500, 1000, (0 == UveClient::GetInstance()->VmIntfMapSize()));

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
    int vrf11_id = vrf->GetVrfId();

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
    int vrf21_id = vrf->GetVrfId();
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
