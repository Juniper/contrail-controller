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
#include "test_cmn_util.h"
#include "vr_types.h"
#include <uve/vn_uve_table_test.h>

using namespace std;

void RouterIdDepInit() {
}

class UveVnUveTest : public ::testing::Test {
public:
    void VnAdd(int id) {
        char vn_name[80];

        sprintf(vn_name, "vn%d", id);
        uint32_t vn_count = Agent::GetInstance()->GetVnTable()->Size();
        client->Reset();
        AddVn(vn_name, id);
        EXPECT_TRUE(client->VnNotifyWait(1));
        EXPECT_TRUE(VnFind(id));
        EXPECT_EQ((vn_count + 1), Agent::GetInstance()->GetVnTable()->Size());
    }

    void VnDelete(int id) {
        char vn_name[80];

        sprintf(vn_name, "vn%d", id);
        uint32_t vn_count = Agent::GetInstance()->GetVnTable()->Size();
        client->Reset();
        DelNode("virtual-network", vn_name);
        EXPECT_TRUE(client->VnNotifyWait(1));
        EXPECT_EQ((vn_count - 1), Agent::GetInstance()->GetVnTable()->Size());
        EXPECT_FALSE(VnFind(id));
    }

    void AclAdd(int id) {
        char acl_name[80];

        sprintf(acl_name, "acl%d", id);
        uint32_t count = Agent::GetInstance()->GetAclTable()->Size();
        client->Reset();
        AddAcl(acl_name, id);
        EXPECT_TRUE(client->AclNotifyWait(1));
        EXPECT_EQ((count + 1), Agent::GetInstance()->GetAclTable()->Size());
    }

    void VmAdd(int id) {
        char vm_name[80];

        sprintf(vm_name, "vm%d", id);
        uint32_t vm_count = Agent::GetInstance()->GetVmTable()->Size();
        client->Reset();
        AddVm(vm_name, id);
        EXPECT_TRUE(client->VmNotifyWait(1));
        EXPECT_TRUE(VmFind(id));
        EXPECT_EQ((vm_count + 1), Agent::GetInstance()->GetVmTable()->Size());
    }

    void VrfAdd(int id) {
        char vrf_name[80];

        sprintf(vrf_name, "vrf%d", id);
        AddVrf(vrf_name);
        client->WaitForIdle();
        EXPECT_TRUE(client->VrfNotifyWait(1));
        EXPECT_TRUE(VrfFind(vrf_name));
    }

    void NovaPortAdd(struct PortInfo *input) {
        client->Reset();
        IntfCfgAdd(input, 0);
        EXPECT_TRUE(client->PortNotifyWait(1));
        EXPECT_EQ(1U, Agent::GetInstance()->GetIntfCfgTable()->Size());
    }

    void ConfigPortAdd(struct PortInfo *input) {
        client->Reset();
        AddPort(input[0].name, input[0].intf_id);
        client->WaitForIdle();
    }
};

TEST_F(UveVnUveTest, VnAddDel_1) {
    //Add VN
    VnAdd(1);
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->send_count());

    UveVirtualNetworkAgent *uve1 =  VnUveTableTest::GetInstance()->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Delete VN
    VnDelete(1);
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->delete_count());

    //clear counters at the end of test case
    client->Reset();
    VnUveTableTest::GetInstance()->ClearCount();
}

TEST_F(UveVnUveTest, VnIntfAddDel_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    //Add VN
    VnAdd(input[0].vn_id);
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->send_count());

    UveVirtualNetworkAgent *uve1 =  VnUveTableTest::GetInstance()->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    // Nova Port add message
    NovaPortAdd(input);

    // Config Port add
    ConfigPortAdd(input);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Since the port is inactive, verify that no additional VN UVE send has 
    //happened since port addition
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->send_count());

    //Add necessary objects and links to make vm-intf active
    VmAdd(input[0].vm_id);
    VrfAdd(input[0].vn_id);
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
    EXPECT_EQ(2U, VnUveTableTest::GetInstance()->send_count());
    EXPECT_EQ(1U, uve1->get_virtualmachine_list().size()); 
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
    EXPECT_EQ(3U, VnUveTableTest::GetInstance()->send_count());
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Delete VN
    VnDelete(input[0].vn_id);

    //Verify UVE 
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->delete_count());

    //other cleanup
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

    //clear counters at the end of test case
    client->Reset();
    VnUveTableTest::GetInstance()->ClearCount();
}

TEST_F(UveVnUveTest, VnChange_1) {
    //Add VN
    VnAdd(1);
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->send_count());

    UveVirtualNetworkAgent *uve1 =  VnUveTableTest::GetInstance()->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Associate an ACL with VN
    AclAdd(1);
    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();

    //Verify UVE
    EXPECT_EQ(2U, VnUveTableTest::GetInstance()->send_count());
    EXPECT_TRUE((uve1->get_acl().compare(string("acl1")) == 0));

    //Disasociate ACL from VN
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();

    //Verify UVE
    EXPECT_EQ(3U, VnUveTableTest::GetInstance()->send_count());
    EXPECT_TRUE((uve1->get_acl().compare(string("")) == 0));

    //Associate VN with a new ACL
    AclAdd(2);
    AddLink("virtual-network", "vn1", "access-control-list", "acl2");
    client->WaitForIdle();

    //Verify UVE
    EXPECT_EQ(4U, VnUveTableTest::GetInstance()->send_count());
    EXPECT_TRUE((uve1->get_acl().compare(string("acl2")) == 0));

    //Delete VN
    VnDelete(1);
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->delete_count());

    //clear counters at the end of test case
    client->Reset();
    VnUveTableTest::GetInstance()->ClearCount();
}

TEST_F(UveVnUveTest, VnUVE_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", 2, 2},
    };

    //Create VM, VN, VRF and Vmport
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveVmCount("vn2")) == 1U);
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn2")) == 1U);

    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn2"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn2"));

    struct PortInfo input1[] = {
        {"vnet3", 3, "1.1.1.3", "00:00:00:01:01:03", 1, 3},
    };
    struct PortInfo input2[] = {
        {"vnet4", 4, "1.1.1.4", "00:00:00:01:01:04", 1, 4},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    EXPECT_EQ(3U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(3U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));

    client->Reset();
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(2U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(2U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn2"));
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn2"));
}

TEST_F(UveVnUveTest, VnUVE_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", 2, 2},
    };

    VnUveTableTest::GetInstance()->ClearCount();
    //Create VM, VN, VRF and Vmport
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveVmCount("vn2")) == 1U);
    WAIT_FOR(500, 1000, (VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn2")) == 1U);

    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn2"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn2"));

    UveVirtualNetworkAgent *uve1 =  VnUveTableTest::GetInstance()->VnUveObject("vn1");
    EXPECT_EQ(1U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    UveVirtualNetworkAgent *uve2 =  VnUveTableTest::GetInstance()->VnUveObject("vn2");
    EXPECT_EQ(1U, uve2->get_virtualmachine_list().size()); 
    EXPECT_EQ(1U, uve2->get_interface_list().size()); 

    struct PortInfo input1[] = {
        {"vnet3", 3, "1.1.1.3", "00:00:00:01:01:03", 1, 3},
    };
    struct PortInfo input2[] = {
        {"vnet4", 4, "1.1.1.4", "00:00:00:01:01:04", 1, 4},
    };
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    EXPECT_EQ(3U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(3U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(3U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(3U, uve1->get_interface_list().size()); 

    client->Reset();
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(2U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(2U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(2U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(2U, uve1->get_interface_list().size()); 

    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(1U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn1"));
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveVmCount("vn2"));
    EXPECT_EQ(0U, VnUveTableTest::GetInstance()->GetVnUveInterfaceCount("vn2"));

    uve1 =  VnUveTableTest::GetInstance()->VnUveObject("vn1");
    uve2 =  VnUveTableTest::GetInstance()->VnUveObject("vn2");
    EXPECT_TRUE(uve1 == NULL);
    EXPECT_TRUE(uve2 == NULL);
    EXPECT_EQ(2U, VnUveTableTest::GetInstance()->delete_count());
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    usleep(10000);
    return RUN_ALL_TESTS();
}
