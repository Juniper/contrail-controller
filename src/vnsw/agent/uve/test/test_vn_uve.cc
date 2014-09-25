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
#include <uve/test/vn_uve_table_test.h>
#include "uve/test/test_uve_util.h"

using namespace std;

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"
#define remote_vm4_ip "13.1.1.2"
#define remote_router_ip "10.1.1.2"

struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
};

VmInterface *flow0;
VmInterface *flow1;

void RouterIdDepInit(Agent *agent) {
}

class UveVnUveTest : public ::testing::Test {
public:
    UveVnUveTest() : util_(), peer_(NULL) {
    }
    bool InterVnStatsMatch(const string &svn, const string &dvn, uint32_t pkts,
                           uint32_t bytes, bool out) {
        VnUveTableTest *vut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
        const VnUveEntry::VnStatsSet *stats_set = vut->FindInterVnStats(svn);

        if (!stats_set) {
            return false;
        }
        VnUveEntry::VnStatsPtr key(new VnUveEntry::VnStats(dvn, 0, 0, false));
        VnUveEntry::VnStatsSet::iterator it = stats_set->find(key);
        if (it == stats_set->end()) {
            return false;
        }
        VnUveEntry::VnStatsPtr stats_ptr(*it);
        const VnUveEntry::VnStats *stats = stats_ptr.get();
        if (out && stats->out_bytes_ == bytes && stats->out_pkts_ == pkts) {
            return true;
        }
        if (!out && stats->in_bytes_ == bytes && stats->in_pkts_ == pkts) {
            return true;
        }
        return false;
    }
    void FlowSetUp() {
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 1));

        flow0 = VmInterfaceGet(input[0].intf_id);
        assert(flow0);
        flow1 = VmInterfaceGet(input[1].intf_id);
        assert(flow1);
    }

    void FlowTearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle(10);
        client->Reset();
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle(3);
        client->PortDelNotifyWait(2);
        EXPECT_FALSE(VmPortFind(input, 0));
        EXPECT_FALSE(VmPortFind(input, 1));
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    }

    void AclAdd(int id) {
        char acl_name[80];

        sprintf(acl_name, "acl%d", id);
        uint32_t count = Agent::GetInstance()->acl_table()->Size();
        client->Reset();
        AddAcl(acl_name, id);
        EXPECT_TRUE(client->AclNotifyWait(1));
        EXPECT_EQ((count + 1), Agent::GetInstance()->acl_table()->Size());
    }

    TestUveUtil util_;
    BgpPeer *peer_;
};

TEST_F(UveVnUveTest, VnAddDel_1) {
    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    //Add VN
    util_.VnAdd(1);
    client->WaitForIdle();
    EXPECT_EQ(1U, vnut->send_count());

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Delete VN
    util_.VnDelete(1);
    EXPECT_EQ(1U, vnut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vnut->ClearCount();
}

TEST_F(UveVnUveTest, VnIntfAddDel_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    //Add VN
    util_.VnAdd(input[0].vn_id);
    client->WaitForIdle();
    EXPECT_EQ(1U, vnut->send_count());

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Since the port is inactive, verify that no additional VN UVE send has 
    //happened since port addition
    EXPECT_EQ(1U, vnut->send_count());

    //Add necessary objects and links to make vm-intf active
    util_.VmAdd(input[0].vm_id);
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
    EXPECT_EQ(2U, vnut->send_count());
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
    EXPECT_EQ(3U, vnut->send_count());
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Delete VN
    util_.VnDelete(input[0].vn_id);

    //Verify UVE 
    EXPECT_EQ(1U, vnut->delete_count());

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
    vnut->ClearCount();
}

TEST_F(UveVnUveTest, VnChange_1) {
    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    //Add VN
    util_.VnAdd(1);
    client->WaitForIdle();
    EXPECT_EQ(1U, vnut->send_count());

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 
    EXPECT_TRUE((uve1->get_acl().compare(string("acl1")) != 0));

    //Associate an ACL with VN
    client->Reset();
    AclAdd(1);
    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    //Verify UVE
    EXPECT_EQ(2U, vnut->send_count());
    EXPECT_TRUE((uve1->get_acl().compare(string("acl1")) == 0));

    //Disasociate ACL from VN
    client->Reset();
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    //Verify UVE
    EXPECT_EQ(3U, vnut->send_count());
    EXPECT_TRUE((uve1->get_acl().compare(string("")) == 0));

    //Associate VN with a new ACL
    client->Reset();
    AclAdd(2);
    AddLink("virtual-network", "vn1", "access-control-list", "acl2");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    //Verify UVE
    EXPECT_EQ(4U, vnut->send_count());
    EXPECT_TRUE((uve1->get_acl().compare(string("acl2")) == 0));

    //Disassociate acl and Delete VN
    DelLink("virtual-network", "vn1", "access-control-list", "acl2");
    DelNode("access-control-list", "acl2");
    client->WaitForIdle();
    util_.VnDelete(1);
    EXPECT_EQ(1U, vnut->delete_count());

    //clear counters at the end of test case
    client->Reset();
    vnut->ClearCount();
}

TEST_F(UveVnUveTest, VnUVE_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", 2, 2},
    };

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    //Create VM, VN, VRF and Vmport
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    WAIT_FOR(500, 1000, (vnut->GetVnUveVmCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveVmCount("vn2")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 1U);

    EXPECT_EQ(1, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1, vnut->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(1, vnut->GetVnUveVmCount("vn2"));
    EXPECT_EQ(1, vnut->GetVnUveInterfaceCount("vn2"));

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
    EXPECT_EQ(3, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(3, vnut->GetVnUveInterfaceCount("vn1"));

    client->Reset();
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(2, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(2, vnut->GetVnUveInterfaceCount("vn1"));
    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(1, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1, vnut->GetVnUveInterfaceCount("vn1"));

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    EXPECT_EQ(0, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(0, vnut->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(0, vnut->GetVnUveVmCount("vn2"));
    EXPECT_EQ(0, vnut->GetVnUveInterfaceCount("vn2"));
    vnut->ClearCount();
}

TEST_F(UveVnUveTest, VnUVE_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", 2, 2},
    };

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    vnut->ClearCount();
    //Create VM, VN, VRF and Vmport
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    WAIT_FOR(500, 1000, (vnut->GetVnUveVmCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn1")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveVmCount("vn2")) == 1U);
    WAIT_FOR(500, 1000, (vnut->GetVnUveInterfaceCount("vn2")) == 1U);

    EXPECT_EQ(1, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1, vnut->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(1, vnut->GetVnUveVmCount("vn2"));
    EXPECT_EQ(1, vnut->GetVnUveInterfaceCount("vn2"));

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_EQ(1U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    UveVirtualNetworkAgent *uve2 =  vnut->VnUveObject("vn2");
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
    EXPECT_EQ(3, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(3, vnut->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(3U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(3U, uve1->get_interface_list().size()); 

    client->Reset();
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(2, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(2, vnut->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(2U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(2U, uve1->get_interface_list().size()); 

    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(1, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(1, vnut->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(1U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(1U, uve1->get_interface_list().size()); 

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    EXPECT_EQ(0, vnut->GetVnUveVmCount("vn1"));
    EXPECT_EQ(0, vnut->GetVnUveInterfaceCount("vn1"));
    EXPECT_EQ(0, vnut->GetVnUveVmCount("vn2"));
    EXPECT_EQ(0, vnut->GetVnUveInterfaceCount("vn2"));

    uve1 =  vnut->VnUveObject("vn1");
    uve2 =  vnut->VnUveObject("vn2");
    EXPECT_TRUE(uve1 == NULL);
    EXPECT_TRUE(uve2 == NULL);
    EXPECT_EQ(2U, vnut->delete_count());
    vnut->ClearCount();
}

//Flow count test (VMport to VMport - Same VN)
//Flow creation using IP and TCP packets
TEST_F(UveVnUveTest, FlowCount_1) {
    KSyncSockTypeMap *ksock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    EXPECT_EQ(0U, ksock->flow_map.size());
    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    vnut->ClearCount();
    FlowSetUp();
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                       flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200,
                       "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    Agent::GetInstance()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(4U, in_count);
    EXPECT_EQ(4U, out_count);
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn5");
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);
    EXPECT_EQ(4U, uve1->get_ingress_flow_count());
    EXPECT_EQ(4U, uve1->get_egress_flow_count());

    DeleteFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);
    EXPECT_EQ(2U, uve1->get_ingress_flow_count());
    EXPECT_EQ(2U, uve1->get_egress_flow_count());

    //cleanup
    FlowTearDown();
    uve1 =  vnut->VnUveObject("vn5");
    EXPECT_TRUE(uve1 == NULL);
    vnut->ClearCount();
    EXPECT_EQ(0U, ksock->flow_map.size());
}

//Flow count test (IP fabric to VMport - Different VNs)
//Flow creation using IP and TCP packets
TEST_F(UveVnUveTest, FlowCount_2) {
    KSyncSockTypeMap *ksock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    EXPECT_EQ(0U, ksock->flow_map.size());
    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    vnut->ClearCount();
    FlowSetUp();

    /* Add remote VN route to vrf5 */
    util_.CreateRemoteRoute("vrf5", remote_vm4_ip, remote_router_ip, 8, "vn3",
                            peer_);

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, 1, 0, 0, "vrf5",
                        remote_router_ip, 16),
            { 
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, IPPROTO_TCP,
                        1006, 1007, "vrf5", remote_router_ip, 16),
            {
                new VerifyVn("vn3", "vn5"),
            }
        }
    };

    CreateFlow(flow, 2);
    client->WaitForIdle();
    //Verify ingress and egress flow count of local VN "vn5"
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    Agent::GetInstance()->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Trigger VN UVE send
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verfiy ingress and egress flow counts for local VN "vn5"
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn5");
    EXPECT_EQ(2U, uve1->get_ingress_flow_count());
    EXPECT_EQ(2U, uve1->get_egress_flow_count());

    //Verfiy that local VN "vn3" uve does not exist
    UveVirtualNetworkAgent *uve2 =  vnut->VnUveObject("vn3");
    EXPECT_TRUE(uve2 == NULL);

    //Delete a flow
    DeleteFlow(flow, 1);
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Trigger VN UVE send
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);
    //Verfiy ingress and egress flow counts in VN UVEs
    EXPECT_EQ(1U, uve1->get_ingress_flow_count());
    EXPECT_EQ(1U, uve1->get_egress_flow_count());

    //Remove remote VM routes
    util_.DeleteRemoteRoute("vrf5", remote_vm4_ip, peer_);
    client->WaitForIdle();

    FlowTearDown();
    uve1 =  vnut->VnUveObject("vn5");
    EXPECT_TRUE(uve1 == NULL);
    vnut->ClearCount();
    EXPECT_EQ(0U, ksock->flow_map.size());
}

TEST_F(UveVnUveTest, FipCount) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
    };

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    //Add VN
    util_.VnAdd(input[0].vn_id);
    // Nova Port add message
    util_.NovaPortAdd(&input[0]);
    util_.NovaPortAdd(&input[1]);
    // Config Port add
    util_.ConfigPortAdd(&input[0]);
    util_.ConfigPortAdd(&input[1]);
    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Add necessary objects and links to make vm-intf active
    util_.VmAdd(input[0].vm_id);
    util_.VmAdd(input[1].vm_id);
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet2");
    client->WaitForIdle();
    AddVmPortVrf("vnet1", "", 0);
    AddVmPortVrf("vnet2", "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddInstanceIp("instance1", input[1].vm_id, input[1].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    AddLink("virtual-machine-interface", input[1].name,
            "instance-ip", "instance1");
    client->WaitForIdle(3);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vnet2",
            "routing-instance", "vrf1");
    client->WaitForIdle(3);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine-interface-routing-instance", "vnet2",
            "virtual-machine-interface", "vnet2");
    client->WaitForIdle(3);
    EXPECT_TRUE(VmPortActive(input, 0));

    //Trigger VN UVE send
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verify UVE 
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(0, uve1->get_associated_fip_count()); 

    //Create a VN for floating-ip
    client->Reset();
    AddVn("default-project:vn2", 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));
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

    //Trigger VN UVE send
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_EQ(1, uve1->get_associated_fip_count());

    //Add one more floating IP
    AddFloatingIp("fip2", 2, "71.1.1.101");
    AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip2");
    client->WaitForIdle(3);

    //Trigger VN UVE send
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);
    client->WaitForIdle();

    //Verify UVE 
    EXPECT_EQ(2, uve1->get_associated_fip_count());

    //Delete one of the floating-IP
    DelLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet2", "floating-ip", "fip2");
    DelFloatingIp("fip2");
    client->WaitForIdle();

    //Trigger VN UVE send
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verify UVE 
    EXPECT_EQ(1, uve1->get_associated_fip_count());

    //Delete the other floating-IP
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    client->WaitForIdle();

    //Trigger VN UVE send
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verify UVE 
    EXPECT_EQ(0, uve1->get_associated_fip_count());

    //cleanup
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn1");
    DelFloatingIpPool("fip-pool1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet2",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface-routing-instance", "vnet2",
            "virtual-machine-interface", "vnet2");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet2");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet2");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-machine-interface", input[1].name,
            "instance-ip", "instance1");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle(3);

    DelNode("virtual-network", "default-project:vn2");
    DelVrf("default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vnet2");
    client->WaitForIdle(3);
    DelNode("virtual-machine", "vm1");
    DelNode("virtual-machine", "vm2");
    DelNode("routing-instance", "vrf1");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface", "vnet2");
    DelInstanceIp("instance0");
    DelInstanceIp("instance1");
    client->WaitForIdle(3);
    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);
    util_.VnDelete(input[0].vn_id);
    client->WaitForIdle(3);

    //clear counters at the end of test case
    client->Reset();
    vnut->ClearCount();
}

TEST_F(UveVnUveTest, VnVrfAssoDisassoc_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 3, 1},
    };

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    //Add VN
    util_.VnAdd(input[0].vn_id);
    client->WaitForIdle();
    EXPECT_EQ(1U, vnut->send_count());

    //Trigger send of VN stats
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verify vrf stats list in UVE
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn3");
    EXPECT_EQ(0U, uve1->get_vrf_stats_list().size()); 

    //Add VRF and associate it with VN
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn3", "routing-instance", "vrf3");
    client->WaitForIdle();

    //Trigger send of VN stats
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verify vrf stats list in UVE
    EXPECT_EQ(1U, uve1->get_vrf_stats_list().size()); 

    //Disassociate VN from VRF
    DelLink("virtual-network", "vn3", "routing-instance", "vrf3");
    client->WaitForIdle();

    //Trigger send of VN stats
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verify vrf stats list in UVE
    EXPECT_EQ(0U, uve1->get_vrf_stats_list().size()); 

    //Reassociate VN with VRF
    AddLink("virtual-network", "vn3", "routing-instance", "vrf3");
    client->WaitForIdle();

    //Trigger send of VN stats
    Agent::GetInstance()->uve()->vn_uve_table()->SendVnStats(false);

    //Verify vrf stats list in UVE
    EXPECT_EQ(1U, uve1->get_vrf_stats_list().size()); 

    //cleanup
    DelLink("virtual-network", "vn3", "routing-instance", "vrf3");
    DelNode("routing-instance", "vrf3");
    util_.VnDelete(input[0].vn_id);

    //clear counters at the end of test case
    client->Reset();
    vnut->ClearCount();
}

//Only for Xen platform, LinkLocal Vn will be created in agent. Verify
//interface add/delete for LinkLocal Vn results in correct VN UVEs to be sent
TEST_F(UveVnUveTest, LinkLocalVn_Xen) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    Agent *agent = Agent::GetInstance();
    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (agent->uve()->vn_uve_table());
    //Add VN
    util_.VnAddByName(agent->linklocal_vn_name().c_str(), input[0].vn_id);
    client->WaitForIdle();
    EXPECT_EQ(1U, vnut->send_count());

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject(agent->linklocal_vn_name());
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size());
    EXPECT_EQ(0U, uve1->get_interface_list().size());

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Since the port is inactive, verify that no additional VN UVE send has
    //happened since port addition
    EXPECT_EQ(1U, vnut->send_count());

    //Add necessary objects and links to make vm-intf active
    util_.VmAdd(input[0].vm_id);
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", agent->linklocal_vn_name().c_str(), "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", agent->linklocal_vn_name().c_str(), "virtual-machine-interface", "vnet1");
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
    EXPECT_EQ(2U, vnut->send_count());
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
    EXPECT_EQ(3U, vnut->send_count());
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size());
    EXPECT_EQ(0U, uve1->get_interface_list().size());

    //Delete VN
    DelLink("virtual-network", agent->linklocal_vn_name().c_str(),
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", agent->linklocal_vn_name().c_str(),
            "routing-instance", "vrf1");
    util_.VnDeleteByName(agent->linklocal_vn_name().c_str(), input[0].vn_id);

    //Verify UVE
    EXPECT_EQ(1U, vnut->delete_count());

    //other cleanup
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortInactive(input, 0));

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
    vnut->ClearCount();
}

TEST_F(UveVnUveTest, VnThroughput) {
    struct PortInfo input[30];
    VmInterface *intf[30];
    uint8_t size = Agent::GetInstance()->interface_config_table()->Size();
    for (int i = 0; i < 30; i++) {
        int id = (i + 10);
        sprintf(input[i].name, "myvnet%d", id);
        input[i].intf_id = id;
        sprintf(input[i].addr, "1.1.1.%d", id);
        sprintf(input[i].mac, "00:00:00:01:01:%02d", id);
        input[i].vn_id = 8;
        input[i].vm_id = id;
    }
    CreateVmportEnv(input, 30);
    client->WaitForIdle();
    EXPECT_EQ((size + 30), Agent::GetInstance()->interface_config_table()->
                                                 Size());

    AgentStatsCollectorTest *collector = static_cast<AgentStatsCollectorTest *>
        (Agent::GetInstance()->uve()->agent_stats_collector());
    collector->Run();
    client->WaitForIdle();
    for (int i = 0; i < 30; i++) {
        intf[i] = VmInterfaceGet(input[i].intf_id);
        EXPECT_TRUE(intf[i] != NULL);
        EXPECT_TRUE(VmPortStatsMatch(intf[i], 0,0,0,0));
        KSyncSockTypeMap::IfStatsUpdate(intf[i]->id(), 50, 1, 0, 20, 1, 0);
    }

    collector->Run();
    client->WaitForIdle();

    for (int i = 0; i < 30; i++) {
        EXPECT_TRUE(VmPortStatsMatch(intf[i], 50, 1, 20, 1));
    }

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject(intf[0]->vn()->GetName());
    EXPECT_EQ(30U, uve1->get_in_tpkts());
    EXPECT_EQ(30U, uve1->get_out_tpkts());
    EXPECT_EQ((30 * 50), uve1->get_in_bytes());
    EXPECT_EQ((30 * 20), uve1->get_out_bytes());

    //cleanup
    DeleteVmportEnv(input, 30, true);
    client->WaitForIdle();
    EXPECT_EQ(size, Agent::GetInstance()->interface_config_table()->Size());
    vnut->ClearCount();
}

//Inter VN stats test between same VN(VMport to VMport - Same VN)
TEST_F(UveVnUveTest, InterVnStats_1) {
    KSyncSockTypeMap *ksock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    EXPECT_EQ(0U, ksock->flow_map.size());

    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    vnut->ClearCount();
    FlowSetUp();
    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        },
        //Add a TCP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, IPPROTO_TCP, 1000, 200,
                "vrf5", flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 2);
    EXPECT_EQ(4U, Agent::GetInstance()->pkt()->flow_table()->Size());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 4, (4 * 30U), true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 4, (4 * 30U), false); //Incoming stats

    //Trigger dispatch of VN uve
    const VnEntry *vn5 = VnGet(5);
    vnut->SendVnStatsMsg_Test(vn5, false);
    client->WaitForIdle(10);

    //Verify stats in the dispatched UVE
    const UveVirtualNetworkAgent uve = vnut->last_sent_uve();
    vector<InterVnStats> list = uve.get_vn_stats();
    EXPECT_EQ(1U, list.size());
    InterVnStats stats = list.front();
    EXPECT_EQ(4U, stats.get_in_tpkts());
    EXPECT_EQ(4U, stats.get_out_tpkts());
    EXPECT_EQ((4U * 30U), stats.get_in_bytes());
    EXPECT_EQ((4U * 30U), stats.get_out_bytes());


    const FlowEntry *fe1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *fe2 = flow[1].pkt_.FlowFetch();
    //Inter-VN stats updation when flow stats are updated
    //Change the stats in mock kernel
    KSyncSockTypeMap::IncrFlowStats(fe1->flow_handle(), 1, 30);
    KSyncSockTypeMap::IncrFlowStats(fe2->flow_handle(), 1, 30);
    client->WaitForIdle(10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 6, (6 * 30U), true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 6, (6 * 30U), false); //Incoming stats

    //Trigger dispatch of VN uve
    vnut->SendVnStatsMsg_Test(vn5, false);
    client->WaitForIdle(10);

    //Verify stats in the dispatched UVE. Verify only differential stats are sent
    const UveVirtualNetworkAgent uve2 = vnut->last_sent_uve();
    list = uve2.get_vn_stats();
    EXPECT_EQ(1U, list.size());
    stats = list.front();
    EXPECT_EQ(2U, stats.get_in_tpkts());
    EXPECT_EQ(2U, stats.get_out_tpkts());
    EXPECT_EQ((2U * 30), stats.get_in_bytes());
    EXPECT_EQ((2U * 30U), stats.get_out_bytes());

    FlowTearDown();
    vnut->ClearCount();
    UveVirtualNetworkAgent *uve3 =  vnut->VnUveObject("vn5");
    EXPECT_TRUE(uve3 == NULL);
    EXPECT_EQ(0U, ksock->flow_map.size());
}

int main(int argc, char **argv) {
    GETUSERARGS();
    /* Sent AgentStatsCollector and FlowStatsCollector timer intervals to 10
     * minutes each so that they don't get fired during the lifetime of this
     * test suite.
     */
    client = TestInit(init_file, ksync_init, true, false, true,
                      (10 * 60 * 1000), (10 * 60 * 1000));

    usleep(10000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
