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
    }
    void FlowSetUp() {
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
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
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
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

    void FlowSetUp3() {
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);
        //CreateVmportFIpEnv(input2, 1, 0);
        //client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));
        //EXPECT_TRUE(VmPortActive(input2, 0));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
        //flow2 = VmInterfaceGet(input2[0].intf_id);
        //assert(flow2);

        // Configure Floating-IP
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, vm1_fip);
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1",
                "virtual-network", "default-project:vn4");
        AddLink("virtual-machine-interface", "flow0", "floating-ip", "fip1");
        client->WaitForIdle();
        EXPECT_TRUE(flow0->HasFloatingIp());
    }

    void FlowTearDown3() {
        FlushFlowTable();
        client->Reset();

        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(2);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));

        /*client->Reset();
        DeleteVmportFIpEnv(input2, 1, true);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(1);
        EXPECT_FALSE(VmPortFind(input2, 0));*/
    }

    void CreatePeer() {
        boost::system::error_code ec;
        peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    }

    void DeletePeer() {
        DeleteBgpPeer(peer_);
    }

    void CreateRemoteRoute(const char *vrf, const char *remote_vm,
                           const char *serv, int label, const char *vn) {
        boost::system::error_code ec;
        Ip4Address addr = Ip4Address::from_string(remote_vm, ec);
        Ip4Address gw = Ip4Address::from_string(serv, ec);
        Inet4TunnelRouteAdd(peer_, vrf, addr, 32, gw, TunnelType::AllType(), label, vn,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle(2);
        WAIT_FOR(1000, 500, (RouteFind(vrf, addr, 32) == true));
    }

    void DeleteRemoteRoute(const char *vrf, const char *ip) {
        boost::system::error_code ec;
        Ip4Address addr = Ip4Address::from_string(ip, ec);
        agent()->fabric_inet4_unicast_table()->DeleteReq(peer_,
                vrf, addr, 32, NULL);
        client->WaitForIdle();
        WAIT_FOR(1000, 1, (RouteFind(vrf, addr, 32) == false));
    }

    Agent *agent() {return agent_;}

    TestUveUtil util_;
    BgpPeer *peer_;
    Agent *agent_;
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

    //Verify interface UUID
    VmInterfaceAgent intf_entry = uve1->get_interface_list().front();
    VmInterface *vmi = VmInterfaceGet(input[0].intf_id);
    assert(vmi);
    string uuid_str = to_string(vmi->GetUuid());
    EXPECT_STREQ(uuid_str.c_str(), intf_entry.get_uuid().c_str());

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

    //Verify UVE 
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

    //Disassociate VM from VMI and delete the VM
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    util_.VmDelete(input[0].vm_id);
    client->WaitForIdle();
    EXPECT_EQ(3U, vmut->send_count());
    
    //Add the VM back and re-associate it with same VMI
    util_.VmAdd(input[0].vm_id);
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_EQ(5U, vmut->send_count());
    EXPECT_EQ(1U, vmut->delete_count());


    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Verify UVE 
    vm = VmGet(input[0].vm_id);
    EXPECT_TRUE(vm != NULL);
    uve1 =  vmut->VmUveObject(vm);
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(6U, vmut->send_count());
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Activate the interface again
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_EQ(7U, vmut->send_count());
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
    EXPECT_EQ(8U, vmut->send_count());
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

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

    //Verify UVE 
    EXPECT_EQ(2U, vmut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vmut->ClearCount();
}

TEST_F(UveVmUveTest, FipStats_1) {
    FlowSetUp();
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm4_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm4_ip,
                        vm1_fip, 1, 0, 0, rev->key().nh));

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Verify that stats FIP entry is absent until flow stats are updated
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Update FIP stats which resuts in creation of stats FIP entry
    vmut->UpdateFloatingIpStats(f1, 300, 3);
    vmut->UpdateFloatingIpStats(rev, 300, 3);

    //Verify that stats FIP entry is created
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Fetch stats FIP entry and verify its statistics
    const VmUveEntry::FloatingIp *fip = vmut->GetVmIntfFip(flow0->vm(), flow0,
                                               vm1_fip, "default-project:vn4");
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //cleanup
    FlowTearDown();
    RemoveFipConfig();
}

// Delete FIP config and verify stats entry for that FIP is removed from
// our data-structures
TEST_F(UveVmUveTest, FipStats_2) {
    FlowSetUp();
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm4_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm4_ip,
                        vm1_fip, 1, 0, 0, rev->key().nh));

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Verify that stats FIP entry is absent until flow stats are updated
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Update FIP stats which resuts in creation of stats FIP entry
    vmut->UpdateFloatingIpStats(f1, 300, 3);
    vmut->UpdateFloatingIpStats(rev, 300, 3);

    //Verify that stats FIP entry is created
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Remove floating-IP configuration
    RemoveFipConfig();

    //Verify that stats FIP entry is created
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //cleanup
    FlowTearDown();
}

// Update FIP stats and verify dispatched VM Stats UVE has the expected stats
TEST_F(UveVmUveTest, FipStats_3) {
    FlowSetUp();
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, vm4_ip, 1, 0, 0, "vrf5",
                    flow0->id(), 1),
            {
                new VerifyNat(vm4_ip, vm1_fip, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, vm4_ip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("default-project:vn4:vn4")->vrf_id(), vm4_ip,
                        vm1_fip, 1, 0, 0, rev->key().nh));

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Verify that stats FIP entry is absent until flow stats are updated
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Update FIP stats which resuts in creation of stats FIP entry
    vmut->UpdateFloatingIpStats(f1, 300, 3);
    vmut->UpdateFloatingIpStats(rev, 300, 3);

    //Verify that stats FIP entry is created
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Fetch stats FIP entry and verify its statistics
    const VmUveEntry::FloatingIp *fip = vmut->GetVmIntfFip(flow0->vm(), flow0,
                                              vm1_fip, "default-project:vn4");
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //Trigger VM UVE send
    vmut->ClearCount();
    vmut->SendVmStats();
    EXPECT_EQ(3U, vmut->send_count());
    EXPECT_EQ(0U, vmut->delete_count());

    //Verify UVE
    UveVirtualMachineAgent *uve1 = vmut->VmUveObject(flow0->vm());
    EXPECT_EQ(1U, uve1->get_fip_stats_list().size());

    //Verify stats values in UVE
    const VmFloatingIPStats &stats = uve1->get_fip_stats_list().front();
    EXPECT_EQ(3U, stats.get_in_pkts());
    EXPECT_EQ(3U, stats.get_out_pkts());
    EXPECT_EQ(300U, stats.get_in_bytes());
    EXPECT_EQ(300U, stats.get_out_bytes());

    //cleanup
    FlowTearDown();
    RemoveFipConfig();
    vmut->ClearCount();
}

// Update FIP stats and verify dispatched VM Stats UVE has the expected stats.
// The VMs representing source and destination IP of flow should have a FIP each
// assigned from a common third VN's floating IP pool. Both VMs are part of same
// compute node (Local Flow case)
TEST_F(UveVmUveTest, FipStats_4) {
    FlowSetUp2();
    TestFlow flow[] = {
        {
            TestFlowPkt(vm_a_ip, vm_c_fip2, 1, 0, 0, "vrf6",
                        flowa->id(), 1),
            {
                new VerifyNat(vm_b_ip, vm_c_fip1, 1, 0, 0)
            }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf6")->vrf_id(), vm_a_ip, vm_c_fip2, 1, 0, 0,
                        flowa->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("vrf7")->vrf_id(), vm_b_ip, vm_c_fip1, 1, 0, 0,
                        rev->key().nh));

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Verify that stats FIP entry is absent until flow stats are updated
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flowa->vm(), flowa));

    //Update FIP stats which resuts in creation of stats FIP entry
    vmut->UpdateFloatingIpStats(f1, 300, 3);
    vmut->UpdateFloatingIpStats(rev, 300, 3);

    //Verify that stats FIP entry is created
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flowa->vm(), flowa));

    //Fetch stats FIP entry and verify its statistics
    const VmUveEntry::FloatingIp *fip = vmut->GetVmIntfFip(flowa->vm(), flowa,
                                            vm_c_fip1, "default-project:vn8");
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //Trigger VM UVE send
    vmut->ClearCount();
    vmut->SendVmStats();
    EXPECT_EQ(3U, vmut->send_count());
    EXPECT_EQ(0U, vmut->delete_count());

    //Verify UVE
    UveVirtualMachineAgent *uve1 = vmut->VmUveObject(flowa->vm());
    EXPECT_EQ(1U, uve1->get_fip_stats_list().size());

    //Verify stats values in UVE
    const VmFloatingIPStats &stats = uve1->get_fip_stats_list().front();
    EXPECT_EQ(3U, stats.get_in_pkts());
    EXPECT_EQ(3U, stats.get_out_pkts());
    EXPECT_EQ(300U, stats.get_in_bytes());
    EXPECT_EQ(300U, stats.get_out_bytes());

    //cleanup
    FlowTearDown2();
    RemoveFipConfig2();
    vmut->ClearCount();
}

// Update FIP stats and verify dispatched VM Stats UVE has the expected stats.
// The VMs representing source and destination IP of flow should have a FIP each
// assigned from a common third VN's floating IP pool. The destination VM is in
// different compute node. (Non Local flow case)
TEST_F(UveVmUveTest, FipStats_5) {
    FlowSetUp();
    CreatePeer();
    CreateRemoteRoute("default-project:vn4:vn4", remote_vm_fip,
                      remote_router_ip, 30, "default-project:vn4");
    client->WaitForIdle();
    TestFlow flow[] = {
        {
            TestFlowPkt(vm1_ip, remote_vm_fip, 1, 0, 0, "vrf5",
                    flow0->id(), 1001),
            {}
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Verify Floating IP flows are created.
    const FlowEntry *f1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *rev = f1->reverse_flow_entry();
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), vm1_ip, remote_vm_fip, 1, 0, 0,
                        flow0->flow_key_nh()->id()));
    EXPECT_TRUE(FlowGet(VrfGet("vrf5")->vrf_id(), remote_vm_fip, vm1_fip, 1, 0, 0,
                        rev->key().nh));

    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Verify that stats FIP entry is absent until flow stats are updated
    EXPECT_EQ(0U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Update FIP stats which resuts in creation of stats FIP entry
    vmut->UpdateFloatingIpStats(f1, 300, 3);
    vmut->UpdateFloatingIpStats(rev, 300, 3);

    //Verify that stats FIP entry is created
    EXPECT_EQ(1U, vmut->GetVmIntfFipCount(flow0->vm(), flow0));

    //Fetch stats FIP entry and verify its statistics
    const VmUveEntry::FloatingIp *fip = vmut->GetVmIntfFip(flow0->vm(), flow0,
                                              vm1_fip, "default-project:vn4");
    EXPECT_EQ(3U, fip->in_packets_);
    EXPECT_EQ(3U, fip->out_packets_);
    EXPECT_EQ(300U, fip->in_bytes_);
    EXPECT_EQ(300U, fip->out_bytes_);

    //Trigger VM UVE send
    vmut->ClearCount();
    vmut->SendVmStats();
    EXPECT_EQ(3U, vmut->send_count());
    EXPECT_EQ(0U, vmut->delete_count());

    //Verify UVE
    UveVirtualMachineAgent *uve1 = vmut->VmUveObject(flow0->vm());
    EXPECT_EQ(1U, uve1->get_fip_stats_list().size());

    //Verify stats values in UVE
    const VmFloatingIPStats &stats = uve1->get_fip_stats_list().front();
    EXPECT_EQ(3U, stats.get_in_pkts());
    EXPECT_EQ(3U, stats.get_out_pkts());
    EXPECT_EQ(300U, stats.get_in_bytes());
    EXPECT_EQ(300U, stats.get_out_bytes());
    //cleanup
    FlowTearDown();
    DeleteRemoteRoute("default-project:vn4:vn4", remote_vm_fip);
    RemoveFipConfig();
    DeletePeer();
    vmut->ClearCount();
}

// Update FIP stats and verify dispatched VM Stats UVE has the expected stats
TEST_F(UveVmUveTest, VmUVE_Name_1) {
    FlowSetUp();
    VmUveTableTest *vmut = static_cast<VmUveTableTest *>
        (Agent::GetInstance()->uve()->vm_uve_table());

    //Trigger VM UVE send
    vmut->ClearCount();
    vmut->SendVmStats();

    //Verify the count based on number of VMs we have.
    EXPECT_EQ(3U, vmut->send_count());
    EXPECT_EQ(3U, vmut->vm_stats_send_count());

    //Verify that VM name was not empty
    const UveVirtualMachineAgent uve = vmut->last_sent_uve();
    const VirtualMachineStats stats_uve = vmut->last_sent_stats_uve();
    EXPECT_STRNE(uve.get_name().c_str(), "");
    EXPECT_STRNE(stats_uve.get_name().c_str(), "");

    //cleanup
    vmut->ClearCount();
    FlowTearDown();

    //Verify dispatched UVE deletes match the number of VMs deleted
    EXPECT_EQ(3U, vmut->delete_count());
    EXPECT_EQ(3U, vmut->vm_stats_delete_count());

    //Verify that VM name was not empty in delete msg
    const UveVirtualMachineAgent uve2 = vmut->last_sent_uve();
    const VirtualMachineStats stats_uve2 = vmut->last_sent_stats_uve();
    EXPECT_STRNE(uve2.get_name().c_str(), "");
    EXPECT_STRNE(stats_uve2.get_name().c_str(), "");
    vmut->ClearCount();

    RemoveFipConfig();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    usleep(10000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
