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
#include "ksync/ksync_sock_user.h"
#include "vr_types.h"
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
    UveVnUveTest() : util_() {
        peer_ = CreateBgpPeer("127.0.0.1", "Bgp Peer");
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
    }
    virtual ~UveVnUveTest() {
        DeleteBgpPeer(peer_);
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
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        client->Reset();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle(5);

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortActive(input, 1));
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 0)));
        WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 1)));

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
        WAIT_FOR(1000, 1000, (VmPortFind(input, 0) == false));
        WAIT_FOR(1000, 1000, (VmPortFind(input, 1) == false));
        EXPECT_EQ(0U, flow_proto_->FlowCount());
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
    Agent *agent_;
    FlowProto *flow_proto_;
};

TEST_F(UveVnUveTest, VnAddDel_1) {
    VnUveTableTest *vnut = static_cast<VnUveTableTest *>
        (Agent::GetInstance()->uve()->vn_uve_table());
    //Add VN
    util_.VnAdd(1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (VnGet(1) != NULL));

    WAIT_FOR(1000, 500, (vnut->VnUveCount() == (2U + 1U)));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->send_count() >= 1U));
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    //Delete VN
    util_.VnDelete(1);

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveCount() == 2U));
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
    WAIT_FOR(1000, 500, (VnGet(1) != NULL));

    WAIT_FOR(1000, 500, (vnut->VnUveCount() == (2U + 1U)));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") != NULL));

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size()); 
    EXPECT_EQ(0U, uve1->get_interface_list().size()); 

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

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

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    //Verify UVE 
    WAIT_FOR(1000, 500, (vnut->send_count() >= 2U));
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

    uint32_t send_count = vnut->send_count();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();

    //Verify that no additional UVEs are sent on VMI deactivation
    WAIT_FOR(1000, 500, (vnut->send_count() == send_count));
    EXPECT_EQ(1U, uve1->get_virtualmachine_list().size());
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    //Delete VN
    util_.VnDelete(input[0].vn_id);

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveCount() == 2U));

    //Verify UVE 
    EXPECT_EQ(1U, vnut->delete_count());

    //other cleanup
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
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
    WAIT_FOR(1000, 5000, (VrfFind("vrf1") == false));
    WAIT_FOR(1000, 5000, (vnut->GetVnUveEntry("vn1") == NULL));

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
    WAIT_FOR(1000, 500, (VnGet(1) != NULL));

    WAIT_FOR(1000, 500, (vnut->VnUveCount() == (2U + 1U)));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->send_count() >= 1U));

    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") != NULL));
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

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();

    //Verify UVE
    WAIT_FOR(1000, 500, (vnut->send_count() >= 2U));
    EXPECT_TRUE((uve1->get_acl().compare(string("acl1")) == 0));

    //Disasociate ACL from VN
    client->Reset();
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();

    //Verify UVE
    WAIT_FOR(1000, 500, (vnut->send_count() >= 3U));
    EXPECT_TRUE((uve1->get_acl().compare(string("")) == 0));

    //Associate VN with a new ACL
    client->Reset();
    AclAdd(2);
    AddLink("virtual-network", "vn1", "access-control-list", "acl2");
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(1));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();

    //Verify UVE
    WAIT_FOR(1000, 500, (vnut->send_count() >= 4U));
    EXPECT_TRUE((uve1->get_acl().compare(string("acl2")) == 0));

    //Disassociate acl and Delete VN
    DelLink("virtual-network", "vn1", "access-control-list", "acl2");
    DelNode("access-control-list", "acl2");
    client->WaitForIdle();
    util_.VnDelete(1);

    //clear counters at the end of test case
    client->Reset();
    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") == NULL));
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
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(2) != NULL));

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    WAIT_FOR(1000, 500, (VnGet(1) != NULL));
    WAIT_FOR(1000, 500, (VnGet(2) != NULL));
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") != NULL));
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn2") != NULL));

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
    WAIT_FOR(1000, 500, (VmInterfaceGet(3) != NULL));

    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (VmInterfaceGet(4) != NULL));

    WAIT_FOR(1000, 500, (3 == vnut->GetVnUveVmCount("vn1")));
    WAIT_FOR(1000, 500, (3 == vnut->GetVnUveInterfaceCount("vn1")));

    client->Reset();
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (2 == vnut->GetVnUveVmCount("vn1")));
    WAIT_FOR(1000, 500, (2 == vnut->GetVnUveInterfaceCount("vn1")));

    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (1 == vnut->GetVnUveVmCount("vn1")));
    WAIT_FOR(1000, 500, (1 == vnut->GetVnUveInterfaceCount("vn1")));

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();

    WAIT_FOR(1000, 500, (0 == vnut->GetVnUveVmCount("vn1")));
    WAIT_FOR(1000, 500, (0 == vnut->GetVnUveInterfaceCount("vn1")));
    WAIT_FOR(1000, 500, (0 == vnut->GetVnUveVmCount("vn2")));
    WAIT_FOR(1000, 500, (0 == vnut->GetVnUveInterfaceCount("vn2")));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") == NULL));
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn2") == NULL));

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
    WAIT_FOR(1000, 500, (VnGet(1) != NULL));
    WAIT_FOR(1000, 500, (VnGet(2) != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(1) != NULL));
    WAIT_FOR(1000, 500, (VmInterfaceGet(2) != NULL));
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));

    WAIT_FOR(1000, 500, (vnut->VnUveCount() == (2U + 2U)));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
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
    WAIT_FOR(1000, 500, (VmInterfaceGet(3) != NULL));

    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (VmInterfaceGet(4) != NULL));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (3 == vnut->GetVnUveVmCount("vn1")));

    WAIT_FOR(500, 1000, (3 == vnut->GetVnUveInterfaceCount("vn1")));
    WAIT_FOR(500, 1000, (3U == uve1->get_virtualmachine_list().size()));
    WAIT_FOR(500, 1000, (3U == uve1->get_interface_list().size()));

    client->Reset();
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (2 == vnut->GetVnUveVmCount("vn1")));
    WAIT_FOR(500, 1000, (2 == vnut->GetVnUveInterfaceCount("vn1")));
    WAIT_FOR(500, 1000, (2U == uve1->get_virtualmachine_list().size()));
    WAIT_FOR(500, 1000, (2U == uve1->get_interface_list().size()));

    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (1 == vnut->GetVnUveVmCount("vn1")));
    WAIT_FOR(500, 1000, (1 == vnut->GetVnUveInterfaceCount("vn1")));
    WAIT_FOR(500, 1000, (1U == uve1->get_virtualmachine_list().size()));
    WAIT_FOR(500, 1000, (1U == uve1->get_interface_list().size()));

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(500, 1000, (0 == vnut->GetVnUveVmCount("vn1")));
    WAIT_FOR(500, 1000, (0 == vnut->GetVnUveInterfaceCount("vn1")));
    WAIT_FOR(500, 1000, (0 == vnut->GetVnUveVmCount("vn2")));
    WAIT_FOR(500, 1000, (0 == vnut->GetVnUveInterfaceCount("vn2")));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") == NULL));
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn2") == NULL));
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
    EXPECT_EQ(4U, flow_proto_->FlowCount());

    //Verify the ingress and egress flow counts
    uint32_t in_count, out_count;
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    const VnEntry *vn = fe->data().vn_entry.get();
    flow_proto_->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(4U, in_count);
    EXPECT_EQ(4U, out_count);

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn5") != NULL));

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn5");
    vnut->SendVnStats(false);
    EXPECT_EQ(4U, uve1->get_ingress_flow_count());
    EXPECT_EQ(4U, uve1->get_egress_flow_count());

    DeleteFlow(flow, 1);
    WAIT_FOR(1000, 1000, ((flow_proto_->FlowCount() == 2U)));
    vnut->SendVnStats(false);
    EXPECT_EQ(2U, uve1->get_ingress_flow_count());
    EXPECT_EQ(2U, uve1->get_egress_flow_count());

    //cleanup
    FlowTearDown();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveCount() == 2U));
    vnut->ClearCount();

    uve1 =  vnut->VnUveObject("vn5");
    EXPECT_TRUE(uve1 == NULL);
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

    VmInterface *intf = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(intf != NULL);
    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, 1, 0, 0, "vrf5",
                        remote_router_ip, intf->label(), 1),
            { 
                new VerifyVn("vn3", "vn5"),
            }
        },
        //Send a TCP flow from remote VM in vn3 to local VM in vn5
        {
            TestFlowPkt(Address::INET, remote_vm4_ip, vm1_ip, IPPROTO_TCP,
                        1006, 1007, "vrf5", remote_router_ip, intf->label(), 2),
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
    flow_proto_->VnFlowCounters(vn, &in_count, &out_count);
    EXPECT_EQ(2U, in_count);
    EXPECT_EQ(2U, out_count);

    //Trigger VN UVE send
    vnut->SendVnStats(false);

    //Verfiy ingress and egress flow counts for local VN "vn5"
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn5");
    EXPECT_EQ(2U, uve1->get_ingress_flow_count());
    EXPECT_EQ(2U, uve1->get_egress_flow_count());

    //Verfiy that local VN "vn3" uve does not exist
    UveVirtualNetworkAgent *uve2 =  vnut->VnUveObject("vn3");
    EXPECT_TRUE(uve2 == NULL);

    //Delete a flow
    DeleteFlow(flow, 1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, ((flow_proto_->FlowCount() == 2U)));

    //Trigger VN UVE send
    vnut->SendVnStats(false);

    //Verfiy ingress and egress flow counts in VN UVEs
    EXPECT_EQ(1U, uve1->get_ingress_flow_count());
    EXPECT_EQ(1U, uve1->get_egress_flow_count());

    //Remove remote VM routes
    util_.DeleteRemoteRoute("vrf5", remote_vm4_ip, peer_);
    client->WaitForIdle();

    FlowTearDown();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveCount() == 2U));
    vnut->ClearCount();

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
    vnut->SendVnStats(false);

    //Verify UVE 
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn1");
    EXPECT_TRUE(uve1 != NULL);
    EXPECT_EQ(0, uve1->get_associated_fip_count()); 

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

    //Trigger VN UVE send
    vnut->SendVnStats(false);
    client->WaitForIdle();

    //Verify UVE 
    WAIT_FOR(1000, 500, (uve1->get_associated_fip_count() == 1U));

    //Add one more floating IP
    AddFloatingIp("fip2", 2, "71.1.1.101");
    AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip2");
    client->WaitForIdle(3);

    //Trigger VN UVE send
    vnut->SendVnStats(false);
    client->WaitForIdle();

    //Verify UVE 
    WAIT_FOR(1000, 500, (uve1->get_associated_fip_count() == 2U));

    //Delete one of the floating-IP
    DelLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet2", "floating-ip", "fip2");
    DelFloatingIp("fip2");
    client->WaitForIdle();

    //Trigger VN UVE send
    vnut->SendVnStats(false);

    //Verify UVE 
    WAIT_FOR(1000, 500, (uve1->get_associated_fip_count() == 1U));

    //Delete the other floating-IP
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    client->WaitForIdle();

    //Trigger VN UVE send
    vnut->SendVnStats(false);

    //Verify UVE 
    WAIT_FOR(1000, 500, (uve1->get_associated_fip_count() == 0));

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
    WAIT_FOR(1000, 500, (VnGet(input[0].vn_id) == NULL));

    //clear counters at the end of test case
    client->Reset();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn1") == NULL));
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
    WAIT_FOR(1000, 500, (VnGet(input[0].vn_id) != NULL));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->send_count() >= 1U));

    //Trigger send of VN stats
    vnut->SendVnStats(false);

    //Verify vrf stats list in UVE
    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject("vn3");
    EXPECT_EQ(0U, uve1->get_vrf_stats_list().size()); 

    //Add VRF and associate it with VN
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn3", "routing-instance", "vrf3");
    client->WaitForIdle();

    //Trigger send of VN stats
    vnut->SendVnStats(false);

    //Verify vrf stats list in UVE
    EXPECT_EQ(1U, uve1->get_vrf_stats_list().size()); 

    //Disassociate VN from VRF
    DelLink("virtual-network", "vn3", "routing-instance", "vrf3");
    client->WaitForIdle();

    //Trigger send of VN stats
    vnut->SendVnStats(false);

    //Verify vrf stats list in UVE
    EXPECT_EQ(0U, uve1->get_vrf_stats_list().size()); 

    //Reassociate VN with VRF
    AddLink("virtual-network", "vn3", "routing-instance", "vrf3");
    client->WaitForIdle();

    //Trigger send of VN stats
    vnut->SendVnStats(false);

    //Verify vrf stats list in UVE
    EXPECT_EQ(1U, uve1->get_vrf_stats_list().size()); 

    //cleanup
    DelLink("virtual-network", "vn3", "routing-instance", "vrf3");
    DelNode("routing-instance", "vrf3");
    util_.VnDelete(input[0].vn_id);

    //clear counters at the end of test case
    client->Reset();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveCount() == 2U));
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
    WAIT_FOR(1000, 500, (VnGet(input[0].vn_id) != NULL));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->send_count() == 1U));

    UveVirtualNetworkAgent *uve1 =  vnut->VnUveObject(agent->linklocal_vn_name());
    EXPECT_EQ(0U, uve1->get_virtualmachine_list().size());
    EXPECT_EQ(0U, uve1->get_interface_list().size());

    // Nova Port add message
    util_.NovaPortAdd(input);

    // Config Port add
    util_.ConfigPortAdd(input);

    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();

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

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->send_count() == 2U));

    //Verify UVE
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

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();

    //Verify that no additional UVEs are sent on VMI deactivation
    WAIT_FOR(1000, 500, (vnut->send_count() == 2U));
    EXPECT_EQ(1U, uve1->get_virtualmachine_list().size());
    EXPECT_EQ(1U, uve1->get_interface_list().size());

    //Delete VN
    DelLink("virtual-network", agent->linklocal_vn_name().c_str(),
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", agent->linklocal_vn_name().c_str(),
            "routing-instance", "vrf1");
    util_.VnDeleteByName(agent->linklocal_vn_name().c_str(), input[0].vn_id);

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

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->delete_count() == 1U));
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
    EXPECT_EQ(4U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify Inter-Vn stats
    InterVnStatsMatch("vn5", "vn5", 4, (4 * 30U), true); //outgoing stats
    InterVnStatsMatch("vn5", "vn5", 4, (4 * 30U), false); //Incoming stats

    const VnUveEntry *entry = vnut->GetVnUveEntry("vn5");
    EXPECT_TRUE((entry->deleted() == false));

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

    entry = vnut->GetVnUveEntry("vn5");
    EXPECT_TRUE((entry->deleted() == false));

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

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn5") == NULL));
    vnut->ClearCount();
    EXPECT_EQ(0U, ksock->flow_map.size());
}

TEST_F(UveVnUveTest, VnBandwidth) {
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
    EXPECT_EQ(4U, flow_proto_->FlowCount());

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify Vn stats (in and out bytes)
    EXPECT_TRUE(util_.FlowBasedVnStatsMatch("vn5", 120, 120));

    const FlowEntry *fe1 = flow[0].pkt_.FlowFetch();
    const FlowEntry *fe2 = flow[1].pkt_.FlowFetch();
    //Inter-VN stats updation when flow stats are updated
    //Change the stats in mock kernel
    KSyncSockTypeMap::IncrFlowStats(fe1->flow_handle(), 1, 180);
    KSyncSockTypeMap::IncrFlowStats(fe2->flow_handle(), 1, 200);
    client->WaitForIdle(10);

    //Invoke FlowStatsCollector to update the stats
    util_.EnqueueFlowStatsCollectorTask();
    client->WaitForIdle(10);

    //Verify Vn stats
    EXPECT_TRUE(util_.FlowBasedVnStatsMatch("vn5", 500, 500));

    const VnEntry *vn = flow0->vn();
    EXPECT_TRUE(vn != NULL);
    VnUveEntry* vne = vnut->GetVnUveEntry("vn5");
    EXPECT_TRUE(vne != NULL);

    //Update prev_time to current_time - 1 sec
    uint64_t t = UTCTimestampUsec() - 1000000;
    vne->set_prev_stats_update_time(t);

    //Trigger framing of UVE message
    UveVirtualNetworkAgent uve;
    vne->FrameVnStatsMsg(vn, uve, false);

    //Verify that UVE msg has bandwidth
    EXPECT_TRUE(uve.get_in_bandwidth_usage() == 4000);
    EXPECT_TRUE(uve.get_out_bandwidth_usage() == 4000);

    FlowTearDown();

    util_.EnqueueSendVnUveTask();
    client->WaitForIdle();
    WAIT_FOR(1000, 500, (vnut->VnUveObject("vn5") == NULL));
    vnut->ClearCount();
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
