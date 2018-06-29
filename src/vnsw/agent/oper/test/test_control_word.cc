/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "test_cmn_util.h"
#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/path_preference.h"
#include "filter/acl.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>

using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd10::2"},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2, "fd10::3"},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
    {"2.2.2.0", 24, "2.2.2.10"}
};

class TestControlWord : public ::testing::Test {
public:
    TestControlWord() {
        agent_ = Agent::GetInstance();
    }

    ~TestControlWord() {
    }

    virtual void SetUp() {
        CreateVmportEnv(input, 2);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        AddIPAM("vn1", ipam_info, 2);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VrfFind("vrf1", true));
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
    }

    void ChangeControlWord(const std::string &vn, bool layer2_control_word) {
        std::stringstream str;
        str << "<layer2-control-word>"<< layer2_control_word
            << "</layer2-control-word>";
        AddNode("virtual-network", "vn1", 1,  str.str().c_str());
        client->WaitForIdle();
    }

protected:
    Peer *peer_;
    Agent *agent_;
};

TEST_F(TestControlWord, InterfaceNH) {
    VmInterface *vm_intf = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vm_intf->layer2_control_word() == false);
    const InterfaceNH *l3_no_policy =
        static_cast<const InterfaceNH *>(vm_intf->l3_interface_nh_no_policy());
    const InterfaceNH *l2_policy =
        static_cast<const InterfaceNH *>(vm_intf->l2_interface_nh_policy());
    const InterfaceNH *l2_no_policy =
        static_cast<const InterfaceNH *>(vm_intf->l2_interface_nh_no_policy());

    EXPECT_TRUE(l3_no_policy->layer2_control_word() == false);
    EXPECT_TRUE(l2_policy->layer2_control_word() == false);
    EXPECT_TRUE(l2_no_policy->layer2_control_word() == false);

    ChangeControlWord("vn1", true);

    EXPECT_TRUE(vm_intf->layer2_control_word() == true);
    EXPECT_TRUE(l3_no_policy->layer2_control_word() == false);
    EXPECT_TRUE(l2_policy->layer2_control_word() == true);
    EXPECT_TRUE(l2_no_policy->layer2_control_word() == true);
}

TEST_F(TestControlWord, LocalBridgeRoute) {
    MacAddress smac(0x00, 0x00, 0x00, 0x01, 0x01, 0x01);
    AgentRoute *rt = L2RouteGet("vrf1", smac);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);

    ChangeControlWord("vn1", true);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == true);

    ChangeControlWord("vn1", false);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);
}

TEST_F(TestControlWord, RemoteBridgeRoute) {
    BgpPeer *bgp_peer_ptr = CreateBgpPeer(Ip4Address(1), "BGP Peer1");

    MacAddress smac(0x00, 0x00, 0x00, 0x01, 0x01, 0xaa);
    Ip4Address server_ip(0x10101010);
    BridgeTunnelRouteAdd(bgp_peer_ptr,
                        "vrf1", TunnelType::AllType(), server_ip,
                        (MplsTable::kStartLabel + 60), smac,
                        Ip4Address::from_string("0.0.0.0"), 32);
    client->WaitForIdle();

    AgentRoute *rt = L2RouteGet("vrf1", smac);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);

    ChangeControlWord("vn1", true);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == true);

    ChangeControlWord("vn1", false);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);

    EvpnAgentRouteTable::DeleteReq(bgp_peer_ptr, "vrf1", smac,
                                   Ip4Address(0), 0, 0, NULL);
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer_ptr);
    client->WaitForIdle();
}

TEST_F(TestControlWord, CompositeNH) {
    MacAddress smac(0xff, 0xff, 0xff, 0xff, 0xff, 0xff);

    AgentRoute *rt = L2RouteGet("vrf1", smac);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);
    const CompositeNH *comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->layer2_control_word() == false);

    ChangeControlWord("vn1", true);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == true);
    EXPECT_TRUE(comp_nh->layer2_control_word() == true);

    ChangeControlWord("vn1", false);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);
    EXPECT_TRUE(comp_nh->layer2_control_word() == false);
}

TEST_F(TestControlWord, BridgeDomain) {
    AddBridgeDomain("bridge1", 1, 1, false);
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();

    BridgeDomainEntry *bd = BridgeDomainGet(1);
    EXPECT_TRUE(bd->vrf() != NULL);

    MacAddress smac(0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
    AgentRoute *rt = L2RouteGet("vrf1:00000000-0000-0000-0000-000000000001",
                                smac);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);
    const CompositeNH *comp_nh =
        dynamic_cast<const CompositeNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(comp_nh->layer2_control_word() == false);

    MplsLabel *mpls =
        agent_->mpls_table()->FindMplsLabel(bd->vrf()->table_label());
    EXPECT_TRUE(mpls->nexthop()->GetType() == NextHop::VRF);
    const VrfNH *vrf_nh = dynamic_cast<const VrfNH *>(mpls->nexthop());
    EXPECT_TRUE(vrf_nh->layer2_control_word() == false);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);

    ChangeControlWord("vn1", true);
    EXPECT_TRUE(vrf_nh->layer2_control_word() == true);
    EXPECT_TRUE(comp_nh->layer2_control_word() == true);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == true);

    ChangeControlWord("vn1", false);
    EXPECT_TRUE(vrf_nh->layer2_control_word() == false);
    EXPECT_TRUE(comp_nh->layer2_control_word() == false);
    EXPECT_TRUE(rt->GetActivePath()->layer2_control_word() == false);

    DelNode("bridge-domain", "bridge1");
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
