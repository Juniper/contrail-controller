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
#include <oper/interface.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>
#include <uve/agent_uve.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "pkt/test/test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "vr_types.h"
#include <uve/test/interface_uve_table_test.h>
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

struct PortInfo stats_if[] = {
        {"test0", 8, "3.1.1.1", "00:00:00:01:01:01", 6, 3},
        {"test1", 9, "4.1.1.2", "00:00:00:01:01:02", 6, 4},
};

VmInterface *test0, *test1;
VmInterface *flow0;
VmInterface *flow1;
VmInterface *flow2;

VmInterface *flowa;
VmInterface *flowb;
VmInterface *flowc;

void RouterIdDepInit(Agent *agent) {
}

class InterfaceUveTest : public ::testing::Test {
public:
    InterfaceUveTest() : util_(), peer_(NULL), agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
    }
    void InterfaceSetup() {
        client->Reset();
        CreateVmportEnv(stats_if, 2);
        client->WaitForIdle(10);

        EXPECT_TRUE(VmPortActive(stats_if, 0));
        EXPECT_TRUE(VmPortActive(stats_if, 1));

        test0 = VmInterfaceGet(stats_if[0].intf_id);
        assert(test0);
        test1 = VmInterfaceGet(stats_if[1].intf_id);
        assert(test1);
    }
    void InterfaceCleanup() {
        client->Reset();
        DeleteVmportEnv(stats_if, 2, 1);
        client->WaitForIdle(10);
    }

    void FlowSetUp() {
        EXPECT_EQ(0U, flow_proto_->FlowCount());
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
        AddLink("virtual-machine-interface", "flow0", "floating-ip",
                "fip1");
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
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        client->Reset();
        CreateVmportEnv(fip_input1, 2, 1);
        client->WaitForIdle(5);
        CreateVmportFIpEnv(fip_input2, 1, 0);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(fip_input1, 0));
        EXPECT_TRUE(VmPortActive(fip_input1, 1));
        EXPECT_TRUE(VmPortPolicyEnable(fip_input1, 0));
        EXPECT_TRUE(VmPortPolicyEnable(fip_input1, 1));
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

    void CreatePeer() {
        boost::system::error_code ec;
        peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    }

    void DeletePeer() {
        DeleteBgpPeer(peer_);
    }

    Agent *agent() {return agent_;}

    TestUveUtil util_;
    BgpPeer *peer_;
    Agent *agent_;
    FlowProto *flow_proto_;
};

TEST_F(InterfaceUveTest, VmIntfAddDel_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    //Add VN
    util_.VnAdd(input[0].vn_id);

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Add VM
    util_.VmAdd(input[0].vm_id);
    client->WaitForIdle();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify send_count after VM addition
    EXPECT_TRUE((vmut->send_count() > 0));
    uint32_t send_count = vmut->send_count();

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

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE 
    const VmInterface *vmi = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(vmi != NULL);
    const UveVMInterfaceAgent uve1 = vmut->last_sent_uve();
    EXPECT_TRUE((vmut->send_count() > send_count));

    //Verify interface config name
    std::string intf_entry = uve1.get_name();
    string cfg_name = vmi->cfg_name();
    EXPECT_STREQ(cfg_name.c_str(), intf_entry.c_str());

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

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

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));

    //Verify UVE 
    EXPECT_EQ(1U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

TEST_F(InterfaceUveTest, VmSriovIntfAddDel_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    //Add VN
    util_.VnAdd(input[0].vn_id);

    // Config Sriov Port add
    util_.ConfigSriovPortAdd(input);

    //Add VM
    util_.VmAdd(input[0].vm_id);
    client->WaitForIdle();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify send_count after VM addition
    EXPECT_TRUE((vmut->send_count() > 0));
    uint32_t send_count = vmut->send_count();

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

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE
    const VmInterface *vmi = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(vmi != NULL);
    const UveVMInterfaceAgent uve1 = vmut->last_sent_uve();
    EXPECT_TRUE((vmut->send_count() > send_count));

    //Verify interface config name
    std::string intf_entry = uve1.get_name();
    string cfg_name = vmi->cfg_name();
    EXPECT_STREQ(cfg_name.c_str(), intf_entry.c_str());


    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

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

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));

    //Verify UVE
    EXPECT_EQ(1U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

/* Vm Dissassociation from VMI, VM Delete --> Vm Add, Vm Reassociation */
TEST_F(InterfaceUveTest, VmIntfAddDel_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    //Add VN
    util_.VnAdd(input[0].vn_id);

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Add VM
    util_.VmAdd(input[0].vm_id);

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify send_count after VM addition
    EXPECT_EQ(1U, vmut->send_count());

    //Add necessary objects and links to make vm-intf active
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface",
            "vnet1");
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

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_EQ(2U, vmut->send_count());

    //Disassociate VM from VMI and delete the VM
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    util_.VmDelete(input[0].vm_id);
    client->WaitForIdle();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify that no additional interface UVEs are sent
    EXPECT_TRUE((vmut->send_count() == 3U));
    EXPECT_EQ(0U, vmut->delete_count());
    
    //Add the VM back and re-associate it with same VMI
    util_.VmAdd(input[0].vm_id);
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_TRUE((vmut->send_count() >= 4U));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_TRUE((vmut->send_count() >= 5U));

    //Activate the interface again
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_TRUE((vmut->send_count() >= 6U));

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_TRUE((vmut->send_count() >= 7U));

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

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));

    //Verify UVE 
    EXPECT_EQ(1U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

TEST_F(InterfaceUveTest, VmIntfAddDel_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    // Nova Port add message
    util_.NovaPortAdd(input);

    //Verify that entry for interface is not added to our tree until config is
    //received.
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    // Config Port add
    util_.ConfigPortAdd(input);
    client->WaitForIdle();

    //Verify that entry for interface is added to our tree after config is
    //received.
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 1U)));
    EXPECT_EQ(1U, vmut->InterfaceUveCount());

    // Config Port delete
    DelNode("virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that on deletion of interface config, the entry is marked for
    //delete in our tree.
    InterfaceUveTable::UveInterfaceEntry* entry = vmut->GetUveInterfaceEntry
        ("vnet1");
    EXPECT_TRUE(entry->deleted_);

    // Nova port delete
    IntfCfgDel(input, 0);
    client->WaitForIdle();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

TEST_F(InterfaceUveTest, FipStats_1) {
    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    FlowSetUp();

    //Verify that stats FIP entry is created on FIP config add
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0));

    FlowStatsCollector *fsc = Agent::GetInstance()->flow_stats_manager()->
                                  default_flow_stats_collector();
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
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm4_ip,
                        vm1_fip, 1, 0, 0, rev->key().nh));

    FlowExportInfo *info = fsc->FindFlowExportInfo(f1->uuid());
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rev->uuid());
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    //Update FIP stats which resuts in creation of stats FIP entry
    fsc->UpdateFloatingIpStats(info, 300, 3);
    fsc->UpdateFloatingIpStats(rinfo, 300, 3);

    //Fetch stats FIP entry and verify its statistics
    const InterfaceUveTable::FloatingIp *fip = vmut->GetVmIntfFip(flow0,
                                               vm1_fip, "default-project:vn4");
    EXPECT_TRUE(fip != NULL);
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //cleanup
    FlowTearDown();
    RemoveFipConfig();
    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));
    vmut->ClearCount();
}

// Delete FIP config and verify stats entry for that FIP is removed from
// our data-structures
TEST_F(InterfaceUveTest, FipStats_2) {
    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    FlowSetUp();
    //Verify that stats FIP entry is created on FIP config add
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0));

    FlowStatsCollector *fsc = Agent::GetInstance()->flow_stats_manager()->
                                  default_flow_stats_collector();
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
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm4_ip,
                        vm1_fip, 1, 0, 0, rev->key().nh));

    FlowExportInfo *info = fsc->FindFlowExportInfo(f1->uuid());
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rev->uuid());
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    //Update FIP stats which resuts in creation of stats FIP entry
    fsc->UpdateFloatingIpStats(info, 300, 3);
    fsc->UpdateFloatingIpStats(rinfo, 300, 3);

    //Remove floating-IP configuration
    RemoveFipConfig();
    WAIT_FOR(1000, 500, ((flow0->floating_ip_list().list_.size() == 0U)));

    //Verify that stats FIP entry is removed
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flow0));

    //cleanup
    FlowTearDown();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));
    vmut->ClearCount();
}

// Update FIP stats and verify dispatched VM Stats UVE has the expected stats
TEST_F(InterfaceUveTest, FipStats_3) {
    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    FlowSetUp();

    //Verify that stats FIP entry is created on FIP config add
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0));

    FlowStatsCollector *fsc = Agent::GetInstance()->flow_stats_manager()->
                                  default_flow_stats_collector();
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
    EXPECT_EQ(2U, flow_proto_->FlowCount());
    EXPECT_EQ(3U, vmut->InterfaceUveCount());

    //Send the required UVEs which clears changed flag set on UVE objects.
    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    vmut->ClearCount();

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm4_ip,
                        vm1_fip, 1, 0, 0, rev->key().nh));

    FlowExportInfo *info = fsc->FindFlowExportInfo(f1->uuid());
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rev->uuid());
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    //Update FIP stats which resuts in creation of stats FIP entry
    fsc->UpdateFloatingIpStats(info, 300, 3);
    fsc->UpdateFloatingIpStats(rinfo, 300, 3);
    client->WaitForIdle();

    //Verify that stats FIP entry is created
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0));

    //Fetch stats FIP entry and verify its statistics
    const InterfaceUveTable::FloatingIp *fip = vmut->GetVmIntfFip(flow0,
                                              vm1_fip, "default-project:vn4");
    EXPECT_TRUE(fip != NULL);
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //Trigger Interface UVE send
    vmut->ClearCount();
    vmut->SendInterfaceStats();
    EXPECT_EQ(3U, vmut->send_count());
    EXPECT_EQ(0U, vmut->delete_count());

    //Verify UVE
    UveVMInterfaceAgent *uve1 = vmut->InterfaceUveObject(flow0);
    EXPECT_EQ(1U, uve1->get_fip_agg_stats().size());

    //Verify stats values in UVE
    const VmFloatingIPStats &stats = uve1->get_fip_agg_stats().front();
    EXPECT_EQ(3U, stats.get_in_pkts());
    EXPECT_EQ(3U, stats.get_out_pkts());
    EXPECT_EQ(300U, stats.get_in_bytes());
    EXPECT_EQ(300U, stats.get_out_bytes());

    //cleanup
    FlowTearDown();
    RemoveFipConfig();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));
    vmut->ClearCount();
}

// Update FIP stats and verify dispatched VM Stats UVE has the expected stats.
// The VMs representing source and destination IP of flow should have a FIP each
// assigned from a common third VN's floating IP pool. Both VMs are part of same
// compute node (Local Flow case)
TEST_F(InterfaceUveTest, FipStats_4) {
    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    FlowSetUp2();

    //Verify that stats FIP entry is created on FIP config add
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flowa));

    FlowStatsCollector *fsc = Agent::GetInstance()->flow_stats_manager()->
                                  default_flow_stats_collector();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm_a_ip, vm_c_fip2, 1, 0, 0, "vrf6",
                        flowa->id(), 1),
            {
                new VerifyNat(vm_b_ip, vm_c_fip1, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf6")->vrf_id(), vm_a_ip, vm_c_fip2, 1, 0, 0,
                        flowa->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("vrf7")->vrf_id(), vm_b_ip, vm_c_fip1, 1, 0, 0,
                        rev->key().nh));

    FlowExportInfo *info = fsc->FindFlowExportInfo(f1->uuid());
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rev->uuid());
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    //Update FIP stats which resuts in creation of stats FIP entry
    fsc->UpdateFloatingIpStats(info, 300, 3);
    fsc->UpdateFloatingIpStats(rinfo, 300, 3);

    //Fetch stats FIP entry and verify its statistics
    const InterfaceUveTable::FloatingIp *fip = vmut->GetVmIntfFip(flowa,
                                            vm_c_fip1, "default-project:vn8");
    EXPECT_TRUE(fip != NULL);
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //Trigger VM UVE send
    vmut->ClearCount();
    vmut->SendInterfaceStats();
    EXPECT_EQ(3U, vmut->send_count());
    EXPECT_EQ(0U, vmut->delete_count());

    //Verify UVE
    UveVMInterfaceAgent *uve1 = vmut->InterfaceUveObject(flowa);
    EXPECT_EQ(1U, uve1->get_fip_agg_stats().size());

    //Verify stats values in UVE
    const VmFloatingIPStats &stats = uve1->get_fip_agg_stats().front();
    EXPECT_EQ(3U, stats.get_in_pkts());
    EXPECT_EQ(3U, stats.get_out_pkts());
    EXPECT_EQ(300U, stats.get_in_bytes());
    EXPECT_EQ(300U, stats.get_out_bytes());

    //cleanup
    FlowTearDown2();
    RemoveFipConfig2();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));

    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flowa));
}

// Update FIP stats and verify dispatched VM Stats UVE has the expected stats.
// The VMs representing source and destination IP of flow should have a FIP each
// assigned from a common third VN's floating IP pool. The destination VM is in
// different compute node. (Non Local flow case)
TEST_F(InterfaceUveTest, FipStats_5) {
    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());

    FlowSetUp();

    //Verify that stats FIP entry is created on FIP config add
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0));

    FlowStatsCollector *fsc = Agent::GetInstance()->flow_stats_manager()->
                                  default_flow_stats_collector();
    CreatePeer();
    util_.CreateRemoteRoute("default-project:vn4:vn4", remote_vm_fip,
                      remote_router_ip, 30, "default-project:vn4", peer_);
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, vm1_ip, remote_vm_fip, 1, 0, 0, "vrf5",
                        flow0->id(), 1001),
            {}
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, remote_vm_fip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm_fip, vm1_fip, 1, 0, 0,
                        rev->key().nh));

    FlowExportInfo *info = fsc->FindFlowExportInfo(f1->uuid());
    FlowExportInfo *rinfo = fsc->FindFlowExportInfo(rev->uuid());
    EXPECT_TRUE(info != NULL);
    EXPECT_TRUE(rinfo != NULL);
    //Update FIP stats which resuts in creation of stats FIP entry
    fsc->UpdateFloatingIpStats(info, 300, 3);
    fsc->UpdateFloatingIpStats(rinfo, 300, 3);

    //Fetch stats FIP entry and verify its statistics
    const InterfaceUveTable::FloatingIp *fip = vmut->GetVmIntfFip(flow0,
                                              vm1_fip, "default-project:vn4");
    EXPECT_TRUE(fip != NULL);
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //Trigger VM UVE send
    vmut->ClearCount();
    vmut->SendInterfaceStats();
    WAIT_FOR(1000, 500, (3U == vmut->send_count()));
    EXPECT_EQ(0U, vmut->delete_count());

    //Verify UVE
    UveVMInterfaceAgent *uve1 = vmut->InterfaceUveObject(flow0);
    EXPECT_EQ(1U, uve1->get_fip_agg_stats().size());

    //Verify stats values in UVE
    const VmFloatingIPStats &stats = uve1->get_fip_agg_stats().front();
    EXPECT_EQ(3U, stats.get_in_pkts());
    EXPECT_EQ(3U, stats.get_out_pkts());
    EXPECT_EQ(300U, stats.get_in_bytes());
    EXPECT_EQ(300U, stats.get_out_bytes());
    //cleanup
    FlowTearDown();
    util_.DeleteRemoteRoute("default-project:vn4:vn4", remote_vm_fip, peer_);
    RemoveFipConfig();
    DeletePeer();

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));

    vmut->ClearCount();
}

/* Verify that VM name is not NULL-string in the interface UVE sent as part of
 * VM UVE */
TEST_F(InterfaceUveTest, VmNameInInterfaceList) {
    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->InterfaceUveCount());
    struct PortInfo input[] = {
        {"vmi1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    //Add physical-device and physical-interface and add their association
    util_.VmAdd(input[0].vm_id);
    AddPhysicalDevice("prouter1", 1);
    AddPhysicalInterface("pi1", 1, "pid1");
    AddLogicalInterface("li1", 1, "lid1");
    AddPort("vmi1", 1);
    AddLink("physical-router", "prouter1", "physical-interface", "pi1");
    AddLink("physical-interface", "pi1", "logical-interface", "li1");
    AddLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    AddLink("virtual-machine-interface", "vmi1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) != NULL));
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") != NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));

    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();

    //Verify UVE
    VmEntry *vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    VmInterface *vmi = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(vmi != NULL);
    EXPECT_TRUE(vmi->vm() != NULL);
    EXPECT_TRUE(vmi->vm() == vm);
    EXPECT_TRUE(!vm->GetCfgName().empty());
    UveVMInterfaceAgent uve1 = vmut->last_sent_uve();

    //Verify that VMI does not have VM Name set
    //EXPECT_TRUE((vmi->vm_name() == agent_->NullString()));
    EXPECT_TRUE((vmi->vm_name().empty()));

    //Verify that UVE has sent VM name for VMI
    EXPECT_TRUE((uve1.get_vm_name() == vm->GetCfgName()));

    //cleanup
    DelLink("virtual-machine-interface", "vmi1", "virtual-machine", "vm1");
    DelNode("virtual-machine", "vm1");
    //Disassociate VMI from logical-interface
    DelLink("virtual-machine-interface", "vmi1", "logical-interface", "li1");
    //Disassociate logical-interface from physical_interface
    DelLink("physical-interface", "pi1", "logical-interface", "li1");
    //Disassociate physical-device from physical-interface
    DelLink("physical-router", "prouter1", "physical-interface", "pi1");
    //Delete physical-device and physical-interface
    DelPort("vmi1");
    DeleteLogicalInterface("li1");
    DeletePhysicalInterface("pi1");
    DeletePhysicalDevice("prouter1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (PhysicalInterfaceGet("pi1") == NULL));
    WAIT_FOR(1000, 500, (PhysicalDeviceGet(1) == NULL));
    WAIT_FOR(1000, 500, (LogicalInterfaceGet(1, "li1") == NULL));

    //clear counters at the end of test case
    client->Reset();
    util_.EnqueueSendVmiUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((vmut->InterfaceUveCount() == 0U)));
    vmut->ClearCount();
}

TEST_F(InterfaceUveTest, IntfStatsTest) {
    InterfaceSetup();
    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->stats_collector());
    collector->interface_stats_responses_ = 0;
    InterfaceUveTableTest *vmut = static_cast<InterfaceUveTableTest *>
        (Agent::GetInstance()->uve()->interface_uve_table());
    vmut->ClearCount();
    EXPECT_EQ(0U, vmut->send_count());

    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);

    EXPECT_TRUE(VmPortStatsMatch(test0, 0,0,0,0));
    EXPECT_TRUE(VmPortStatsMatch(test1, 0,0,0,0));

    //Change the stats
    KSyncSockTypeMap::IfStatsUpdate(test0->id(), 1, 50, 0, 1, 20, 0);
    KSyncSockTypeMap::IfStatsUpdate(test1->id(), 1, 50, 0, 1, 20, 0);

    //Wait for stats to be updated
    collector->interface_stats_responses_ = 0;

    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verify the updated flow stats
    EXPECT_TRUE(VmPortStatsMatch(test0, 1, 50, 1, 20));
    EXPECT_TRUE(VmPortStatsMatch(test1, 1, 50, 1, 20));

    //Reset the stats so that repeat of this test case works
    KSyncSockTypeMap::IfStatsSet(test0->id(), 0, 0, 0, 0, 0, 0);
    KSyncSockTypeMap::IfStatsSet(test1->id(), 0, 0, 0, 0, 0, 0);

    vmut->ClearCount();
    collector->interface_stats_responses_ = 0;
    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verify 2 UVE sends have happened (one for each VMI)
    //Because bandwith of VMI is changing from non-zero to zero, we expect
    //UVEs to be sent for each VMI
    EXPECT_EQ(2U, vmut->send_count());

    // Trigger stats collection again without change in stats
    vmut->ClearCount();
    collector->interface_stats_responses_ = 0;
    util_.EnqueueAgentStatsCollectorTask(1);
    //Wait until agent_stats_collector() is run
    WAIT_FOR(100, 1000, (collector->interface_stats_responses_ >= 1));
    client->WaitForIdle(3);

    //Verify that no UVE sends have happened
    EXPECT_EQ(0U, vmut->send_count());
    InterfaceCleanup();
}

TEST_F(InterfaceUveTest, PhysicalIntfAddDel_1) {
}

TEST_F(InterfaceUveTest, LogicalIntfAddDel_1) {
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
