//
//  test_forwarding_mode.cc
//  vnsw/agent/test
//
//  Created by Praveen K V
//  Copyright (c) 2015 Contrail Systems. All rights reserved.
//
#include "base/os.h"
#include <base/logging.h>
#include <boost/shared_ptr.hpp>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "kstate/test/test_kstate_util.h"
#include "vr_types.h"

#include <controller/controller_export.h>
#include <boost/assign/list_of.hpp>
using namespace boost::assign;
std::string eth_itf;
bool dhcp_external;

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
};

IpamInfo ipam_info_dhcp_enable[] = {
    {"1.1.1.0", 24, "1.1.1.200", true},
};

IpamInfo ipam_info_dhcp_disable[] = {
    {"1.1.1.0", 24, "1.1.1.200", false},
};

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class ForwardingModeTest : public ::testing::Test {
public:

protected:
    ForwardingModeTest() {
    }

    ~ForwardingModeTest() {
    }

    virtual void SetUp() {
        vrf_name_ = "vrf1";
        eth_name_ = eth_itf;
        default_dest_ip_ = Ip4Address::from_string("0.0.0.0");
        agent_ = Agent::GetInstance();
        bgp_peer_ = NULL;

        if (agent_->router_id_configured()) {
            vhost_ip_ = agent_->router_id();
        } else {
            vhost_ip_ = Ip4Address::from_string("10.1.1.10");
        }
        server1_ip_ = Ip4Address::from_string("10.1.1.11");

        strcpy(local_vm_mac_str_, "00:00:01:01:01:10");
        local_vm_mac_ = MacAddress::FromString(local_vm_mac_str_);
        strcpy(local_vm_ip4_str_, "1.1.1.10");
        local_vm_ip4_ = Ip4Address::from_string(local_vm_ip4_str_);
        strcpy(local_vm_ip6_str_, "fdff::10");
        local_vm_ip6_ = Ip6Address::from_string(local_vm_ip6_str_);

        strcpy(remote_vm_mac_str_, "00:00:01:01:01:11");
        remote_vm_mac_ = MacAddress::FromString(remote_vm_mac_str_);
        strcpy(remote_vm_ip4_str_, "1.1.1.11");
        remote_vm_ip4_ = Ip4Address::from_string(remote_vm_ip4_str_);
        strcpy(remote_vm_ip6_str_, "fdff::11");
        remote_vm_ip6_ = Ip6Address::from_string(remote_vm_ip6_str_);

        client->Reset();
        //Create a VRF
        VrfAddReq(vrf_name_.c_str());
        PhysicalInterface::CreateReq(agent_->interface_table(),
                                eth_name_,
                                agent_->fabric_vrf_name(),
                                PhysicalInterface::FABRIC,
                                PhysicalInterface::ETHERNET, false, nil_uuid(),
                                Ip4Address(0), Interface::TRANSPORT_ETHERNET);
        AddResolveRoute(server1_ip_, 24);
        client->WaitForIdle();
        bgp_peer_ = CreateBgpPeer("127.0.0.1", "remote");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        VrfDelReq(vrf_name_.c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VrfFind(vrf_name_.c_str()) != true));
        DeleteBgpPeer(bgp_peer_);
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(MacAddress &remote_vm_mac, const Ip4Address &ip_addr,
                          const Ip4Address &server_ip,
                          uint32_t label, TunnelType::TypeBmap bmap) {
        //Use any other peer than localvmpeer
        Inet4TunnelRouteAdd(bgp_peer_, vrf_name_, ip_addr, 32, server_ip, 
                            bmap, label+1, vrf_name_,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle();
        BridgeTunnelRouteAdd(bgp_peer_, vrf_name_, bmap, server_ip,
                             label, remote_vm_mac, ip_addr, 32);
        client->WaitForIdle();
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        InetInterfaceKey vhost_key(agent_->vhost_interface()->name());
        agent_->fabric_inet4_unicast_table()->AddResolveRoute(
                agent_->local_peer(),
                agent_->fabric_vrf_name(), server_ip, plen, vhost_key,
                0, false, "", SecurityGroupList());
        client->WaitForIdle();
    }

    void DeleteRoute(const Peer *peer, const std::string &vrf_name,
                     MacAddress &remote_vm_mac, const IpAddress &ip_addr) {
        const BgpPeer *bgp_peer = static_cast<const BgpPeer *>(peer);
        if (bgp_peer) {
            EvpnAgentRouteTable::DeleteReq(peer, vrf_name_, remote_vm_mac,
                                           ip_addr, 0,
                                           (new ControllerVmRoute(bgp_peer)));
        } else {
            EvpnAgentRouteTable::DeleteReq(peer, vrf_name_, remote_vm_mac,
                                           ip_addr, 0, NULL);
        }
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (L2RouteFind(vrf_name, remote_vm_mac, ip_addr) ==
                 false));
        if (bgp_peer) {
            agent_->fabric_inet4_unicast_table()->DeleteReq(peer, vrf_name,
                                                  ip_addr, 32,
                                                  (new ControllerVmRoute(bgp_peer)));
        } else {
            agent_->fabric_inet4_unicast_table()->DeleteReq(peer, vrf_name,
                                                            ip_addr, 32, NULL);
        }
        client->WaitForIdle();
    }

    void VerifyVmInterfaceDhcp() {
        //Validate ksync
        InterfaceKSyncObject *obj = agent_->ksync()->interface_ksync_obj();;
        VmInterface *vm_intf = static_cast<VmInterface *>(VmPortGet(1));
        std::auto_ptr<InterfaceKSyncEntry> ksync(new InterfaceKSyncEntry(obj,
                                                                         vm_intf));
        ksync->Sync(vm_intf);
        if (dhcp_external) {
            EXPECT_TRUE(ksync->dhcp_enable() == false);
        } else {
            EXPECT_TRUE(ksync->dhcp_enable() == true);
        }
    }

    void VerifyL2L3Mode() {
        //First verify multicast
        MulticastGroupObject *obj =
            MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
        EXPECT_TRUE(obj != NULL);
        BridgeRouteEntry *mc_route =
            L2RouteGet(vrf_name_, MacAddress::FromString("ff:ff:ff:ff:ff:ff"));
        EXPECT_TRUE(mc_route != NULL);
        RouteExport::State *route_state = static_cast<RouteExport::State *>
            (bgp_peer_->GetRouteExportState(mc_route->get_table_partition(),
                                            mc_route));
        EXPECT_TRUE(route_state->fabric_multicast_exported_ == true);
        EXPECT_TRUE(route_state->ingress_replication_exported_ == true);

        //L2 route present
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == true));
        //L3 route present
        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, local_vm_ip4_, 32) == true));
        //Remote route present
        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, remote_vm_ip4_, 32) == true));
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, remote_vm_mac_, remote_vm_ip4_) == true));

        //Ksync validation for local route
        VrfKSyncObject *vrf1_obj = agent_->ksync()->vrf_ksync_obj();;
        DBTableBase::ListenerId vrf_listener_id = vrf1_obj->vrf_listener_id();
        InetUnicastRouteEntry* local_vm_rt = RouteGet("vrf1", local_vm_ip4_,
                                                       32);
        BridgeRouteEntry* local_vm_l2_rt = L2RouteGet("vrf1", local_vm_mac_);
        if (dhcp_external) {
            WAIT_FOR(1000, 1000,(
                     (local_vm_l2_rt->FindMacVmBindingPath()->flood_dhcp()
                      == true)));
        } else {
            WAIT_FOR(1000, 1000,
                     (local_vm_l2_rt->FindMacVmBindingPath()->flood_dhcp()
                      == false));
        }
        VrfEntry *vrf = local_vm_rt->vrf();
        AgentRouteTable *vrf_l2_table =
            static_cast<AgentRouteTable *>(vrf->GetBridgeRouteTable());
        InetUnicastAgentRouteTable *vrf_uc_table =
            static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet4UnicastRouteTable());
        VrfKSyncObject::VrfState *l3_state =
            static_cast<VrfKSyncObject::VrfState *>
            (vrf->GetState(vrf_uc_table, vrf_listener_id));
        VrfKSyncObject::VrfState *l2_state =
            static_cast<VrfKSyncObject::VrfState *>
            (vrf->GetState(vrf_l2_table, vrf_listener_id));
        RouteKSyncObject *vrf_rt_obj = l3_state->inet4_uc_route_table_;
        RouteKSyncObject *vrf_l2_rt_obj = l2_state->bridge_route_table_;
        std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf_rt_obj,
                                                                 local_vm_rt));
        std::auto_ptr<RouteKSyncEntry> l2_ksync(new RouteKSyncEntry(vrf_l2_rt_obj,
                                                                    local_vm_l2_rt));
        ksync->BuildArpFlags(local_vm_rt, local_vm_rt->GetActivePath(),
                             local_vm_mac_);
        l2_ksync->BuildArpFlags(local_vm_l2_rt, local_vm_l2_rt->GetActivePath(),
                             MacAddress());
        EXPECT_TRUE(ksync->proxy_arp());
        EXPECT_FALSE(ksync->flood());
        EXPECT_TRUE(vrf1_obj->RouteNeedsMacBinding(local_vm_rt));
        EXPECT_TRUE(vrf1_obj->GetIpMacBinding(local_vm_rt->vrf(), local_vm_ip4_)
                    != MacAddress());
        EXPECT_FALSE(l2_ksync->proxy_arp());
        EXPECT_FALSE(l2_ksync->flood());

        //Ksync validation for remote route
        InetUnicastRouteEntry* remote_vm_rt = RouteGet("vrf1", remote_vm_ip4_,
                                                       32);
        BridgeRouteEntry* remote_vm_l2_rt = L2RouteGet("vrf1", remote_vm_mac_);
        //TODO mac vm binding path should get installed by evpn
        //if (dhcp_external) {
        //    WAIT_FOR(1000, 1000,(
        //             (remote_vm_l2_rt->FindMacVmBindingPath()->flood_dhcp()
        //              == true)));
        //} else {
        //    WAIT_FOR(1000, 1000,
        //             (remote_vm_l2_rt->FindMacVmBindingPath()->flood_dhcp()
        //              == false));
        //}
        std::auto_ptr<RouteKSyncEntry> remote_ksync(new RouteKSyncEntry(vrf_rt_obj,
                                                                        remote_vm_rt));
        std::auto_ptr<RouteKSyncEntry> remote_l2_ksync(new RouteKSyncEntry(vrf_l2_rt_obj,
                                                                           remote_vm_l2_rt));
        remote_ksync->BuildArpFlags(remote_vm_rt, remote_vm_rt->GetActivePath(),
                             remote_vm_mac_);
        l2_ksync->BuildArpFlags(remote_vm_l2_rt, remote_vm_l2_rt->GetActivePath(),
                             MacAddress());
        EXPECT_TRUE(remote_ksync->proxy_arp());
        EXPECT_FALSE(remote_ksync->flood());
        EXPECT_TRUE(vrf1_obj->RouteNeedsMacBinding(remote_vm_rt));
        EXPECT_TRUE(vrf1_obj->GetIpMacBinding(remote_vm_rt->vrf(), remote_vm_ip4_)
                    != MacAddress());
        EXPECT_FALSE(remote_l2_ksync->proxy_arp());
        EXPECT_FALSE(remote_l2_ksync->flood());
        //repeat same for subnet.
        InetUnicastRouteEntry* subnet_rt = RouteGet("vrf1",
                                                    Ip4Address::from_string("1.1.1.0"),
                                                    24);
        std::auto_ptr<RouteKSyncEntry> ksync1(new RouteKSyncEntry(vrf_rt_obj,
                                                                  subnet_rt));
        EXPECT_FALSE(vrf1_obj->RouteNeedsMacBinding(subnet_rt));
        ksync1->BuildArpFlags(subnet_rt, subnet_rt->GetActivePath(),
                              MacAddress());
        EXPECT_FALSE(ksync1->proxy_arp());
        EXPECT_TRUE(ksync1->flood());
        EXPECT_TRUE(ksync1->mac() == MacAddress::FromString("00:00:00:00:00:00"));

        VerifyVmInterfaceDhcp();
    }

    void VerifyL2OnlyMode() {
        //First verify multicast
        MulticastGroupObject *obj =
            MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
        EXPECT_TRUE(obj != NULL);
        BridgeRouteEntry *mc_route =
            L2RouteGet(vrf_name_, MacAddress::FromString("ff:ff:ff:ff:ff:ff"));
        EXPECT_TRUE(mc_route != NULL);
        RouteExport::State *route_state = static_cast<RouteExport::State *>
            (bgp_peer_->GetRouteExportState(mc_route->get_table_partition(),
                                            mc_route));
        EXPECT_TRUE(route_state->fabric_multicast_exported_ == true);
        EXPECT_TRUE(route_state->ingress_replication_exported_ == true);

        //L2 route present
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == true));
        //L3 route present
        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, local_vm_ip4_, 32) == false));
        //Remote route present
        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, remote_vm_ip4_, 32) == true));
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, remote_vm_mac_, remote_vm_ip4_) == true));

        //Ksync validation
        VrfKSyncObject *vrf1_obj = agent_->ksync()->vrf_ksync_obj();;
        DBTableBase::ListenerId vrf_listener_id = vrf1_obj->vrf_listener_id();
        BridgeRouteEntry* local_vm_rt = L2RouteGet("vrf1", local_vm_mac_);
        VrfEntry *vrf = local_vm_rt->vrf();
        AgentRouteTable *vrf_l2_table =
            static_cast<AgentRouteTable *>(vrf->GetBridgeRouteTable());
        VrfKSyncObject::VrfState *state =
            static_cast<VrfKSyncObject::VrfState *>
            (vrf->GetState(vrf_l2_table, vrf_listener_id));
        RouteKSyncObject *vrf_rt_obj = state->bridge_route_table_;
        std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf_rt_obj,
                                                                 local_vm_rt));
        ksync->BuildArpFlags(local_vm_rt, local_vm_rt->GetActivePath(),
                             MacAddress());
        EXPECT_FALSE(ksync->proxy_arp());
        EXPECT_FALSE(ksync->flood());

        //Ksync validation for remote route
        VrfKSyncObject::VrfState *l2_state =
            static_cast<VrfKSyncObject::VrfState *>
            (vrf->GetState(vrf_l2_table, vrf_listener_id));
        RouteKSyncObject *vrf_l2_rt_obj = l2_state->bridge_route_table_;
        InetUnicastRouteEntry* remote_vm_rt = RouteGet("vrf1", remote_vm_ip4_,
                                                       32);
        BridgeRouteEntry* remote_vm_l2_rt = L2RouteGet("vrf1", remote_vm_mac_);
        std::auto_ptr<RouteKSyncEntry> remote_ksync(new RouteKSyncEntry(vrf_rt_obj,
                                                                        remote_vm_rt));
        std::auto_ptr<RouteKSyncEntry> remote_l2_ksync(new RouteKSyncEntry(vrf_l2_rt_obj,
                                                                           remote_vm_l2_rt));
        remote_ksync->BuildArpFlags(remote_vm_rt, remote_vm_rt->GetActivePath(),
                             remote_vm_mac_);
        remote_l2_ksync->BuildArpFlags(remote_vm_l2_rt, remote_vm_l2_rt->GetActivePath(),
                             MacAddress());
        EXPECT_FALSE(remote_ksync->proxy_arp());
        EXPECT_TRUE(remote_ksync->flood());
        //Since proxy is false, does not matter if mac binding is needed.
        EXPECT_TRUE(vrf1_obj->RouteNeedsMacBinding(remote_vm_rt));
        EXPECT_FALSE(remote_l2_ksync->proxy_arp());
        EXPECT_FALSE(remote_l2_ksync->flood());
        //repeat same for subnet.
        InetUnicastRouteEntry* subnet_rt = RouteGet("vrf1",
                                                    Ip4Address::from_string("1.1.1.0"),
                                                    24);
        EXPECT_TRUE(subnet_rt == NULL);
        //interface validation
        VerifyVmInterfaceDhcp();
    }

    void VerifyL3OnlyMode() {
        //First verify multicast
        MulticastGroupObject *obj =
            MulticastHandler::GetInstance()->FindFloodGroupObject("vrf1");
        EXPECT_TRUE(obj != NULL);
        BridgeRouteEntry *mc_route =
            L2RouteGet(vrf_name_, MacAddress::FromString("ff:ff:ff:ff:ff:ff"));
        EXPECT_TRUE(mc_route != NULL);
        RouteExport::State *route_state = static_cast<RouteExport::State *>
            (bgp_peer_->GetRouteExportState(mc_route->get_table_partition(),
                                            mc_route));
        WAIT_FOR(1000, 1000, (route_state->fabric_multicast_exported_ == true));
        WAIT_FOR(1000, 1000, (route_state->ingress_replication_exported_ == false));

        //L2 route present
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == true));
        //L3 route present
        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, local_vm_ip4_, 32) == true));
        //Remote route present
        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, remote_vm_ip4_, 32) == true));
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, remote_vm_mac_, remote_vm_ip4_) == true));

        //Ksync validation
        VrfKSyncObject *vrf1_obj = agent_->ksync()->vrf_ksync_obj();;
        DBTableBase::ListenerId vrf_listener_id = vrf1_obj->vrf_listener_id();
        InetUnicastRouteEntry* local_vm_rt = RouteGet("vrf1", local_vm_ip4_,
                                                       32);
        VrfEntry *vrf = local_vm_rt->vrf();
        InetUnicastAgentRouteTable *vrf_uc_table =
            static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet4UnicastRouteTable());
        VrfKSyncObject::VrfState *state =
            static_cast<VrfKSyncObject::VrfState *>
            (vrf->GetState(vrf_uc_table, vrf_listener_id));
        RouteKSyncObject *vrf_rt_obj = state->inet4_uc_route_table_;
        std::auto_ptr<RouteKSyncEntry> ksync(new RouteKSyncEntry(vrf_rt_obj,
                                                                 local_vm_rt));
        EXPECT_FALSE(vrf1_obj->RouteNeedsMacBinding(local_vm_rt));
        ksync->BuildArpFlags(local_vm_rt, local_vm_rt->GetActivePath(),
                             MacAddress());
        EXPECT_TRUE(ksync->proxy_arp());
        EXPECT_FALSE(ksync->flood());
        EXPECT_TRUE(ksync->mac() == MacAddress::FromString("00:00:00:00:00:00"));
        //repeat same for subnet.
        InetUnicastRouteEntry* subnet_rt = RouteGet("vrf1",
                                                    Ip4Address::from_string("1.1.1.0"),
                                                    24);
        std::auto_ptr<RouteKSyncEntry> ksync1(new RouteKSyncEntry(vrf_rt_obj,
                                                                  subnet_rt));
        EXPECT_FALSE(vrf1_obj->RouteNeedsMacBinding(subnet_rt));
        ksync1->BuildArpFlags(subnet_rt, subnet_rt->GetActivePath(),
                              MacAddress());
        EXPECT_TRUE(ksync1->proxy_arp());
        EXPECT_FALSE(ksync1->flood());
        EXPECT_TRUE(ksync1->mac() == MacAddress::FromString("00:00:00:00:00:00"));

        VerifyVmInterfaceDhcp();
    }

    void WaitForSetup(std::string type) {
        if (type == "l2_l3") {
            WAIT_FOR(1000, 100, (VmPortL2Active(input, 0) == true)); //l2 active
            WAIT_FOR(1000, 100, (VmPortActive(input, 0) == true)); //v4 active
        } else if (type == "l2") {
            WAIT_FOR(1000, 100, (VmPortL2Active(input, 0) == true)); //l2 active
            EXPECT_TRUE(VmPortActive(input, 0) == false);
        } else if (type == "l3") {
            WAIT_FOR(1000, 100, (VmPortActive(input, 0) == true)); //v4 active
            EXPECT_TRUE(VmPortL2Active(input, 0) == false);
        }
    }

    void SetupSingleVmEnvironment(std::string setup_type,
                                  bool global_config = false,
                                  std::string global_type = "l2_l3") {
        client->Reset();
        //Add Ipam
        if (dhcp_external)
            AddIPAM("vn1", ipam_info_dhcp_disable, 1);
        else
            AddIPAM("vn1", ipam_info_dhcp_enable, 1);
        client->WaitForIdle();

        if (setup_type == "l2_l3") {
            CreateVmportEnv(input, 1);
        } else if (setup_type == "l2") {
            CreateL2VmportEnv(input, 1);
        } else if (setup_type == "l3") {
            CreateL3VmportEnv(input, 1);
        }

        //Expect l3vpn or evpn route irrespective of forwarding mode.
        AddRemoteVmRoute(remote_vm_mac_, remote_vm_ip4_, server1_ip_,
                         200, (TunnelType::AllType()));
        client->WaitForIdle();

        if (global_config) {
            WaitForSetup(global_type);
        } else {
            WaitForSetup(setup_type);
        }
        client->WaitForIdle();
    }

    void DeleteSingleVmEnvironment() {
        //Time for deletion
        DelIPAM("vn1");
        client->WaitForIdle();
        DeleteVmportEnv(input, 1, true);
        client->WaitForIdle();
        DeleteRoute(bgp_peer_, vrf_name_, remote_vm_mac_, remote_vm_ip4_);
        client->WaitForIdle();

        WAIT_FOR(1000, 100,
                 (RouteFind(vrf_name_, local_vm_ip4_, 32) == false));
        WAIT_FOR(1000, 100,
                 (L2RouteFind(vrf_name_, local_vm_mac_, local_vm_ip4_) == false));
        EXPECT_FALSE(VmPortFind(input, 0));
        client->WaitForIdle();
    }

    std::string vrf_name_;
    std::string eth_name_;
    Ip4Address  default_dest_ip_;

    char local_vm_mac_str_[100];
    MacAddress  local_vm_mac_;
    char local_vm_ip4_str_[100];
    Ip4Address  local_vm_ip4_;
    char local_vm_ip6_str_[100];
    IpAddress  local_vm_ip6_;

    char remote_vm_mac_str_[100];
    MacAddress  remote_vm_mac_;
    char remote_vm_ip4_str_[100];
    Ip4Address  remote_vm_ip4_;
    char remote_vm_ip6_str_[100];
    IpAddress  remote_vm_ip6_;

    Ip4Address  vhost_ip_;
    Ip4Address  server1_ip_;
    Ip4Address  server2_ip_;

    BgpPeer *bgp_peer_;
    Agent *agent_;
};

TEST_F(ForwardingModeTest, dummy) {
}

//VN forwarding mode tests
TEST_F(ForwardingModeTest, default_forwarding_mode_l2_l3_with_none_configure) {
    GlobalForwardingMode("l2_l3");
    SetupSingleVmEnvironment("l2_l3");
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

//With l2_l3 in environment VN gets set with blank forwarding mode.
//This in turn defaults to l2_l3.
//This test case explicitly modifies blank forwarding mode
//to l2_l3 and verify.
TEST_F(ForwardingModeTest, default_forwarding_mode_l2_l3_configured) {
    SetupSingleVmEnvironment("l2_l3");
    ModifyForwardingModeVn("vn1", 1, "l2_l3");
    client->WaitForIdle();
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, default_forwarding_mode_l2) {
    SetupSingleVmEnvironment("l2");
    VerifyL2OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, default_forwarding_mode_l3) {
    SetupSingleVmEnvironment("l3");
    VerifyL3OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, change_forwarding_mode_l2_l3_to_l2) {
    SetupSingleVmEnvironment("l2_l3");
    VerifyL2L3Mode();
    SetupSingleVmEnvironment("l2");
    VerifyL2OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, change_forwarding_mode_l2_l3_to_l3) {
    SetupSingleVmEnvironment("l2_l3");
    VerifyL2L3Mode();
    SetupSingleVmEnvironment("l3");
    VerifyL3OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, change_forwarding_mode_l2_to_l2_l3) {
    SetupSingleVmEnvironment("l2");
    VerifyL2OnlyMode();
    SetupSingleVmEnvironment("l2_l3");
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, change_forwarding_mode_l2_to_l3) {
    SetupSingleVmEnvironment("l2");
    VerifyL2OnlyMode();
    SetupSingleVmEnvironment("l3");
    VerifyL3OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, change_forwarding_mode_l3_to_l2_l3) {
    SetupSingleVmEnvironment("l3");
    VerifyL3OnlyMode();
    SetupSingleVmEnvironment("l2_l3");
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, change_forwarding_mode_l3_to_l2) {
    SetupSingleVmEnvironment("l3");
    VerifyL3OnlyMode();
    SetupSingleVmEnvironment("l2");
    VerifyL2OnlyMode();
    DeleteSingleVmEnvironment();
}

//Global vrouter forwarding mode config tests
TEST_F(ForwardingModeTest, global_default_to_empty) {
    GlobalForwardingMode("");
    SetupSingleVmEnvironment("l2_l3");
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_default_to_l2_l3) {
    GlobalForwardingMode("l2_l3");
    SetupSingleVmEnvironment("l2_l3");
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_default_to_l2) {
    GlobalForwardingMode("l2");
    SetupSingleVmEnvironment("l2_l3", true, "l2");
    VerifyL2OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_default_to_l3) {
    GlobalForwardingMode("l3");
    SetupSingleVmEnvironment("l2_l3", true, "l3");
    VerifyL3OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_change_l2_l3_to_l2) {
    GlobalForwardingMode("l2_l3");
    SetupSingleVmEnvironment("l2_l3", true, "l2_l3");
    //Toggle
    GlobalForwardingMode("l2");
    client->WaitForIdle();
    VerifyL2OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_change_l2_l3_to_l3) {
    GlobalForwardingMode("l2_l3");
    SetupSingleVmEnvironment("l2_l3", true, "l2_l3");
    //Toggle
    GlobalForwardingMode("l3");
    client->WaitForIdle();
    VerifyL3OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_change_l2_to_l2_l3) {
    GlobalForwardingMode("l2");
    SetupSingleVmEnvironment("l2_l3", true, "l2");
    //Toggle
    GlobalForwardingMode("l2_l3");
    client->WaitForIdle();
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_change_l3_to_l2_l3) {
    GlobalForwardingMode("l3");
    SetupSingleVmEnvironment("l2_l3", true, "l3");
    //Toggle
    GlobalForwardingMode("l2_l3");
    client->WaitForIdle();
    VerifyL2L3Mode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_change_l3_to_l2) {
    GlobalForwardingMode("l3");
    SetupSingleVmEnvironment("l2_l3", true, "l3");
    //Toggle
    GlobalForwardingMode("l2");
    client->WaitForIdle();
    VerifyL2OnlyMode();
    DeleteSingleVmEnvironment();
}

TEST_F(ForwardingModeTest, global_change_l2_to_l3) {
    GlobalForwardingMode("l2");
    SetupSingleVmEnvironment("l2_l3", true, "l2");
    //Toggle
    GlobalForwardingMode("l3");
    client->WaitForIdle();
    VerifyL3OnlyMode();
    DeleteSingleVmEnvironment();
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    eth_itf = Agent::GetInstance()->fabric_interface_name();

    int ret = 0;
    //Run tests with dhcp server as external
    dhcp_external = true;
    ret += RUN_ALL_TESTS();
    client->WaitForIdle();
    //Run tests with agent as dhcp server
    dhcp_external = false;
    ret += RUN_ALL_TESTS();

    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}

