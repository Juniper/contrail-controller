//
//  test_evpn_route.cc
//  vnsw/agent/test
//
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
#include "controller/controller_route_walker.h"
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

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class RouteTest : public ::testing::Test {
public:
    static void SetTunnelType(TunnelType::Type type) {
        TunnelType::SetDefaultType(type);
        type_ = type;
    }
    static TunnelType::Type GetTunnelType() {
        return type_;
    }

protected:
    RouteTest() : vrf_name_("vrf1"), eth_name_(eth_itf) {
        default_dest_ip_ = Ip4Address::from_string("0.0.0.0");
        agent_ =Agent::GetInstance();

        if (agent_->router_id_configured()) {
            vhost_ip_ = agent_->router_id();
        } else {
            vhost_ip_ = Ip4Address::from_string("10.1.1.10");
        }
        server1_ip_ = Ip4Address::from_string("10.1.1.11");

        strcpy(ipam_subnet_str_, "1.1.1.0");
        ipam_subnet_ip4_ = Ip4Address::from_string(ipam_subnet_str_);

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

        strcpy(local_vm_mac_2_str_, "00:00:02:02:02:10");
        local_vm_mac_2_ = MacAddress::FromString(local_vm_mac_2_str_);
        strcpy(local_vm_ip4_2_str_, "2.2.2.10");
        local_vm_ip4_2_ = Ip4Address::from_string(local_vm_ip4_2_str_);
        strcpy(local_vm_ip6_2_str_, "fdff::20");
        local_vm_ip6_2_ = Ip6Address::from_string(local_vm_ip6_2_str_);

        strcpy(remote_vm_mac_2_str_, "00:00:02:02:02:11");
        remote_vm_mac_2_ = MacAddress::FromString(remote_vm_mac_2_str_);
        strcpy(remote_vm_ip4_2_str_, "2.2.2.11");
        remote_vm_ip4_2_ = Ip4Address::from_string(remote_vm_ip4_2_str_);
        strcpy(remote_vm_ip6_2_str_, "fdff::21");
        remote_vm_ip6_2_ = Ip6Address::from_string(remote_vm_ip6_2_str_);
    }

    ~RouteTest() {
    }

    virtual void SetUp() {
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
    }

    virtual void TearDown() {
        VrfDelReq(vrf_name_.c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VrfFind(vrf_name_.c_str()) != true));
    }

    void AddRemoteVmRoute(MacAddress &remote_vm_mac, const IpAddress &ip_addr,
                          const Ip4Address &server_ip,
                          uint32_t label, TunnelType::TypeBmap bmap) {
        //Use any other peer than localvmpeer
        BridgeTunnelRouteAdd(agent_->local_peer(), vrf_name_, bmap, server_ip,
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

    void DeleteBridgeRoute(const Peer *peer, const std::string &vrf_name,
                     MacAddress &remote_vm_mac, const IpAddress &ip_addr) {
        const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
        EvpnAgentRouteTable::DeleteReq(peer, vrf_name_, remote_vm_mac,
                                        ip_addr, 0,
                                        ((bgp_peer == NULL) ? NULL :
                                         (new ControllerVmRoute(bgp_peer))));
        client->WaitForIdle();
        //while (evpnFind(vrf_name, remote_vm_mac, ip_addr) == true) {
        //    client->WaitForIdle();
        //}
    }

    std::string vrf_name_;
    std::string eth_name_;
    Ip4Address  default_dest_ip_;

    char ipam_subnet_str_[100];
    Ip4Address  ipam_subnet_ip4_;
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

    char local_vm_mac_2_str_[100];
    MacAddress  local_vm_mac_2_;
    char local_vm_ip4_2_str_[100];
    Ip4Address  local_vm_ip4_2_;
    char local_vm_ip6_2_str_[100];
    IpAddress  local_vm_ip6_2_;

    char remote_vm_mac_2_str_[100];
    MacAddress  remote_vm_mac_2_;
    char remote_vm_ip4_2_str_[100];
    Ip4Address  remote_vm_ip4_2_;
    char remote_vm_ip6_2_str_[100];
    IpAddress  remote_vm_ip6_2_;

    Ip4Address  vhost_ip_;
    Ip4Address  server1_ip_;
    Ip4Address  server2_ip_;

    static TunnelType::Type type_;
    Agent *agent_;
};
TunnelType::Type RouteTest::type_;

TEST_F(RouteTest, LocalVmRoute_without_ipam) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", local_vm_mac_,
                                           local_vm_ip4_, 0);
    EXPECT_TRUE(evpn_rt == NULL);
    InetUnicastRouteEntry *inet_rt = RouteGet("vrf1", local_vm_ip4_, 32);
    EXPECT_TRUE(inet_rt == NULL);
    InetUnicastRouteEntry *zero_ip_rt = RouteGet("vrf1",
                                                 Ip4Address::from_string("0.0.0.0"),
                                                 32);
    EXPECT_TRUE(zero_ip_rt == NULL);
    EvpnRouteEntry *zero_ip_evpn_rt = EvpnRouteGet("vrf1", local_vm_mac_,
                                                   Ip4Address::from_string("0.0.0.0"),
                                                   0);
    EXPECT_TRUE(zero_ip_evpn_rt != NULL);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

TEST_F(RouteTest, LocalVmRoute_with_ipam) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", local_vm_mac_,
                                           local_vm_ip4_, 0);
    EXPECT_TRUE(evpn_rt != NULL);
    InetUnicastRouteEntry *inet_rt = RouteGet("vrf1", local_vm_ip4_, 32);
    EXPECT_TRUE(inet_rt != NULL);
    EXPECT_TRUE(inet_rt->GetPathList().size() == 2);
    AgentPath *evpn_inet_path = inet_rt->FindPath(agent_->inet_evpn_peer());
    EXPECT_TRUE(evpn_inet_path != NULL);
    EXPECT_TRUE(evpn_inet_path->nexthop() == NULL);
    EXPECT_TRUE(inet_rt->GetActivePath() != evpn_inet_path);

    //Take out subnet route
    InetUnicastRouteEntry *subnet_rt = RouteGet("vrf1", ipam_subnet_ip4_, 24);
    EXPECT_TRUE(subnet_rt->GetActiveNextHop() ==
                evpn_inet_path->ComputeNextHop(agent_));
    EXPECT_TRUE(subnet_rt->GetActiveLabel() ==
                evpn_inet_path->label());

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_with_zero_ip) {
    BgpPeer *bgp_peer = CreateBgpPeer("127.0.0.1", "remote");
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    InetUnicastRouteEntry *zero_ip_rt = RouteGet("vrf1",
                                                 Ip4Address::from_string("0.0.0.0"),
                                                 32);
    EXPECT_TRUE(zero_ip_rt == NULL);
    BridgeTunnelRouteAdd(bgp_peer, "vrf1", TunnelType::AllType(), server1_ip_,
                         (MplsTable::kStartLabel + 60), remote_vm_mac_,
                         Ip4Address::from_string("0.0.0.0"), 32);
    client->WaitForIdle();
    zero_ip_rt = RouteGet("vrf1",
                          Ip4Address::from_string("0.0.0.0"),
                          32);
    EXPECT_TRUE(zero_ip_rt == NULL);

    EvpnAgentRouteTable::DeleteReq(bgp_peer, vrf_name_,
                                   remote_vm_mac_,
                                   Ip4Address::from_string("0.0.0.0"),
                                   0, new ControllerVmRoute(bgp_peer));
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer);
    client->WaitForIdle();
}

//Add two subnets with different prefix, add evpn route, verify path
TEST_F(RouteTest, RemoteVmRoute_with_ipam) {
    BgpPeer *bgp_peer = CreateBgpPeer("127.0.0.1", "remote");
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    Inet4TunnelRouteAdd(bgp_peer, vrf_name_,
                        Ip4Address::from_string("1.1.1.100"),
                        26, server1_ip_,
                        TunnelType::MplsType(),
                        (MplsTable::kStartLabel + 50),
                        vrf_name_, SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    BridgeTunnelRouteAdd(bgp_peer, "vrf1", TunnelType::AllType(), server1_ip_,
                         (MplsTable::kStartLabel + 60), remote_vm_mac_,
                         Ip4Address::from_string("1.1.1.99"), 32);
    client->WaitForIdle();

    InetUnicastRouteEntry *ipam_subnet_rt = RouteGet("vrf1",
                                            Ip4Address::from_string("1.1.1.0"),
                                            24);
    InetUnicastRouteEntry *remote_subnet_rt = RouteGet("vrf1",
                                              Ip4Address::from_string("1.1.1.100"),
                                              26);
    InetUnicastRouteEntry *remote_host_rt = RouteGet("vrf1",
                                                   Ip4Address::from_string("1.1.1.99"),
                                                   32);
    EXPECT_TRUE(remote_subnet_rt != NULL);
    EXPECT_TRUE(remote_host_rt != NULL);
    EXPECT_TRUE(remote_host_rt->GetActivePath() ==
                remote_subnet_rt->GetActivePath());

    DeleteRoute(vrf_name_.c_str(), "1.1.1.100", 26,
                bgp_peer);
    client->WaitForIdle();

    ipam_subnet_rt = RouteGet("vrf1",
                              Ip4Address::from_string("1.1.1.0"),
                              24);
    remote_subnet_rt = RouteGet("vrf1",
                                Ip4Address::from_string("1.1.1.100"),
                                26);
    remote_host_rt = RouteGet("vrf1",
                              Ip4Address::from_string("1.1.1.99"),
                              32);
    EXPECT_TRUE(remote_subnet_rt == NULL);
    EXPECT_TRUE(remote_host_rt != NULL);
    EXPECT_TRUE(remote_host_rt->GetActivePath() ==
                ipam_subnet_rt->GetActivePath());

    EvpnAgentRouteTable::DeleteReq(bgp_peer, vrf_name_,
                                   remote_vm_mac_,
                                   Ip4Address::from_string("1.1.1.99"),
                                   0, new ControllerVmRoute(bgp_peer));
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer);
    client->WaitForIdle();
}

//Add two subnets with different prefix, and add evpn route before addition of
//second subnet.
TEST_F(RouteTest, RemoteVmRoute_with_ipam_2) {
    BgpPeer *bgp_peer = CreateBgpPeer("127.0.0.1", "remote");
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    BridgeTunnelRouteAdd(bgp_peer, "vrf1", TunnelType::AllType(), server1_ip_,
                         (MplsTable::kStartLabel + 60), remote_vm_mac_,
                         Ip4Address::from_string("1.1.1.99"), 32);
    client->WaitForIdle();

    InetUnicastRouteEntry *ipam_subnet_rt = RouteGet("vrf1",
                                            Ip4Address::from_string("1.1.1.0"),
                                            24);
    InetUnicastRouteEntry *remote_subnet_rt = RouteGet("vrf1",
                                              Ip4Address::from_string("1.1.1.100"),
                                              26);
    InetUnicastRouteEntry *remote_host_rt = RouteGet("vrf1",
                                                   Ip4Address::from_string("1.1.1.99"),
                                                   32);
    EXPECT_TRUE(remote_subnet_rt == NULL);
    EXPECT_TRUE(remote_host_rt != NULL);
    EXPECT_TRUE(remote_host_rt->GetActivePath() ==
                ipam_subnet_rt->GetActivePath());

    Inet4TunnelRouteAdd(bgp_peer, vrf_name_,
                        Ip4Address::from_string("1.1.1.100"),
                        26, server1_ip_,
                        TunnelType::MplsType(),
                        (MplsTable::kStartLabel + 50),
                        vrf_name_, SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    ipam_subnet_rt = RouteGet("vrf1",
                              Ip4Address::from_string("1.1.1.0"),
                              24);
    remote_subnet_rt = RouteGet("vrf1",
                                Ip4Address::from_string("1.1.1.100"),
                                26);
    remote_host_rt = RouteGet("vrf1",
                              Ip4Address::from_string("1.1.1.99"),
                              32);
    EXPECT_TRUE(remote_subnet_rt != NULL);
    EXPECT_TRUE(remote_host_rt != NULL);
    EXPECT_TRUE(remote_host_rt->GetActivePath() ==
                remote_subnet_rt->GetActivePath());


    EvpnAgentRouteTable::DeleteReq(bgp_peer, vrf_name_,
                                   remote_vm_mac_,
                                   Ip4Address::from_string("1.1.1.99"),
                                     0, new ControllerVmRoute(bgp_peer));
    client->WaitForIdle();
    DeleteRoute(vrf_name_.c_str(), "1.1.1.100", 26,
                bgp_peer);
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer);
    client->WaitForIdle();
}

//Add IPAM and then delete and then re-add same IPAM.
TEST_F(RouteTest, LocalVmRoute_with_ipam_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", local_vm_mac_,
                                           local_vm_ip4_, 0);
    EXPECT_TRUE(evpn_rt != NULL);
    InetUnicastRouteEntry *inet_rt = RouteGet("vrf1", local_vm_ip4_, 32);
    EXPECT_TRUE(inet_rt != NULL);
    EXPECT_TRUE(inet_rt->GetPathList().size() == 2);
    AgentPath *evpn_inet_path = inet_rt->FindPath(agent_->inet_evpn_peer());
    EXPECT_TRUE(evpn_inet_path != NULL);
    EXPECT_TRUE(evpn_inet_path->nexthop() == NULL);
    EXPECT_TRUE(inet_rt->GetActivePath() != evpn_inet_path);

    //Take out subnet route
    InetUnicastRouteEntry *subnet_rt = RouteGet("vrf1", ipam_subnet_ip4_, 24);
    EXPECT_TRUE(subnet_rt->GetActiveNextHop() ==
                evpn_inet_path->ComputeNextHop(agent_));
    EXPECT_TRUE(subnet_rt->GetActiveLabel() ==
                evpn_inet_path->label());

    //Delete IPAM and verify
    DelIPAM("vn1");
    client->WaitForIdle();
    subnet_rt = RouteGet("vrf1", ipam_subnet_ip4_, 24);
    EXPECT_TRUE(subnet_rt == NULL);

    evpn_inet_path = inet_rt->FindPath(agent_->inet_evpn_peer());
    EXPECT_TRUE(evpn_inet_path != NULL);
    EXPECT_TRUE(evpn_inet_path->nexthop() == NULL);
    EXPECT_TRUE(inet_rt->GetActivePath() != evpn_inet_path);
    //EXPECT_TRUE(evpn_inet_path->ComputeNextHop(agent_) == NULL);
    EXPECT_TRUE(evpn_inet_path->label() == MplsTable::kInvalidLabel);
    EXPECT_TRUE(evpn_inet_path->unresolved() == true);

    //Re-add same IPAM
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    subnet_rt = RouteGet("vrf1", ipam_subnet_ip4_, 24);
    EXPECT_TRUE(subnet_rt != NULL);
    evpn_inet_path = inet_rt->FindPath(agent_->inet_evpn_peer());
    EXPECT_TRUE(evpn_inet_path != NULL);
    EXPECT_TRUE(evpn_inet_path->nexthop() == NULL);
    EXPECT_TRUE(inet_rt->GetActivePath() != evpn_inet_path);
    EXPECT_TRUE(evpn_inet_path->unresolved() == false);
    EXPECT_TRUE(subnet_rt->GetActiveNextHop() ==
                evpn_inet_path->ComputeNextHop(agent_));
    EXPECT_TRUE(subnet_rt->GetActiveLabel() ==
                evpn_inet_path->label());

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(RouteTest, LocalVmRoute_with_ipam_and_external_subnet_route) {
    BgpPeer *bgp_peer = CreateBgpPeer("127.0.0.1", "remote");
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EvpnRouteEntry *evpn_rt = EvpnRouteGet("vrf1", local_vm_mac_,
                                           local_vm_ip4_, 0);
    EXPECT_TRUE(evpn_rt != NULL);
    InetUnicastRouteEntry *inet_rt = RouteGet("vrf1", local_vm_ip4_, 32);
    EXPECT_TRUE(inet_rt != NULL);
    EXPECT_TRUE(inet_rt->GetPathList().size() == 2);
    AgentPath *evpn_inet_path = inet_rt->FindPath(agent_->inet_evpn_peer());
    EXPECT_TRUE(evpn_inet_path != NULL);
    EXPECT_TRUE(evpn_inet_path->nexthop() == NULL);
    EXPECT_TRUE(inet_rt->GetActivePath() != evpn_inet_path);

    //Add non-ipam subnet route
    Inet4TunnelRouteAdd(bgp_peer, vrf_name_,
                        ipam_subnet_ip4_, 24, server1_ip_,
                        TunnelType::MplsType(), MplsTable::kStartLabel,
                        vrf_name_, SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    //Take out subnet route
    InetUnicastRouteEntry *subnet_rt = RouteGet("vrf1", ipam_subnet_ip4_, 24);
    EXPECT_TRUE(subnet_rt->GetActiveNextHop() ==
                evpn_inet_path->ComputeNextHop(agent_));

    //Remove local_vm_peer path from inet route 1.1.1.10/32
    InetUnicastAgentRouteTable::DeleteReq(inet_rt->GetActivePath()->peer(),
                                          "vrf1", local_vm_ip4_, 32, NULL);
    client->WaitForIdle();

    //Now evpn_inet_route should be active path.
    EXPECT_TRUE(subnet_rt->GetActiveNextHop() ==
                inet_rt->GetActiveNextHop());
    EXPECT_TRUE(inet_rt->GetActivePath()->label() == MplsTable::kStartLabel);
    EXPECT_TRUE(subnet_rt->GetActiveLabel() ==
                inet_rt->GetActiveLabel());

    //Change the label of subnet_route
    Inet4TunnelRouteAdd(bgp_peer, vrf_name_,
                        ipam_subnet_ip4_, 24, server1_ip_,
                        TunnelType::MplsType(), (MplsTable::kStartLabel + 1),
                        vrf_name_, SecurityGroupList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(subnet_rt->GetActiveNextHop() ==
                evpn_inet_path->ComputeNextHop(agent_));
    EXPECT_TRUE(inet_rt->GetActiveLabel() == (MplsTable::kStartLabel + 1));
    EXPECT_TRUE(subnet_rt->GetActiveLabel() ==
                inet_rt->GetActiveLabel());

    DeleteRoute(vrf_name_.c_str(), ipam_subnet_str_, 24, bgp_peer);
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer);
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_with_non_ipam_subnet) {
    BgpPeer *bgp_peer = CreateBgpPeer("127.0.0.1", "remote");
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    Inet4TunnelRouteAdd(bgp_peer, vrf_name_,
                        Ip4Address::from_string("2.2.2.100"),
                        26, server1_ip_,
                        TunnelType::MplsType(),
                        (MplsTable::kStartLabel + 50),
                        vrf_name_, SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    BridgeTunnelRouteAdd(bgp_peer, "vrf1", TunnelType::AllType(), server1_ip_,
                         (MplsTable::kStartLabel + 60), remote_vm_mac_,
                         Ip4Address::from_string("2.2.2.99"), 32);
    client->WaitForIdle();

    InetUnicastRouteEntry *remote_subnet_rt = RouteGet("vrf1",
                                              Ip4Address::from_string("2.2.2.100"),
                                              26);
    InetUnicastRouteEntry *remote_host_rt = RouteGet("vrf1",
                                                   Ip4Address::from_string("2.2.2.99"),
                                                   32);
    EXPECT_TRUE(remote_subnet_rt != NULL);
    EXPECT_TRUE(remote_host_rt != NULL);
    EXPECT_TRUE(remote_host_rt->GetActivePath() ==
                remote_subnet_rt->GetActivePath());

    DelVrf("vrf1");
    //DeleteRoute(vrf_name_.c_str(), "2.2.2.100", 26,
    //            bgp_peer);
    client->WaitForIdle();

    remote_subnet_rt = RouteGet("vrf1",
                                Ip4Address::from_string("2.2.2.100"),
                                26);
    remote_host_rt = RouteGet("vrf1",
                              Ip4Address::from_string("2.2.2.99"),
                              32);
    EXPECT_TRUE(remote_subnet_rt == NULL);
    EXPECT_TRUE(remote_host_rt == NULL);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    eth_itf = Agent::GetInstance()->fabric_interface_name();

    RouteTest::SetTunnelType(TunnelType::MPLS_GRE);
    int ret = RUN_ALL_TESTS();
    RouteTest::SetTunnelType(TunnelType::MPLS_UDP);
    ret += RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
