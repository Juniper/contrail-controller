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
#include <oper/interface.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "ksync/ksync_sock_user.h"
#include "vr_types.h"
#include <uve/test/vm_uve_table_test.h>
#include "uve/test/test_uve_util.h"

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

class UveVmUveTest : public ::testing::Test {
public:
    UveVmUveTest() : util_() {
    }

    TestUveUtil util_;
};

TEST_F(UveVmUveTest, VmAddDel_1) {
    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    //Add VM
    util_.VmAdd(1);
    EXPECT_EQ(1U, vmut->VmUveCount());
    EXPECT_EQ(1U, vmut->send_count());

    VmEntry *vm = VmGet(1);
    EXPECT_TRUE(vm != NULL);
    UveVirtualMachineAgent *uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Add another VM
    util_.VmAdd(2);
    EXPECT_EQ(2U, vmut->VmUveCount());
    EXPECT_EQ(2U, vmut->send_count());

    VmEntry *vm2 = VmGet(2);
    EXPECT_TRUE(vm2 != NULL);
    UveVirtualMachineAgent *uve2 =  vmut->VmUveObject(vm2);
    EXPECT_TRUE(uve2 != NULL);
    EXPECT_EQ(0U, uve2->get_interface_list().size()); 

    //Delete one of the VMs
    util_.VmDelete(1);
    EXPECT_EQ(1U, vmut->VmUveCount());
    EXPECT_EQ(1U, vmut->delete_count());

    //Delete other VM also
    util_.VmDelete(2);
    EXPECT_EQ(0U, vmut->VmUveCount());
    EXPECT_EQ(2U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

TEST_F(UveVmUveTest, VmIntfAddDel_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    //Add VN
    util_.VnAdd(input[0].vn_id);

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Since the port is inactive, verify that no VM UVE send has 
    //happened since port addition
    EXPECT_EQ(0U, vmut->send_count());

    //Add VM
    util_.VmAdd(input[0].vm_id);

    //Verify send_count after VM addition
    EXPECT_EQ(1U, vmut->send_count());

    //Add necessary objects and links to make vm-intf active
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddVmPortVrf("vnet1", "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    //Verify UVE 
    VmEntry *vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    UveVirtualMachineAgent *uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(2U, vmut->send_count());
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Verify UVE 
    EXPECT_EQ(3U, vmut->send_count());
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Activate the interface again
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_EQ(4U, vmut->send_count());
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Verify UVE 
    EXPECT_EQ(5U, vmut->send_count());
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //other cleanup
    util_.VnDelete(input[0].vn_id);
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    DelNode("virtual-network", "vn1");
    DelNode("virtual-machine-interface", "vnet1");
    DelInstanceIp("instance0");
    client->WaitForIdle();
    IntfCfgDel(input, 0);
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_EQ(1U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    usleep(10000);
    return RUN_ALL_TESTS();
}
