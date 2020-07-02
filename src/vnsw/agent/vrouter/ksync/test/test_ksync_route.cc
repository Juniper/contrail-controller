/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "oper/path_preference.h"
#include "vrouter/ksync/route_ksync.h"

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10", true},
};

class TestKSyncRoute : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        CreateVmportEnv(input, 2, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));

        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();

        vnet1_ = static_cast<VmInterface *>(VmPortGet(1));
        vnet2_ = static_cast<VmInterface *>(VmPortGet(2));

        vrf1_obj_ = agent_->ksync()->vrf_ksync_obj();
        vrf_listener_id_ = vrf1_obj_->vrf_listener_id();

        VrfTable *table = static_cast<VrfTable *>(agent_->vrf_table());
        VrfKSyncObject::VrfState *state;

        vrf1_ = vnet1_->vrf();
        vrf1_uc_table_ = static_cast<InetUnicastAgentRouteTable *>
            (vrf1_->GetInet4UnicastRouteTable());
        state = static_cast<VrfKSyncObject::VrfState *>
            (vrf1_->GetState(table, vrf_listener_id_));
        vrf1_rt_obj_ = state->inet4_uc_route_table_;

        vrf1_evpn_table_ = static_cast<EvpnAgentRouteTable *>
            (vrf1_->GetEvpnRouteTable());
        vrf1_bridge_table_ = static_cast<BridgeAgentRouteTable *>
            (vrf1_->GetBridgeRouteTable());
        vrf1_bridge_rt_obj_ = state->bridge_route_table_;

        VrfEntry *fabric_vrf =
            table->FindVrfFromName(agent_->fabric_vrf_name());
        fabric_uc_table_ = static_cast<InetUnicastAgentRouteTable *>
            (fabric_vrf->GetInet4UnicastRouteTable());
        state = static_cast<VrfKSyncObject::VrfState *>
            (fabric_vrf->GetState(table, vrf_listener_id_));
        fabric_rt_obj_ = state->inet4_uc_route_table_;
        interface_obj = agent_->ksync()->interface_ksync_obj();
        boost::system::error_code ec;
        bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                  "xmpp channel");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true, 1);
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        WAIT_FOR(1000, 100, (VmPortFindRetDel(1) == false));
        WAIT_FOR(1000, 100, (VmPortFindRetDel(2) == false));

        WAIT_FOR(1000, 100, (VmPortGet(1) == NULL));
        WAIT_FOR(1000, 100, (VmPortGet(2) == NULL));
        WAIT_FOR(1000, 100, (VnGet(1) == NULL));
        DeleteBgpPeer(bgp_peer_);
    }

    void AddRemoteRoute(Peer *peer, const IpAddress &addr, int plen,
                        const string &vn) {
        SecurityGroupList sg_list;
        PathPreference path_pref;
        ControllerVmRoute *data = NULL;
        VnListType vn_list;
        vn_list.insert(vn);
        data = ControllerVmRoute::MakeControllerVmRoute
            (bgp_peer_, agent_->fabric_vrf_name(), agent_->router_id(),
             "vrf1", Ip4Address::from_string("10.10.10.2"), TunnelType::GREType(),
             100, MacAddress(), vn_list, sg_list, TagList(),
             path_pref, false, EcmpLoadBalance(), false);
        vrf1_uc_table_->AddRemoteVmRouteReq(peer, "vrf1", addr, plen, data);
        client->WaitForIdle();
    }

    void AddRemoteEvpnRoute(Peer *peer, const MacAddress &mac,
                            const IpAddress &addr, uint32_t ethernet_tag,
                            const string &vn) {
        SecurityGroupList sg_list;
        PathPreference path_pref;
        ControllerVmRoute *data = NULL;

        VnListType vn_list;
        vn_list.insert(vn);
        data = ControllerVmRoute::MakeControllerVmRoute
            (bgp_peer_, agent_->fabric_vrf_name(), agent_->router_id(),
             "vrf1", Ip4Address::from_string("10.10.10.2"), TunnelType::GREType(),
             100, MacAddress(), vn_list, sg_list, TagList(), path_pref, false,
             EcmpLoadBalance(), false);
        vrf1_evpn_table_->AddRemoteVmRouteReq(peer, "vrf1", mac, addr, 32,
                                              ethernet_tag, data);
        client->WaitForIdle();
    }

    Agent *agent_;
    VnswInterfaceListener *vnswif_;
    VmInterface *vnet1_;
    VmInterface *vnet2_;
    DBTableBase::ListenerId vrf_listener_id_;
    VrfEntry *vrf1_;
    VrfKSyncObject *vrf1_obj_;
    InetUnicastAgentRouteTable *vrf1_uc_table_;
    InetUnicastAgentRouteTable *fabric_uc_table_;
    EvpnAgentRouteTable *vrf1_evpn_table_;
    BridgeAgentRouteTable *vrf1_bridge_table_;
    RouteKSyncObject *vrf1_rt_obj_;
    RouteKSyncObject *vrf1_bridge_rt_obj_;
    RouteKSyncObject *fabric_rt_obj_;
    InterfaceKSyncObject *interface_obj;
};

// proxy_arp_ and flood_ flags for interface-route
TEST_F(TestKSyncRoute, vm_interface_route_1) {
    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(vnet1_->primary_ip_addr());
    EXPECT_TRUE(rt != NULL);

    MacAddress mac("00:00:00:01:01:01");
    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));
    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, vnet1_->primary_ip_addr(),
                                            NULL) == mac);

    ksync->BuildArpFlags(rt, rt->GetActivePath(), vnet1_->mac());
    EXPECT_TRUE(ksync->proxy_arp());
    EXPECT_FALSE(ksync->flood());
}

// proxy_arp_ and flood_ flags for interface-route when MAC not stitched
TEST_F(TestKSyncRoute, vm_interface_route_2) {
    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(vnet1_->primary_ip_addr());
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildArpFlags(rt, rt->GetActivePath(), MacAddress());
    EXPECT_FALSE(ksync->proxy_arp());
    EXPECT_TRUE(ksync->flood());
}

// proxy_arp_ and flood_ flags for remote route
TEST_F(TestKSyncRoute, remote_route_1) {
    client->WaitForIdle();
    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.100"));
    AddRemoteRoute(bgp_peer_, addr, 32, "vn1");

    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildArpFlags(rt, rt->GetActivePath(), vnet1_->mac());
    EXPECT_TRUE(ksync->proxy_arp());
    EXPECT_FALSE(ksync->flood());

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1", addr, 32,
                              (new ControllerVmRoute(bgp_peer_)));
    client->WaitForIdle();
}

// proxy_arp_ and flood_ flags for remote-route when MAC not stitched
TEST_F(TestKSyncRoute, remote_route_2) {
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.100"));
    AddRemoteRoute(bgp_peer_, addr, 32, "vn1");

    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildArpFlags(rt, rt->GetActivePath(), MacAddress());
    EXPECT_FALSE(ksync->proxy_arp());
    EXPECT_TRUE(ksync->flood());

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1", addr, 32,
                              (new ControllerVmRoute(bgp_peer_)));
    DelIPAM("vn1");
    client->WaitForIdle();
}

// dhcp_flood flag for remote EVPN route
TEST_F(TestKSyncRoute, remote_evpn_route_1) {
    MacAddress vmi_mac(input[0].mac);
    BridgeRouteEntry *vmi_rt = vrf1_bridge_table_->FindRoute(vmi_mac);
    EXPECT_TRUE(vmi_rt != NULL);
    std::auto_ptr<RouteKSyncEntry> ksync_vmi(new RouteKSyncEntry(
                                             vrf1_bridge_rt_obj_, vmi_rt));
    ksync_vmi->Sync(vmi_rt);
    EXPECT_FALSE(ksync_vmi->flood_dhcp()); // flood DHCP not set when VMI exists

    uint32_t ethernet_tag = 1000;
    MacAddress mac("00:01:02:03:04:05");
    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.100"));
    AddRemoteEvpnRoute(bgp_peer_, mac, addr, ethernet_tag, "vn1");

    BridgeRouteEntry *rt = vrf1_bridge_table_->FindRoute(mac);
    EXPECT_TRUE(rt != NULL);

    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, addr, NULL) == mac);
    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_bridge_rt_obj_, rt));
    ksync->Sync(rt);
    EXPECT_TRUE(ksync->flood_dhcp()); // flood DHCP set for MAC without VMI

    vrf1_evpn_table_->DeleteReq(bgp_peer_, "vrf1", mac, addr, 32, ethernet_tag,
                                (new ControllerVmRoute(bgp_peer_)));
    client->WaitForIdle();
    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, addr, NULL)
                == MacAddress::ZeroMac());
}

// proxy_arp_ and flood_ flags for route with different VNs
TEST_F(TestKSyncRoute, different_vn_1) {
    IpAddress addr = IpAddress(Ip4Address::from_string("2.2.2.100"));
    AddRemoteRoute(bgp_peer_, addr, 32, "Vn3");

    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildArpFlags(rt, rt->GetActivePath(), vnet1_->mac());
    EXPECT_TRUE(ksync->proxy_arp());
    EXPECT_FALSE(ksync->flood());

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1", addr, 32,
                              (new ControllerVmRoute(bgp_peer_)));
    client->WaitForIdle();
}

// Validate flags from the replacement route
TEST_F(TestKSyncRoute, replacement_rt_1) {
    IpAddress addr1 = IpAddress(Ip4Address::from_string("2.2.2.100"));
    AddRemoteRoute(bgp_peer_, addr1, 32, "Vn3");

    InetUnicastRouteEntry *rt1 = vrf1_uc_table_->FindLPM(addr1);
    EXPECT_TRUE(rt1 != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync1(new RouteKSyncEntry(vrf1_rt_obj_, rt1));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt1));

    ksync1->BuildArpFlags(rt1, rt1->GetActivePath(), vnet1_->mac());
    EXPECT_TRUE(ksync1->proxy_arp());
    EXPECT_FALSE(ksync1->flood());

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    IpAddress addr2 = IpAddress(Ip4Address::from_string("1.1.1.100"));
    AddRemoteRoute(bgp_peer_, addr2, 32, "vn1");

    InetUnicastRouteEntry *rt2 = vrf1_uc_table_->FindLPM(addr2);
    EXPECT_TRUE(rt2 != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync2(new RouteKSyncEntry(vrf1_rt_obj_, rt2));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt2));

    ksync2->BuildArpFlags(rt2, rt2->GetActivePath(), MacAddress());
    EXPECT_FALSE(ksync2->proxy_arp());
    EXPECT_TRUE(ksync2->flood());

    std::auto_ptr<RouteKSyncEntry> ksync3(new RouteKSyncEntry(vrf1_rt_obj_, rt2));
    ksync3->CopyReplacementData(NULL, ksync2.get());
    EXPECT_FALSE(ksync3->proxy_arp());
    EXPECT_TRUE(ksync3->flood());

    ksync3->CopyReplacementData(NULL, ksync1.get());
    EXPECT_TRUE(ksync3->proxy_arp());
    EXPECT_FALSE(ksync3->flood());

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1", addr1, 32,
                              (new ControllerVmRoute(bgp_peer_)));
    DelIPAM("vn1");
    client->WaitForIdle();

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1", addr2, 32,
                              (new ControllerVmRoute(bgp_peer_)));
    client->WaitForIdle();
}

TEST_F(TestKSyncRoute, no_replacement_rt_1) {
    IpAddress addr1 = IpAddress(Ip4Address::from_string("2.2.2.100"));
    AddRemoteRoute(bgp_peer_, addr1, 32, "Vn3");

    InetUnicastRouteEntry *rt1 = vrf1_uc_table_->FindLPM(addr1);
    EXPECT_TRUE(rt1 != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync1(new RouteKSyncEntry(vrf1_rt_obj_, rt1));
    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt1));

    ksync1->BuildArpFlags(rt1, rt1->GetActivePath(), vnet1_->mac());
    EXPECT_TRUE(ksync1->proxy_arp());
    EXPECT_FALSE(ksync1->flood());

    ksync1->CopyReplacementData(NULL, NULL);
    EXPECT_TRUE(ksync1->mac().IsZero());

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1", addr1, 32,
                              (new ControllerVmRoute(bgp_peer_)));
    DelIPAM("vn1");
    client->WaitForIdle();
}

// proxy_arp_ and flood_ flags for IPAM subnet route
TEST_F(TestKSyncRoute, ipam_subnet_route_1) {
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.100"));
    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_FALSE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildArpFlags(rt, rt->GetActivePath(), MacAddress());
    EXPECT_FALSE(ksync->proxy_arp());
    EXPECT_TRUE(ksync->flood());

    DelIPAM("vn1");
    client->WaitForIdle();
}

// proxy_arp_ and flood_ flags for IPAM subnet route exported by Gateway
TEST_F(TestKSyncRoute, ipam_subnet_route_2) {
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.0"));
    AddRemoteRoute(bgp_peer_, addr, 24, "vn1");

    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_FALSE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildArpFlags(rt, rt->GetActivePath(), MacAddress());
    EXPECT_FALSE(ksync->proxy_arp());
    EXPECT_TRUE(ksync->flood());

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1", addr, 32,
                              (new ControllerVmRoute(bgp_peer_)));
    DelIPAM("vn1");
}

// proxy_arp_ and flood_ flags for IPAM subnet ECMP route exported by Gateway
TEST_F(TestKSyncRoute, ecmp_ipam_subnet_route_2) {
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200"},
    };
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    EcmpTunnelRouteAdd(agent_, bgp_peer_, "vrf1", "1.1.1.0", 24,
                       "100.100.100.1", 1, "100.100.100.2", 2, "vn1");
    IpAddress addr = IpAddress(Ip4Address::from_string("1.1.1.100"));
    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(addr);
    EXPECT_TRUE(rt != NULL);

    std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf1_rt_obj_, rt));
    EXPECT_FALSE(vrf1_obj_->RouteNeedsMacBinding(rt));

    ksync->BuildArpFlags(rt, rt->GetActivePath(), MacAddress());
    EXPECT_FALSE(ksync->proxy_arp());
    EXPECT_TRUE(ksync->flood());

    vrf1_uc_table_->DeleteReq(bgp_peer_, "vrf1",
                              IpAddress(Ip4Address::from_string("1.1.1.10")), 24,
                              (new ControllerVmRoute(bgp_peer_)));
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(TestKSyncRoute, ecmp_mac_stitching) {
    struct PortInfo input1[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:00:01:01", 1, 3},
    };

    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    MacAddress mac("00:00:00:00:01:01");
    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, vnet1_->primary_ip_addr(),
                NULL) == mac);

    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();

    MacAddress mac1("00:00:00:01:01:01");
    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, vnet1_->primary_ip_addr(),
                NULL) == mac1);
}

TEST_F(TestKSyncRoute, ecmp_mac_stitching_2) {
    struct PortInfo input1[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:00:01:02", 1, 3},
    };

    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    MacAddress mac("00:00:00:00:01:02");
    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, vnet1_->primary_ip_addr(),
                NULL) == mac);

    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(vnet1_->primary_ip_addr(), 32,
                           vnet1_->id(), vnet1_->vrf()->vrf_id(),
                           vnet1_->vm_mac());
    client->WaitForIdle();

    MacAddress mac1("00:00:00:01:01:01");
    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, vnet1_->primary_ip_addr(),
                NULL) == mac1);

    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();

    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(vrf1_, vnet1_->primary_ip_addr(),
                NULL) == mac1);
}

TEST_F(TestKSyncRoute, evpn_wait_for_traffic) {
    //Send traffic for inet route only
    //such that EVPN route is wait_for_traffic state
    //and verify ...ip route would be still in wait for traffic state
    InetUnicastRouteEntry *rt = vrf1_uc_table_->FindLPM(vnet1_->primary_ip_addr());
    EXPECT_TRUE(rt != NULL);
    EXPECT_TRUE(vrf1_obj_->GetIpMacWaitForTraffic(vrf1_,
                                      vnet1_->primary_ip_addr()));

    Agent::GetInstance()->oper_db()->route_preference_module()->
               EnqueueTrafficSeen(vnet1_->primary_ip_addr(), 32,
                                  vnet1_->id(), vnet1_->vrf()->vrf_id(),
                                  MacAddress::ZeroMac());
    client->WaitForIdle();

    EXPECT_TRUE(vrf1_obj_->GetIpMacWaitForTraffic(vrf1_,
                                   vnet1_->primary_ip_addr()));

    Agent::GetInstance()->oper_db()->route_preference_module()->
               EnqueueTrafficSeen(vnet1_->primary_ip_addr(), 32,
                                  vnet1_->id(), vnet1_->vrf()->vrf_id(),
                                  vnet1_->vm_mac());
    client->WaitForIdle();

    EXPECT_FALSE(vrf1_obj_->GetIpMacWaitForTraffic(vrf1_,
                                   vnet1_->primary_ip_addr()));
}

TEST_F(TestKSyncRoute, FabricRoute) {
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.11");
    AddArp(server_ip.to_string().c_str(), "0a:0b:0c:0d:0e:0f",
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    InetUnicastRouteEntry *rt = RouteGet(agent_->fabric_vrf_name(), server_ip, 32);
    EXPECT_TRUE(rt != NULL);
    EXPECT_FALSE(vrf1_obj_->RouteNeedsMacBinding(rt));

    fabric_uc_table_->DeleteReq(agent_->local_peer(), agent_->fabric_vrf_name(),
                                server_ip, 32, NULL);
    client->WaitForIdle();
}

TEST_F(TestKSyncRoute, IndirectRoute) {
    Ip4Address ip = Ip4Address::from_string("10.1.1.100");
    Ip4Address server_ip = Ip4Address::from_string("10.1.1.11");

    VnListType vn_list;
    agent_->fabric_inet4_unicast_table()->
        AddGatewayRouteReq(agent_->local_peer(), agent_->fabric_vrf_name(),
                           ip, 32, server_ip, vn_list,
                           MplsTable::kInvalidLabel, SecurityGroupList(),
                           TagList(), CommunityList(), true);
    client->WaitForIdle();

    InetUnicastRouteEntry *rt = RouteGet(agent_->fabric_vrf_name(), ip, 32);
    EXPECT_TRUE(rt != NULL);
    EXPECT_FALSE(vrf1_obj_->RouteNeedsMacBinding(rt));

    AddArp(server_ip.to_string().c_str(), "0a:0b:0c:0d:0e:0f",
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    EXPECT_TRUE(vrf1_obj_->RouteNeedsMacBinding(rt));

    MacAddress mac("0a:0b:0c:0d:0e:0f");
    EXPECT_TRUE(vrf1_obj_->GetIpMacBinding(agent_->fabric_vrf(), ip, rt) ==
                mac);

    fabric_uc_table_->DeleteReq(agent_->local_peer(), agent_->fabric_vrf_name(),
                                server_ip, 32, NULL);
    client->WaitForIdle();

    fabric_uc_table_->DeleteReq(agent_->local_peer(), agent_->fabric_vrf_name(),
                                ip, 32, NULL);
    client->WaitForIdle();
}

TEST_F(TestKSyncRoute, ksync_intf_pbb_mac) {
    // By default pbb interface flag is false, expect flag is false in InterfaceKSyncEntry
    EXPECT_FALSE(vnet1_->pbb_interface());

    std::auto_ptr<InterfaceKSyncEntry> ksync(new InterfaceKSyncEntry(interface_obj, vnet1_));
    EXPECT_FALSE(ksync->pbb_interface());
    ksync->Sync(vnet1_);
    EXPECT_FALSE(ksync->pbb_interface());
    // Verify that pbb mac is set to zero mac as pbb_interface flag is set to false for intf
    EXPECT_EQ(ksync->pbb_mac().ToString(), "00:00:00:00:00:00");

    // Set pbb interface flag for vm interface and expect flag is set in InterfaceKSyncEntry
    vnet1_->set_pbb_interface(true);
    EXPECT_TRUE(vnet1_->pbb_interface());
    ksync->Sync(vnet1_);
    EXPECT_TRUE(ksync->pbb_interface());
    // Verify pbb_mac for is set to interface mac as pbb_interface flag is set
    EXPECT_EQ(ksync->pbb_mac().ToString(), vnet1_->vm_mac().ToString());
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
