/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <boost/assign/list_of.hpp>

#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/mirror_table.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include "vr_flow.h"

using namespace std;
using namespace boost::assign;

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

void RouterIdDepInit(Agent *agent) {
}

class L2Ecmpest : public ::testing::Test {
public:
    void SetUp() {
        boost::system::error_code ec;
        bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        channel_ = bgp_peer_->GetAgentXmppChannel();
        agent_ = Agent::GetInstance();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
    }
    void TearDown() {
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 2);
        DeleteBgpPeer(bgp_peer_);
        DelIPAM("vn1");
        client->WaitForIdle();
    }

    Agent *agent_;
    BgpPeer *bgp_peer_;
    AgentXmppChannel *channel_;
};

TEST_F(L2Ecmpest, Controller_verify_evpn) {
    client->WaitForIdle();
    struct PortInfo input1[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:01:01:01", 10, 10}
    };

    client->Reset();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    //Now add remote route with ECMP comp NH
    Ip4Address ip1 = Ip4Address::from_string("9.9.9.1");
    Ip4Address ip2 = Ip4Address::from_string("9.9.9.2");
    TunnelNHKey *nh_key = new TunnelNHKey(agent_->fabric_vrf_name(),
                                          agent_->router_id(),
                                          ip1, false,
                                          TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    TunnelNHKey *nh_key_2 = new TunnelNHKey(agent_->fabric_vrf_name(),
                                            agent_->router_id(),
                                            ip2, false,
                                            TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr_2(nh_key_2);

    ComponentNHKeyPtr component_nh_key(new ComponentNHKey(1000,
                                                          nh_key_ptr));
    ComponentNHKeyPtr component_nh_key_2(new ComponentNHKey(1001,
                                                            nh_key_ptr_2));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(component_nh_key);
    comp_nh_list.push_back(component_nh_key_2);

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                        comp_nh_list, "vrf10"));
    nh_req.data.reset(new CompositeNHData());
    const MacAddress mac("00:00:00:11:11:11");
    Ip4Address prefix = Ip4Address::from_string("18.18.18.0");
    VnListType vn_list;
    vn_list.insert("vn10");
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(bgp_peer_, vn_list, EcmpLoadBalance(),
                                TagList(), SecurityGroupList(),
                                PathPreference(100, PathPreference::LOW, false,
                                               false),
                                (1 << TunnelType::MPLS_GRE),
                                nh_req, mac.ToString());

    //ECMP create component NH
    EvpnAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, "vrf10", mac, prefix,
                                             32, 0, data);
    client->WaitForIdle();
    EvpnRouteEntry *rt = EvpnRouteGet("vrf10", mac, prefix, 0);
    EXPECT_TRUE(rt != NULL);
    const CompositeNH *cnh =
        static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    const TunnelNH *tnh = static_cast<const TunnelNH *>(cnh->GetNH(0));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    tnh = static_cast<const TunnelNH *>(cnh->GetNH(1));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    //cleanup
    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "vrf10", mac, prefix, 32, 0,
                                   new ControllerVmRoute(bgp_peer_));
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

TEST_F(L2Ecmpest, Controller_verify_l2) {
    client->WaitForIdle();
    struct PortInfo input1[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:01:01:01", 10, 10}
    };

    client->Reset();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    //Now add remote route with ECMP comp NH
    Ip4Address ip1 = Ip4Address::from_string("9.9.9.1");
    Ip4Address ip2 = Ip4Address::from_string("9.9.9.2");
    TunnelNHKey *nh_key = new TunnelNHKey(agent_->fabric_vrf_name(),
                                          agent_->router_id(),
                                          ip1, false,
                                          TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    TunnelNHKey *nh_key_2 = new TunnelNHKey(agent_->fabric_vrf_name(),
                                            agent_->router_id(),
                                            ip2, false,
                                            TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr_2(nh_key_2);

    ComponentNHKeyPtr component_nh_key(new ComponentNHKey(1000,
                                                          nh_key_ptr));
    ComponentNHKeyPtr component_nh_key_2(new ComponentNHKey(1001,
                                                            nh_key_ptr_2));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(component_nh_key);
    comp_nh_list.push_back(component_nh_key_2);

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                        comp_nh_list, "vrf10"));
    nh_req.data.reset(new CompositeNHData());
    const MacAddress mac("00:00:00:11:11:11");
    Ip4Address prefix = Ip4Address::from_string("18.18.18.0");
    VnListType vn_list;
    vn_list.insert("vn10");
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(bgp_peer_, vn_list, EcmpLoadBalance(),
                                TagList(), SecurityGroupList(),
                                PathPreference(100, PathPreference::LOW, false,
                                               false),
                                (1 << TunnelType::MPLS_GRE),
                                nh_req, mac.ToString());

    //ECMP create component NH
    EvpnAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, "vrf10", mac, prefix,
                                             32, 0, data);
    client->WaitForIdle();
    EvpnRouteEntry *rt = EvpnRouteGet("vrf10", mac, prefix, 0);
    EXPECT_TRUE(rt != NULL);
    const CompositeNH *cnh =
        static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    const TunnelNH *tnh = static_cast<const TunnelNH *>(cnh->GetNH(0));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    tnh = static_cast<const TunnelNH *>(cnh->GetNH(1));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    BridgeRouteEntry *l2_rt = L2RouteGet("vrf10", mac);
    EXPECT_TRUE(l2_rt != NULL);
    EXPECT_TRUE(l2_rt->GetActiveNextHop() == cnh);

    //cleanup
    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "vrf10", mac, prefix, 32, 0,
                                   new ControllerVmRoute(bgp_peer_));
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

TEST_F(L2Ecmpest, Controller_tunnel_to_ecmp) {
    client->WaitForIdle();
    struct PortInfo input1[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:01:01:01", 10, 10}
    };
    MacAddress mac("00:00:00:11:11:11");
    Ip4Address prefix = Ip4Address::from_string("18.18.18.0");
    //Tunnel
    Ip4Address ip1 = Ip4Address::from_string("9.9.9.1");
    Ip4Address ip2 = Ip4Address::from_string("9.9.9.2");

    client->Reset();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    BridgeTunnelRouteAdd(bgp_peer_, "vrf10", TunnelType::AllType(), ip1,
                         (MplsTable::kStartLabel + 60), mac,
                         IpAddress::from_string("18.18.18.0"), 32);
    client->WaitForIdle();
    EvpnRouteEntry *rt = EvpnRouteGet("vrf10", mac, prefix, 0);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::TUNNEL);

    //Now add remote route with ECMP comp NH
    TunnelNHKey *nh_key = new TunnelNHKey(agent_->fabric_vrf_name(),
                                          agent_->router_id(),
                                          ip1, false,
                                          TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    TunnelNHKey *nh_key_2 = new TunnelNHKey(agent_->fabric_vrf_name(),
                                            agent_->router_id(),
                                            ip2, false,
                                            TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr_2(nh_key_2);

    ComponentNHKeyPtr component_nh_key(new ComponentNHKey(1000,
                                                          nh_key_ptr));
    ComponentNHKeyPtr component_nh_key_2(new ComponentNHKey(1001,
                                                            nh_key_ptr_2));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(component_nh_key);
    comp_nh_list.push_back(component_nh_key_2);

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                        comp_nh_list, "vrf10"));
    nh_req.data.reset(new CompositeNHData());
    VnListType vn_list;
    vn_list.insert("vn10");
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(bgp_peer_, vn_list, EcmpLoadBalance(),
                                TagList(), SecurityGroupList(),
                                PathPreference(100, PathPreference::LOW, false,
                                               false),
                                (1 << TunnelType::MPLS_GRE),
                                nh_req, mac.ToString());

    //ECMP create component NH
    EvpnAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, "vrf10", mac, prefix,
                                             32, 0, data);
    client->WaitForIdle();
    rt = EvpnRouteGet("vrf10", mac, prefix, 0);
    EXPECT_TRUE(rt != NULL);
    const CompositeNH *cnh =
        static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    const TunnelNH *tnh = static_cast<const TunnelNH *>(cnh->GetNH(0));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    tnh = static_cast<const TunnelNH *>(cnh->GetNH(1));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    //cleanup
    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "vrf10", mac, prefix, 32, 0,
                                   new ControllerVmRoute(bgp_peer_));
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

TEST_F(L2Ecmpest, Controller_ecmp_to_tunnel) {
    client->WaitForIdle();
    struct PortInfo input1[] = {
        {"vnet10", 10, "1.1.1.1", "00:00:00:01:01:01", 10, 10}
    };
    MacAddress mac("00:00:00:11:11:11");
    Ip4Address prefix = Ip4Address::from_string("18.18.18.0");
    //Tunnel
    Ip4Address ip1 = Ip4Address::from_string("9.9.9.1");
    Ip4Address ip2 = Ip4Address::from_string("9.9.9.2");

    client->Reset();
    AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
    client->WaitForIdle();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    //Now add remote route with ECMP comp NH
    TunnelNHKey *nh_key = new TunnelNHKey(agent_->fabric_vrf_name(),
                                          agent_->router_id(),
                                          ip1, false,
                                          TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr(nh_key);
    TunnelNHKey *nh_key_2 = new TunnelNHKey(agent_->fabric_vrf_name(),
                                            agent_->router_id(),
                                            ip2, false,
                                            TunnelType::MPLS_GRE);
    std::auto_ptr<const NextHopKey> nh_key_ptr_2(nh_key_2);

    ComponentNHKeyPtr component_nh_key(new ComponentNHKey(1000,
                                                          nh_key_ptr));
    ComponentNHKeyPtr component_nh_key_2(new ComponentNHKey(1001,
                                                            nh_key_ptr_2));
    ComponentNHKeyList comp_nh_list;
    comp_nh_list.push_back(component_nh_key);
    comp_nh_list.push_back(component_nh_key_2);

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::ECMP, true,
                                        comp_nh_list, "vrf10"));
    nh_req.data.reset(new CompositeNHData());
    VnListType vn_list;
    vn_list.insert("vn10");
    ControllerEcmpRoute *data =
        new ControllerEcmpRoute(bgp_peer_, vn_list, EcmpLoadBalance(),
                                TagList(), SecurityGroupList(),
                                PathPreference(100, PathPreference::LOW, false,
                                               false),
                                (1 << TunnelType::MPLS_GRE),
                                nh_req, mac.ToString());

    //ECMP create component NH
    EvpnAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, "vrf10", mac, prefix,
                                             32, 0, data);
    client->WaitForIdle();
    EvpnRouteEntry *rt = EvpnRouteGet("vrf10", mac, prefix, 0);
    EXPECT_TRUE(rt != NULL);
    const CompositeNH *cnh =
        static_cast<const CompositeNH *>(rt->GetActiveNextHop());
    const TunnelNH *tnh = static_cast<const TunnelNH *>(cnh->GetNH(0));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);
    tnh = static_cast<const TunnelNH *>(cnh->GetNH(1));
    EXPECT_TRUE(tnh->GetTunnelType().GetType() == TunnelType::MPLS_GRE);

    //Transition to tunnel
    BridgeTunnelRouteAdd(bgp_peer_, "vrf10", TunnelType::AllType(), ip1,
                         (MplsTable::kStartLabel + 60), mac,
                         Ip4Address::from_string("18.18.18.0"), 32);
    client->WaitForIdle();
    rt = EvpnRouteGet("vrf10", mac, prefix, 0);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::TUNNEL);

    //cleanup
    EvpnAgentRouteTable::DeleteReq(bgp_peer_, "vrf10", mac, prefix, 32, 0,
                                   new ControllerVmRoute(bgp_peer_));
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

TEST_F(L2Ecmpest, no_local_l2_ecmp) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2},
    };

    MacAddress mac("00:00:00:01:01:01");
    Ip4Address prefix = Ip4Address::from_string("1.1.1.1");
    CreateVmportWithEcmp(input1, 2);
    client->WaitForIdle();

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    EvpnRouteEntry *rt = EvpnRouteGet("vrf1", mac, prefix, 0);
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(rt->FindPath(agent_->ecmp_peer()) == NULL);
    const NextHop *nh = rt->GetActiveNextHop();
    EXPECT_TRUE(nh->GetType() != NextHop::COMPOSITE);

    DeleteVmportEnv(input1, 5, true);
    client->WaitForIdle();
    WAIT_FOR(100, 1000, (VrfFind("vrf1") == false));
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
