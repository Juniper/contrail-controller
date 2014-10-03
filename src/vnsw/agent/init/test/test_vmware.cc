/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"

void RouterIdDepInit(Agent *agent) {
}

class VmwareTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        param = agent->params();

        intf_count_ = agent->interface_table()->Size();
        nh_count_ = agent->nexthop_table()->Size();
        vrf_count_ = agent->vrf_table()->Size();
    }

    virtual void TearDown() {
        WAIT_FOR(100, 1000, (agent->interface_table()->Size() == intf_count_));
        WAIT_FOR(100, 1000, (agent->vrf_table()->Size() == vrf_count_));
        WAIT_FOR(100, 1000, (agent->nexthop_table()->Size() == nh_count_));
        WAIT_FOR(100, 1000, (agent->vm_table()->Size() == 0U));
        WAIT_FOR(100, 1000, (agent->vn_table()->Size() == 0U));
    }

    AgentParam *param;
    Agent *agent;

    int intf_count_;
    int nh_count_;
    int vrf_count_;
};

// Validate the vmware parameters set in AgentParam
TEST_F(VmwareTest, VmwareParam_1) {
    EXPECT_EQ(param->mode(), AgentParam::MODE_VMWARE);
    EXPECT_STREQ(param->vmware_physical_port().c_str(), "vmport");
}

// Validate creation of VM connecting port
TEST_F(VmwareTest, VmwarePhysicalPort_1) {

    // Validate the both IP Fabric and VM physical interfaces are present
    PhysicalInterfaceKey key(param->vmware_physical_port());
    Interface *intf = static_cast<Interface *>
        (agent->interface_table()->Find(&key, true));
    EXPECT_TRUE(intf != NULL);
    PhysicalInterface *phy_intf =
        static_cast<PhysicalInterface *>(intf);
    EXPECT_TRUE(phy_intf->persistent() == true);

    PhysicalInterfaceKey key1(agent->fabric_interface_name());
    intf = static_cast<Interface *>
        (agent->interface_table()->Find(&key1, true));
    EXPECT_TRUE(intf != NULL);
    phy_intf = static_cast<PhysicalInterface *>(intf);
    EXPECT_TRUE(phy_intf->persistent() == false);
}

// Create a VM VLAN interface
TEST_F(VmwareTest, VmwareVmPort_1) {
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    client->Reset();
    IntfCfgAdd(1, "vnet1", "1.1.1.1", 1, 1, "00:00:00:01:01:01", 1,
               "::0101:0101", 1);
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    if (VmPortGet(1) == NULL) {
        EXPECT_STREQ("Port not created", "");
        return;
    }

    VmInterface *intf = dynamic_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf != NULL);
    if (intf == NULL)
        return;

    EXPECT_TRUE(intf->vlan_id() == 1);
    EXPECT_TRUE(intf->parent() != NULL);

    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    strcpy(init_file, "controller/src/vnsw/agent/init/test/cfg-vmware.ini");

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    usleep(1000);
    TestShutdown();
    usleep(1000);
    delete client;
    return ret;
}
