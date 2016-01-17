/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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
#include <oper/interface.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "pkt/test/test_flow_util.h"
#include <vrouter/flow_stats/test/flow_stats_collector_test.h>
#include "ksync/ksync_sock_user.h"
#include "vr_types.h"
#include <uve/test/vm_uve_table_test.h>
#include "uve/test/test_uve_util.h"

using namespace std;

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define vm4_ip "14.1.1.1"
#define vm1_fip "14.1.1.100"

#define vm_a_ip "15.0.0.1"
#define vm_b_ip "16.0.0.1"
#define vm_c_ip "17.0.0.1"
#define vm_c_fip1 "17.0.0.10"
#define vm_c_fip2 "17.0.0.11"


#define remote_vm_fip "14.1.1.101"
#define remote_router_ip "10.1.1.2"

struct PortInfo input[] = {
    {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
    {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
};

struct PortInfo input2[] = {
    {"flow2", 8, vm4_ip, "00:00:00:01:01:06", 4, 3},
};

struct PortInfo fip_input1[] = {
    {"flowa", 3, vm_a_ip, "00:00:00:02:01:01", 6, 6},
    {"flowb", 4, vm_b_ip, "00:00:00:02:01:02", 7, 7},
};

struct PortInfo fip_input2[] = {
    {"flowc", 5, vm_c_ip, "00:00:00:02:01:03", 8, 8},
};

VmInterface *flow0;
VmInterface *flow1;
VmInterface *flow2;

VmInterface *flowa;
VmInterface *flowb;
VmInterface *flowc;

void RouterIdDepInit(Agent *agent) {
}

class UveVmUveTest : public ::testing::Test {
public:
    UveVmUveTest() : util_(), peer_(NULL), agent_(Agent::GetInstance()) {
        proto_ = agent_->pkt()->get_flow_proto();
    }
    void FlowSetUp() {
        EXPECT_EQ(0U, proto_->FlowCount());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);
        CreateVmportFIpEnv(input2, 1, 0);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        EXPECT_TRUE(VmPortActive(input2, 0));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
        flow2 = VmInterfaceGet(input2[0].intf_id);
        assert(flow2);

        // Configure Floating-IP
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, vm1_fip);
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "default-project:vn4");
        AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
        client->WaitForIdle();
        EXPECT_TRUE(flow0->HasFloatingIp());
    }

    void FlowTearDown() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(2);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));

        client->Reset();
        DeleteVmportFIpEnv(input2, 1, true);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(1);
        EXPECT_FALSE(VmPortFind(input2, 0));
    }

    void FlowTearDown2() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(fip_input1, 2, true, 1);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(2);
        EXPECT_FALSE(VmPortFind(fip_input1, 0));
        EXPECT_FALSE(VmPortFind(fip_input1, 1));

        client->Reset();
        DeleteVmportFIpEnv(fip_input2, 1, true);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(1);
        EXPECT_FALSE(VmPortFind(fip_input2, 0));
    }

    void FlowSetUp2() {
        EXPECT_EQ(0U, proto_->FlowCount());
        client->Reset();
        CreateVmportEnv(fip_input1, 2, 1);
        client->WaitForIdle(5);
        CreateVmportFIpEnv(fip_input2, 1, 0);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(fip_input1, 0));
        EXPECT_TRUE(VmPortActive(fip_input1, 1));
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(fip_input1, 0)));
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(fip_input1, 1)));
        EXPECT_TRUE(VmPortActive(fip_input2, 0));

        flowa = VmInterfaceGet(fip_input1[0].intf_id);
        assert(flowa);
        flowb = VmInterfaceGet(fip_input1[1].intf_id);
        assert(flowb);
        flowc = VmInterfaceGet(fip_input2[0].intf_id);
        assert(flowc);

        // Configure Floating-IP
        AddFloatingIpPool("fip-poolc", 1);
        AddFloatingIp("fipc1", 1, vm_c_fip1);
        AddFloatingIp("fipc2", 1, vm_c_fip2);
        AddLink("floating-ip", "fipc1", "floating-ip-pool", "fip-poolc");
        AddLink("floating-ip", "fipc2", "floating-ip-pool", "fip-poolc");
        AddLink("floating-ip-pool", "fip-poolc", "virtual-network", "default-project:vn8");
        AddLink("virtual-machine-interface", "flowa", "floating-ip", "fipc1");
        AddLink("virtual-machine-interface", "flowb", "floating-ip", "fipc2");
        client->WaitForIdle();
        EXPECT_TRUE(flowa->HasFloatingIp());
        EXPECT_TRUE(flowb->HasFloatingIp());
    }

    void RemoveFipConfig() {
        DelLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
        DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        DelLink("floating-ip-pool", "fip-pool1",
                "virtual-network", "default-project:vn4");
        DelFloatingIp("fip1");
        DelFloatingIpPool("fip-pool1");
        client->WaitForIdle(3);
    }

    void RemoveFipConfig2() {
        DelLink("virtual-machine-interface", "flowa", "floating-ip", "fipc1");
        DelLink("virtual-machine-interface", "flowb", "floating-ip", "fipc2");
        DelLink("floating-ip-pool", "fip-poolc",
                "virtual-network", "default-project:vn8");
        DelLink("floating-ip", "fipc1", "floating-ip-pool", "fip-poolc");
        DelLink("floating-ip", "fipc2", "floating-ip-pool", "fip-poolc");
        DelFloatingIp("fipc1");
        DelFloatingIp("fipc2");
        DelFloatingIpPool("fip-poolc");
        client->WaitForIdle(3);
    }

    Agent *agent() {return agent_;}

    TestUveUtil util_;
    BgpPeer *peer_;
    Agent *agent_;
    FlowProto *proto_;
};

TEST_F(UveVmUveTest, VmAddDel_1) {
    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    vmut->ClearCount();
    //Add VM
    util_.VmAdd(1);
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    EXPECT_EQ(1U, vmut->VmUveCount());
    EXPECT_EQ(1U, vmut->send_count());

    VmEntry *vm = VmGet(1);
    EXPECT_TRUE(vm != NULL);
    UveVirtualMachineAgent *uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    string uuid_str = to_string(vm->GetUuid());
    EXPECT_STREQ(uuid_str.c_str(), uve1->get_uuid().c_str());
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Add another VM
    util_.VmAdd(2);
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    EXPECT_EQ(2U, vmut->VmUveCount());
    EXPECT_EQ(2U, vmut->send_count());

    VmEntry *vm2 = VmGet(2);
    EXPECT_TRUE(vm2 != NULL);
    UveVirtualMachineAgent *uve2 =  vmut->VmUveObject(vm2);
    EXPECT_TRUE(uve2 != NULL);
    EXPECT_EQ(0U, uve2->get_interface_list().size()); 

    //Delete one of the VMs
    util_.VmDelete(1);
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    EXPECT_EQ(1U, vmut->VmUveCount());
    EXPECT_EQ(1U, vmut->delete_count());

    //Delete other VM also
    util_.VmDelete(2);
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

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
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->VmUveCount());

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
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

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

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify UVE 
    VmEntry *vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    UveVirtualMachineAgent *uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(2U, vmut->send_count());
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    //Verify interface UUID
    std::string intf_entry = uve1->get_interface_list().front();
    VmInterface *vmi = VmInterfaceGet(input[0].intf_id);
    assert(vmi);
    string cfg_name = vmi->cfg_name();
    EXPECT_STREQ(cfg_name.c_str(), intf_entry.c_str());

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify that no UVE is sent on VMI deactivation
    EXPECT_EQ(2U, vmut->send_count());
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    //Activate the interface again
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify that no UVE is sent on VMI reactivation
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

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify that no UVE is sent on VMI deactivation
    EXPECT_EQ(2U, vmut->send_count());
    EXPECT_EQ(1U, uve1->get_interface_list().size());
    uint32_t send_count = vmut->send_count();

    //other cleanup
    util_.VnDelete(input[0].vn_id);
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
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

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

    //Verify UVE
    EXPECT_TRUE((vmut->send_count() > send_count));
    EXPECT_EQ(1U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

/* Vm Dissassociation from VMI, VM Delete --> Vm Add, Vm Reassociation */
TEST_F(UveVmUveTest, VmIntfAddDel_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->VmUveCount());

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

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

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

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify UVE 
    VmEntry *vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    UveVirtualMachineAgent *uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(2U, vmut->send_count());
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    //Disassociate VM from VMI and delete the VM
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    util_.VmDelete(input[0].vm_id);
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

    EXPECT_TRUE((vmut->send_count() >= 3U));
    EXPECT_EQ(1U, vmut->delete_count());
    
    //Add the VM back and re-associate it with same VMI
    util_.VmAdd(input[0].vm_id);
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 1U)));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_TRUE((vmut->send_count() >= 4U));
    EXPECT_EQ(1U, vmut->delete_count());
    vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify UVE 
    vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);

    //Verify that no more VM uves are sent on VMI deactivation
    EXPECT_TRUE((vmut->send_count() >= 4U));
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    //Activate the interface again
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify that no more VM uves are sent on VMI re-activation
    EXPECT_TRUE((vmut->send_count() >= 4U));
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify that no more VM uves are sent on VMI deactivation
    EXPECT_TRUE((vmut->send_count() >= 4U));
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    //other cleanup
    util_.VnDelete(input[0].vn_id);
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
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

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

    //Verify UVE 
    EXPECT_TRUE((vmut->send_count() >= 5U));
    EXPECT_EQ(2U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

/* Test case to verify that FIP entries which are  not in "installed" state
 * are handled correctly. */
TEST_F(UveVmUveTest, FipL3Disabled) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    EXPECT_EQ(0U, vmut->VmUveCount());
    //Add VN
    util_.VnAdd(input[0].vn_id);
    // Nova Port add message
    util_.NovaPortAdd(&input[0]);
    // Config Port add
    util_.ConfigPortAdd(&input[0]);
    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.VmAdd(input[0].vm_id);
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddVmPortVrf("vnet1", "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle(3);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle(3);
    // Expect VmPort to be Inactive because of absence of link
    // AddLink("virtual-machine-interface-routing-instance", "vnet1",
    //         "routing-instance", "vrf1");
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Create a VN for floating-ip
    client->Reset();
    AddVn("default-project:vn2", 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (client->vn_notify_ >= 1));
    AddVrf("default-project:vn2:vn2");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("default-project:vn2:vn2"));
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle();

    // Configure Floating-IP
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "71.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((VmPortFloatingIpCount(1, 1) == true)));

    //Delete the floating-IP
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    client->WaitForIdle();

    //cleanup
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn1");
    DelFloatingIpPool("fip-pool1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle(3);

    DelNode("virtual-network", "default-project:vn2");
    DelVrf("default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    client->WaitForIdle(3);
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface", "vnet1");
    DelInstanceIp("instance0");
    client->WaitForIdle(3);
    IntfCfgDel(input, 0);
    util_.VnDelete(input[0].vn_id);
    client->WaitForIdle(3);

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));
    vmut->ClearCount();
}

/* Create a VMI which has ipv4_active_ as false.
 * Assoicate a FIP to this VMI.
 * FIP will have "installed" as  false.
 * Remove this FIP and trigger a change event on VMI.
 * Verify that Agent does not crash.
 */
TEST_F(UveVmUveTest, FipUninstalledRemove) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    EXPECT_EQ(0U, vmut->VmUveCount());
    //Add VN
    util_.VnAdd(input[0].vn_id);
    // Nova Port add message
    util_.NovaPortAdd(&input[0]);
    // Config Port add
    util_.ConfigPortAdd(&input[0]);

    util_.VmAdd(input[0].vm_id);
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddVmPortVrf("vnet1", "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    client->WaitForIdle(3);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle(3);
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Create a VN for floating-ip
    client->Reset();
    AddVn("default-project:vn2", 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (client->vn_notify_ >= 1));
    AddVrf("default-project:vn2:vn2");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("default-project:vn2:vn2"));
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle();

    // Configure Floating-IP
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "71.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((VmPortFloatingIpCount(1, 1) == true)));

    // Trigger add change event on VMI
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    //Delete the floating-IP
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelFloatingIp("fip1");
    client->WaitForIdle();

    //cleanup
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn1");
    DelFloatingIpPool("fip-pool1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle(3);

    DelNode("virtual-network", "default-project:vn2");
    DelVrf("default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    client->WaitForIdle(3);
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface", "vnet1");
    DelInstanceIp("instance0");
    client->WaitForIdle(3);
    IntfCfgDel(input, 0);
    util_.VnDelete(input[0].vn_id);
    client->WaitForIdle(3);

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));
    vmut->ClearCount();
}

/* Change the VM associated with a VMI to a different VM
 * Verify that the old and new VM UVEs have correct interfaces
 */
TEST_F(UveVmUveTest, VmChangeOnVMI) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    EXPECT_EQ(0U, vmut->VmUveCount());
    //Add VN
    util_.VnAdd(input[0].vn_id);
    // Nova Port add message
    util_.NovaPortAdd(&input[0]);
    // Config Port add
    util_.ConfigPortAdd(&input[0]);
    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.VmAdd(input[0].vm_id);
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddVmPortVrf("vnet1", "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    client->WaitForIdle(3);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle(3);
    EXPECT_TRUE(VmPortActive(input, 0));

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify UVE
    VmEntry *vm = VmGet(1);
    UveVirtualMachineAgent *uve1 = vmut->VmUveObject(vm);
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    //Disassociate VMI from VM
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    util_.VmAdd(2);
    AddLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet1");
    client->WaitForIdle(3);

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify UVE
    uve1 = vmut->VmUveObject(vm);
    EXPECT_EQ(0U, uve1->get_interface_list().size());
    VmEntry *vm2 = VmGet(2);
    UveVirtualMachineAgent *uve2 = vmut->VmUveObject(vm2);
    EXPECT_EQ(1U, uve2->get_interface_list().size());

    //cleanup
    DelLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle(3);

    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    client->WaitForIdle(3);
    DelNode("virtual-machine", "vm1");
    DelNode("virtual-machine", "vm2");
    DelNode("routing-instance", "vrf1");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface", "vnet1");
    DelInstanceIp("instance0");
    client->WaitForIdle(3);
    IntfCfgDel(input, 0);
    util_.VnDelete(input[0].vn_id);
    client->WaitForIdle(3);

    //clear counters at the end of test case
    client->Reset();
    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));
    vmut->ClearCount();
}

//Verfiy Source IP overriden for NAT flows in flow-log messages exported by
//agent
TEST_F(UveVmUveTest, SIP_override) {
    FlowSetUp();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                        flow0->id(), 1),
            {
                new VerifyNat(vm4_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, proto_->FlowCount());

    //Verify Floating IP flows are created.
    FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm4_ip,
                        vm1_fip, 1, 0, 0, rev->key().nh));

    FlowStatsCollectorTest *f = static_cast<FlowStatsCollectorTest *>
        (Agent::GetInstance()->flow_stats_manager()->
         default_flow_stats_collector());
    f->ClearList();
    FlowExportInfo *info = f->FindFlowExportInfo(f1->key());
    FlowExportInfo *rinfo = f->FindFlowExportInfo(rev->key());
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);

    std::vector<FlowLogData> list = f->ingress_flow_log_list();
    EXPECT_EQ(0U, list.size());

    //Set the action as Log for the flow-entries to ensure that they are not
    //dropped during export
    util_.EnqueueFlowActionLogChange(f1);
    util_.EnqueueFlowActionLogChange(rev);
    client->WaitForIdle(10);
    WAIT_FOR(1000, 500, ((info->IsActionLog() == true)));
    WAIT_FOR(1000, 500, ((rinfo->IsActionLog() == true)));

    //Invoke FlowStatsCollector to export flow logs
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    list = f->ingress_flow_log_list();
    EXPECT_EQ(2U, list.size());

    IpAddress dip_addr = IpAddress::from_string(vm4_ip);
    IpAddress sip_addr = IpAddress::from_string(vm1_fip);
    //Verify that the source-ip of one of the exported flow is overwritten
    //with FIP
    FlowLogData fl1, fl2;
    fl1 = list.at(0);
    fl2 = list.at(1);
    if (fl1.get_destip() == dip_addr) {
        EXPECT_EQ(fl1.get_sourceip(), sip_addr);
    } else if (fl2.get_destip() == dip_addr) {
        EXPECT_EQ(fl2.get_sourceip(), sip_addr);
    }

    f->ClearList();
    //cleanup
    FlowTearDown();
    RemoveFipConfig();
    EXPECT_EQ(0U, proto_->FlowCount());
    client->WaitForIdle(10);

    //Verify that flow-logs are sent during flow delete
    WAIT_FOR(1000, 500, ((f->ingress_flow_log_list().size() == 2U)));
    list = f->ingress_flow_log_list();
    EXPECT_EQ(2U, list.size());

    //Verify that SIP is overwritten even for flows sent during flow delete
    fl1 = list.at(0);
    fl2 = list.at(1);
    if (fl1.get_destip() == dip_addr) {
        EXPECT_EQ(fl1.get_sourceip(), sip_addr);
    } else if (fl2.get_destip() == dip_addr) {
        EXPECT_EQ(fl2.get_sourceip(), sip_addr);
    }
    f->ClearList();

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    client->Reset();
    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));
    vmut->ClearCount();
}

TEST_F(UveVmUveTest, VmName_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->VmUveCount());

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
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify send_count after VM addition
    EXPECT_EQ(1U, vmut->send_count());

    //Verify Uve
    VmEntry *vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    UveVirtualMachineAgent *uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_STREQ(uve1->get_vm_name().c_str(), "");

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

    //Trigger UVE send
    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify UVE
    uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_STREQ(uve1->get_vm_name().c_str(), "vm1");
    EXPECT_EQ(2U, vmut->send_count());
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    uint32_t send_count = vmut->send_count();

    //Trigger UVE send
    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    //Verify that vm name is not sent in VM UVE
    uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_TRUE((vmut->send_count() > send_count));
    EXPECT_STREQ(uve1->get_vm_name().c_str(), "");

    //other cleanup
    util_.VnDelete(input[0].vn_id);
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    DelNode("virtual-network", "vn1");
    DelInstanceIp("instance0");
    client->WaitForIdle();

    util_.EnqueueSendVmUveTask();
    client->WaitForIdle();

    WAIT_FOR(1000, 500, ((vmut->VmUveCount() == 0U)));

    //Verify UVE
    EXPECT_EQ(1U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false, true,
                      (10 * 60 * 1000), (10 * 60 * 1000), true, true,
                      (10 * 60 * 1000));

    usleep(10000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
