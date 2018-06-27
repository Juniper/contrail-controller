/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <base/logging.h>

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <oper/vrf.h>
#include <pugixml/pugixml.hpp>
#include <services/arp_proto.h>
#include <test/test_cmn_util.h>
#include <test/pkt_gen.h>
#include "vr_types.h"
#include <services/services_sandesh.h>
#include "mac_learning/mac_learning.h"
#include "mac_learning/mac_learning_proto.h"
#include "ksync/ksync_sock.h"
#include "ksync/ksync_sock_user.h"
#include "pkt/test/test_pkt_util.h"

// Create vm-port and vn
struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

class PbbRouteTest : public ::testing::Test {
public:
    PbbRouteTest() {
        agent_ = Agent::GetInstance();
        server1_ip_ = Ip4Address::from_string("1.1.1.10");
        b_smac_ = MacAddress::FromString("00:00:0b:01:01:01");
        b_dmac_ = MacAddress::FromString("00:00:0b:01:01:02");
        c_smac_ = MacAddress::FromString("00:00:0c:01:01:01");
        c_dmac_ = MacAddress::FromString("00:00:0c:01:01:02");
    };

    virtual void SetUp() {
        AddVrf("evpn_vrf");
        AddVrf("pbb_vrf");
        client->WaitForIdle();
        CreateVmportEnv(input, 2);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
        AddBridgeDomain("bridge1", 1, 1);
        client->WaitForIdle();
        AddVn("vn1", 1, true);
        AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
        client->WaitForIdle();

        bgp_peer_ = CreateBgpPeer(Ip4Address(1), "BgpPeer1");
    }

    virtual void TearDown() {
        DelNode("bridge-domain", "bridge1");
        DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
        client->WaitForIdle();
        DelVrf("evpn_vrf");
        DelVrf("pbb_vrf");
        client->WaitForIdle();
        DeleteVmportEnv(input, 2, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VrfFind("vrf1", true));
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        DeleteBgpPeer(bgp_peer_);
    }

protected:
    Agent *agent_;
    MacAddress c_smac_;
    MacAddress c_dmac_;
    MacAddress b_smac_;
    MacAddress b_dmac_;
    Ip4Address  server1_ip_;
    BgpPeer *bgp_peer_;
};

TEST_F(PbbRouteTest, RouteTest1) {
    //Add a BMAC route in evpn vrf
    BridgeTunnelRouteAdd(bgp_peer_, "evpn_vrf", TunnelType::AllType(),
                         server1_ip_, (MplsTable::kStartLabel + 60), b_smac_,
                         Ip4Address(0), 32);
    client->WaitForIdle();

    PBBRoute *data = new PBBRoute(VrfKey("evpn_vrf"), b_smac_, 0, VnListType(),
                                  SecurityGroupList(), TagList());
    EvpnAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, "pbb_vrf", c_smac_,
                                             Ip4Address(0), 32, 0, data);
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("evpn_vrf", b_smac_, Ip4Address(0), 0);
    EvpnRouteEntry *pbb_rt = EvpnRouteGet("pbb_vrf", c_smac_, Ip4Address(0), 0);

    EXPECT_TRUE(evpn_rt != NULL);
    EXPECT_TRUE(pbb_rt != NULL);

    EXPECT_TRUE(pbb_rt->GetActiveNextHop()->GetType() == NextHop::PBB);
    const PBBNH *pbb_nh = static_cast<const PBBNH *>(pbb_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->dest_bmac() == b_smac_);
    EXPECT_TRUE(pbb_nh->vrf()->GetName() == "evpn_vrf");
    EXPECT_TRUE(pbb_nh->child_nh() == evpn_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->child_nh()->GetType() == NextHop::TUNNEL);
    EXPECT_TRUE(pbb_nh->label() == MplsTable::kStartLabel + 60);

    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "evpn_vrf", b_smac_,
            Ip4Address(0), 32, 0, NULL);
    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "pbb_vrf", c_smac_,
            Ip4Address(0), 32, 0, NULL);
    client->WaitForIdle();
}

TEST_F(PbbRouteTest, RouteTest2) {
    const VrfEntry *vrf = VrfGet("vrf1:00000000-0000-0000-0000-000000000001");
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxIpPBBPacket(0, "10.10.10.10", agent_->router_id().to_string().c_str(),
                  intf->l2_label(), vrf->vrf_id(), b_smac_, b_dmac_, 0,
                  c_smac_, c_dmac_, "1.1.1.1", "1.1.1.10");
    client->WaitForIdle();

    EvpnRouteEntry *pbb_rt =
        EvpnRouteGet("vrf1:00000000-0000-0000-0000-000000000001", c_smac_,
                     Ip4Address(0), 0);
    EXPECT_TRUE(pbb_rt != NULL);
    EXPECT_TRUE(pbb_rt->GetActiveNextHop()->GetType() == NextHop::PBB);
    const PBBNH *pbb_nh = static_cast<const PBBNH *>(pbb_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->child_nh()->GetType() == NextHop::DISCARD);

    //Add a BMAC route in evpn vrf
    BridgeTunnelRouteAdd(bgp_peer_, "vrf1", TunnelType::AllType(),
                         server1_ip_, (MplsTable::kStartLabel + 60), b_smac_,
                         Ip4Address(0), 32);
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", b_smac_, Ip4Address(0), 0);
    pbb_nh = static_cast<const PBBNH *>(pbb_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->dest_bmac() == b_smac_);
    EXPECT_TRUE(pbb_nh->vrf()->GetName() == "vrf1");
    EXPECT_TRUE(pbb_nh->child_nh() == evpn_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->child_nh()->GetType() == NextHop::TUNNEL);
    EXPECT_TRUE(pbb_nh->label() == MplsTable::kStartLabel + 60);

    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "vrf1", b_smac_,
            Ip4Address(0), 32, 0, NULL);
    client->WaitForIdle();

    EXPECT_TRUE(EvpnRouteGet("vrf1", c_smac_, Ip4Address(0), 0) == NULL);
}

TEST_F(PbbRouteTest, RouteTest3) {
    const VrfEntry *vrf = VrfGet("vrf1:00000000-0000-0000-0000-000000000001");
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxIpPBBPacket(0, "10.10.10.10", agent_->router_id().to_string().c_str(),
                  intf->l2_label(), vrf->vrf_id(), b_smac_, b_dmac_, 0,
                  c_smac_, c_dmac_, "1.1.1.1", "1.1.1.10");
    client->WaitForIdle();

    EvpnRouteEntry *pbb_rt =
        EvpnRouteGet("vrf1:00000000-0000-0000-0000-000000000001", c_smac_,
                     Ip4Address(0), 0);
    EXPECT_TRUE(pbb_rt != NULL);
    EXPECT_TRUE(pbb_rt->GetActiveNextHop()->GetType() == NextHop::PBB);
    const PBBNH *pbb_nh = static_cast<const PBBNH *>(pbb_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->child_nh()->GetType() == NextHop::DISCARD);

    //Add a BMAC route in evpn vrf
    BridgeTunnelRouteAdd(bgp_peer_, "vrf1", TunnelType::AllType(),
                         server1_ip_, (MplsTable::kStartLabel + 60), b_smac_,
                         Ip4Address(0), 32, 0, true);
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", b_smac_, Ip4Address(0), 0);
    pbb_nh = static_cast<const PBBNH *>(pbb_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->dest_bmac() == b_smac_);
    EXPECT_TRUE(pbb_nh->vrf()->GetName() == "vrf1");
    EXPECT_TRUE(pbb_nh->child_nh() == evpn_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->child_nh()->GetType() == NextHop::TUNNEL);
    EXPECT_TRUE(pbb_nh->label() == MplsTable::kStartLabel + 60);
    EXPECT_TRUE(pbb_nh->etree_leaf() == true);

    BridgeTunnelRouteAdd(bgp_peer_, "vrf1", TunnelType::AllType(),
            server1_ip_, (MplsTable::kStartLabel + 60), b_smac_,
            Ip4Address(0), 32, 0, false);
    client->WaitForIdle();
    EXPECT_TRUE(pbb_nh->etree_leaf() == false);

    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "vrf1", b_smac_,
                                   Ip4Address(0), 32, 0, NULL);
    client->WaitForIdle();

    EXPECT_TRUE(EvpnRouteGet("vrf1", c_smac_, Ip4Address(0), 0) == NULL);
}

TEST_F(PbbRouteTest, RouteTest4) {
    const VrfEntry *vrf = VrfGet("vrf1:00000000-0000-0000-0000-000000000001");
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxIpPBBPacket(0, "10.10.10.10", agent_->router_id().to_string().c_str(),
                  intf->l2_label(), vrf->vrf_id(), b_smac_, b_dmac_, 0,
                  c_smac_, c_dmac_, "1.1.1.1", "1.1.1.10");
    client->WaitForIdle();

    EvpnRouteEntry *pbb_rt =
        EvpnRouteGet("vrf1:00000000-0000-0000-0000-000000000001", c_smac_,
                     Ip4Address(0), 0);
    EXPECT_TRUE(pbb_rt != NULL);
    EXPECT_TRUE(pbb_rt->GetActiveNextHop()->GetType() == NextHop::PBB);
    const PBBNH *pbb_nh = static_cast<const PBBNH *>(pbb_rt->GetActiveNextHop());
    EXPECT_TRUE(pbb_nh->child_nh()->GetType() == NextHop::DISCARD);

    DelNode("bridge-domain", "bridge1");
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();

    EXPECT_TRUE(EvpnRouteGet("vrf1", c_smac_, Ip4Address(0), 0) == NULL);
}

TEST_F(PbbRouteTest, RouteTest5) {
    const VrfEntry *vrf = VrfGet("vrf1:00000000-0000-0000-0000-000000000001");
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxIpPBBPacket(0, "10.10.10.10", agent_->router_id().to_string().c_str(),
                  intf->l2_label(), vrf->vrf_id(), b_smac_, b_dmac_, 0,
                  c_smac_, c_dmac_, "1.1.1.1", "1.1.1.10");
    client->WaitForIdle();

    EvpnRouteEntry *pbb_rt =
        EvpnRouteGet("vrf1:00000000-0000-0000-0000-000000000001", c_smac_,
                     Ip4Address(0), 0);
    EXPECT_TRUE(pbb_rt != NULL);

    AddBridgeDomain("bridge1", 1, 2);
    client->WaitForIdle();

    EXPECT_TRUE(EvpnRouteGet("vrf1", c_smac_, Ip4Address(0), 0) == NULL);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
