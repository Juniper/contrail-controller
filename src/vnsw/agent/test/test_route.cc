/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/init_config.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "openstack/instance_service_server.h"
#include "cfg/interface_cfg.h"
#include "cfg/init_config.h"
#include "test_cmn_util.h"
#include "test_kstate_util.h"
#include "vr_types.h"

#include <controller/controller_export.h> 

std::string eth_itf;

void RouterIdDepInit() {
}

class RouteTest : public ::testing::Test {
public:
    void NhListener(DBTablePartBase *partition, DBEntryBase *dbe) {
        return;
    }

    void RtListener(DBTablePartBase *partition, DBEntryBase *dbe) {
        return;
    }

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

        if (Agent::GetRouterIdConfigured()) {
            vhost_ip_ = Agent::GetRouterId();
        } else {
            vhost_ip_ = Ip4Address::from_string("10.1.1.10");
        }
        if (Agent::GetGatewayId() != default_dest_ip_) {
            is_gateway_configured = true;
            fabric_gw_ip_ = Agent::GetGatewayId();
        } else {
            is_gateway_configured = false;
            fabric_gw_ip_ = Ip4Address::from_string("10.1.1.254");
        }
        foreign_gw_ip_ = Ip4Address::from_string("10.10.10.254");
        server1_ip_ = Ip4Address::from_string("10.1.1.11");
        server2_ip_ = Ip4Address::from_string("10.1.122.11");
        local_vm_ip_ = Ip4Address::from_string("1.1.1.10");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        trap_ip_ = Ip4Address::from_string("1.1.1.100");
        lpm1_ip_ = Ip4Address::from_string("2.0.0.0");
        lpm2_ip_ = Ip4Address::from_string("2.1.0.0");
        lpm3_ip_ = Ip4Address::from_string("2.1.1.0");
        lpm4_ip_ = Ip4Address::from_string("2.1.1.1");
        lpm5_ip_ = Ip4Address::from_string("2.1.1.2");
    }

    virtual void SetUp() {
        client->Reset();
        //Create a VRF
        VrfAddReq(vrf_name_.c_str());
        EthInterface::CreateReq(eth_name_, Agent::GetDefaultVrf());
        AddResolveRoute(server1_ip_, 24);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        TestRouteTable table1(1);
        WAIT_FOR(100, 100, (table1.Size() == 0));
        EXPECT_EQ(table1.Size(), 0);

        TestRouteTable table2(2);
        WAIT_FOR(100, 100, (table2.Size() == 0));
        EXPECT_EQ(table2.Size(), 0);

        TestRouteTable table3(3);
        WAIT_FOR(100, 100, (table3.Size() == 0));
        EXPECT_EQ(table3.Size(), 0);

        VrfDelReq(vrf_name_.c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VrfFind(vrf_name_.c_str()) != true));
    }

    void AddHostRoute(Ip4Address addr) {
        Agent::GetDefaultInet4UcRouteTable()->AddHostRoute(vrf_name_, addr, 32, Agent::GetFabricVnName());
        client->WaitForIdle();
    }

    void AddVhostRoute() {
        Agent::GetDefaultInet4UcRouteTable()->AddVHostRecvRoute(
                                                Agent::GetDefaultVrf(),
                                                "vhost0", vhost_ip_, false);
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(const Ip4Address &remote_vm_ip, 
                          const Ip4Address &server_ip, uint32_t plen, 
                          uint32_t label, TunnelType::TypeBmap bmap) {
        Agent::GetDefaultInet4UcRouteTable()->AddRemoteVmRoute(NULL, vrf_name_,
            remote_vm_ip, plen, server_ip, bmap, label, vrf_name_);
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(const Ip4Address &remote_vm_ip, 
                          const Ip4Address &server_ip, uint32_t plen, 
                          uint32_t label) {
        AddRemoteVmRoute(remote_vm_ip, server_ip, plen, label,
                         TunnelType::AllType());
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        Agent::GetDefaultInet4UcRouteTable()->AddResolveRoute(
                Agent::GetDefaultVrf(), server_ip, plen);
        client->WaitForIdle();
    }

    void AddGatewayRoute(const std::string &vrf_name, 
                         const Ip4Address &ip, const Ip4Address &server) {
        Agent::GetDefaultInet4UcRouteTable()->AddGatewayRoute(NULL, vrf_name,
                                                            ip, 32, server);
        client->WaitForIdle();
    }

    void AddVlanNHRoute(const std::string &vrf_name, const std::string &ip,
                        uint16_t plen, int id, uint16_t tag,
                        uint16_t label, const std::string &vn_name) {

        SecurityGroupList sg_l;
        Agent::GetDefaultInet4UcRouteTable()->AddVlanNHRoute
            (NULL, vrf_name_, Ip4Address::from_string(ip), plen, MakeUuid(id),
             tag, label, vn_name, sg_l);
        client->WaitForIdle();
    }

    void DeleteRoute(const Peer *peer, const std::string &vrf_name, 
                     const Ip4Address &addr, uint32_t plen) {
        Agent::GetDefaultInet4UcRouteTable()->DeleteReq(peer, vrf_name,
                                                      addr, plen);
        client->WaitForIdle();
        while (RouteFind(vrf_name, addr, plen) == true) {
            client->WaitForIdle();
        }
    }

    bool IsSameNH(const Ip4Address &ip1, uint32_t plen1, const Ip4Address &ip2, 
                  uint32_t plen2, const string vrf_name) {
        Inet4UcRoute *rt1 = RouteGet(vrf_name, ip1, plen1);
        const NextHop *nh1 = rt1->GetActiveNextHop();

        Inet4UcRoute *rt2 = RouteGet(vrf_name, ip1, plen1);
        const NextHop *nh2 = rt2->GetActiveNextHop();

        return (nh1 == nh2);
    }

    std::string vrf_name_;
    std::string eth_name_;
    Inet4UcRouteTable *rt_table_;
    Ip4Address  default_dest_ip_;
    Ip4Address  local_vm_ip_;
    Ip4Address  remote_vm_ip_;
    Ip4Address  vhost_ip_;
    Ip4Address  fabric_gw_ip_;
    Ip4Address  foreign_gw_ip_;
    Ip4Address  trap_ip_;
    Ip4Address  server1_ip_;
    Ip4Address  server2_ip_;
    Ip4Address  lpm1_ip_;
    Ip4Address  lpm2_ip_;
    Ip4Address  lpm3_ip_;
    Ip4Address  lpm4_ip_;
    Ip4Address  lpm5_ip_;
    bool is_gateway_configured;
    static TunnelType::Type type_;
};
TunnelType::Type RouteTest::type_;

class TestRtState : public DBState {
public:
    TestRtState() : DBState(), dummy_(0) { };
    int dummy_;
};

TEST_F(RouteTest, HostRoute_1) {
    //Host Route - Used to trap packets to agent
    //Add and delete host route
    AddHostRoute(trap_ip_);
    EXPECT_TRUE(RouteFind(vrf_name_, trap_ip_, 32));

    DeleteRoute(Agent::GetLocalPeer(), vrf_name_, trap_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, trap_ip_, 32));
}

TEST_F(RouteTest, VhostRecvRoute_1) {
    //Recv route for IP address set on vhost interface
    //Add and delete recv route on fabric VRF
    AddVhostRoute();
    EXPECT_TRUE(RouteFind(Agent::GetDefaultVrf(), vhost_ip_, 32));
    
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), vhost_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), vhost_ip_, 32));
}

TEST_F(RouteTest, LocalVmRoute_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind(vrf_name_, local_vm_ip_, 32));
    Inet4UcRoute *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt->GetDestVnName() == "vn1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    int i = 0;
    while(RouteFind(vrf_name_, local_vm_ip_, 32) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));
}

TEST_F(RouteTest, RemoteVmRoute_1) {
    AddRemoteVmRoute(remote_vm_ip_, fabric_gw_ip_, 32, MplsTable::kStartLabel);

    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UcRoute *rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    EXPECT_TRUE(rt->GetDestVnName() == vrf_name_);
    EXPECT_TRUE(rt->GetMplsLabel() == MplsTable::kStartLabel);

    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));
}

TEST_F(RouteTest, RemoteVmRoute_2) {
    //Add remote VM route, make it point to a server
    //whose ARP is not resolved, since there is no resolve
    //route, tunnel NH will be marked invalid.
    AddRemoteVmRoute(remote_vm_ip_, server1_ip_, 32, MplsTable::kStartLabel);

    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UcRoute *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    const NextHop *addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == false);

    //Add ARP for server IP address
    //Once Arp address is added, remote VM tunnel nexthop
    //would be reevaluated, and tunnel nexthop would be valid
    AddArp(server1_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(addr_nh->IsValid() == true);

    //Delete Remote VM route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    //Delete ARP route
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), 
                server1_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), server1_ip_, 32));
}

TEST_F(RouteTest, RemoteVmRoute_3) {
    //Add a remote VM route with prefix len 32
    AddRemoteVmRoute(remote_vm_ip_, server1_ip_, 32, MplsTable::kStartLabel);
    //Add a remote VM route with prefix len 24
    AddRemoteVmRoute(remote_vm_ip_, server1_ip_, 24, MplsTable::kStartLabel+1);

    //Delete more specific(/32) route, and verify in kernel
    //that specific route points to nexthop of /24 route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    //Cleanup /24 route also
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 24);
}

TEST_F(RouteTest, RemoteVmRoute_4) {
    //Add resolve route
    AddResolveRoute(server1_ip_, 24);
 
    //Add a remote VM route pointing to server in same
    //subnet, tunnel NH will trigger ARP resolution
    AddRemoteVmRoute(remote_vm_ip_, server1_ip_, 32, MplsTable::kStartLabel);

    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UcRoute *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    const NextHop *addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == false);

    EXPECT_TRUE(addr_nh->GetType() == NextHop::TUNNEL);
    if (addr_nh->GetType() == NextHop::TUNNEL) {
        const TunnelNH *tun = static_cast<const TunnelNH *>(addr_nh);
        TunnelType t(RouteTest::GetTunnelType());
        EXPECT_TRUE(tun->GetTunnelType().Compare(t));
    }

    //Add ARP for server IP address
    //Once Arp address is added, remote VM tunnel nexthop
    //would be reevaluated, and tunnel nexthop would be valid
    AddArp(server1_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(addr_nh->IsValid() == true);

    //Delete Remote VM route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    //Delete ARP route
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), 
                server1_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), server1_ip_, 32));

    //Delete Resolve route
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), 
                server1_ip_, 24);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), server1_ip_, 24));
}

TEST_F(RouteTest, RemoteVmRoute_5) {
    if (!is_gateway_configured) {
        Agent::SetGatewayId(fabric_gw_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, fabric_gw_ip_);
        client->WaitForIdle();
    }
    //Add remote VM route IP, pointing to 0.0.0.0
    AddRemoteVmRoute(remote_vm_ip_, server2_ip_, 32, MplsTable::kStartLabel);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UcRoute *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    const NextHop *addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == false);

    //Resolve ARP for gw
    AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", 
           eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(addr_nh->IsValid() == true);

    //Delete ARP for gw
    DelArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f",
           eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(addr_nh->IsValid() == false);

    //Delete remote server route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    if (!is_gateway_configured) {
        Agent::SetGatewayId(default_dest_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }
}

TEST_F(RouteTest, RemoteVmRoute_no_gw) {
    if (is_gateway_configured) {
        Agent::SetGatewayId(default_dest_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }
    //Add remote VM route IP, pointing to 0.0.0.0
    AddRemoteVmRoute(remote_vm_ip_, server2_ip_, 32, MplsTable::kStartLabel);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UcRoute *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    const NextHop *addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == true);
    EXPECT_TRUE(addr_nh->GetType() == NextHop::TUNNEL);
    if (addr_nh->GetType() == NextHop::TUNNEL) {
        const TunnelNH *tun = static_cast<const TunnelNH *>(addr_nh);
        EXPECT_TRUE(tun->GetRt()->GetActiveNextHop()->GetType() == NextHop::DISCARD);

        Agent::SetGatewayId(fabric_gw_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, fabric_gw_ip_);
        client->WaitForIdle();
        //addr_nh = addr_rt->GetActiveNextHop();
        EXPECT_TRUE(addr_nh->IsValid() == false);

        //Resolve ARP for gw
        AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", 
                eth_name_.c_str());
        client->WaitForIdle();
        EXPECT_TRUE(addr_nh->IsValid() == true);

        //Delete ARP for gw
        DelArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f",
                eth_name_.c_str());
        client->WaitForIdle();
        EXPECT_TRUE(addr_nh->IsValid() == false);

        Agent::SetGatewayId(default_dest_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }

    //Delete remote server route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    if (is_gateway_configured) {
        Agent::SetGatewayId(fabric_gw_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, fabric_gw_ip_);
        client->WaitForIdle();
    }
}

TEST_F(RouteTest, RemoteVmRoute_foreign_gw) {

    Agent::SetGatewayId(foreign_gw_ip_);
    Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                       Agent::GetDefaultVrf(),
                                       default_dest_ip_, 0, foreign_gw_ip_);
    client->WaitForIdle();

    //Add remote VM route IP, pointing to 0.0.0.0
    AddRemoteVmRoute(remote_vm_ip_, server2_ip_, 32, MplsTable::kStartLabel);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UcRoute *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    const NextHop *addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == true);
    EXPECT_TRUE(addr_nh->GetType() == NextHop::TUNNEL);
    if (addr_nh->GetType() == NextHop::TUNNEL) {
        const TunnelNH *tun = static_cast<const TunnelNH *>(addr_nh);
        EXPECT_TRUE(tun->GetRt()->GetActiveNextHop()->GetType() == NextHop::DISCARD);

    }

    //Delete remote server route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    if (is_gateway_configured) {
        Agent::SetGatewayId(fabric_gw_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, fabric_gw_ip_);
        client->WaitForIdle();
    } else {
        Agent::SetGatewayId(default_dest_ip_);
        Inet4UcRouteTable::AddGatewayRoute(Agent::GetLocalPeer(),
                                           Agent::GetDefaultVrf(),
                                           default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }
}

TEST_F(RouteTest, GatewayRoute_1) {
    //Addition and deletion of gateway route.
    //We add a gateway route as below
    //server2_ip ----->GW---->ARP NH
    //Server2 route and GW route, should have same NH
    AddGatewayRoute(Agent::GetDefaultVrf(), server2_ip_, fabric_gw_ip_);
    EXPECT_TRUE(RouteFind(Agent::GetDefaultVrf(), server2_ip_, 32));

    //Resolve ARP for subnet gateway route
    AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", 
           eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(server2_ip_, 32, fabric_gw_ip_, 32,
                         Agent::GetDefaultVrf()));

    //Change mac, and verify that nexthop of gateway route
    //also get updated
    AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", 
           eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(server2_ip_, 32, fabric_gw_ip_, 32,
                         Agent::GetDefaultVrf()));

    //Delete indirect route
    DeleteRoute(NULL, Agent::GetDefaultVrf(), server2_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), server2_ip_, 32));

    //Delete ARP route, since no covering resolve route
    //is present server2 route would point to discard NH
    DelArp(fabric_gw_ip_.to_string(), "0a:0b:0c:0d:0e:0e", eth_name_);
    client->WaitForIdle();
}

TEST_F(RouteTest, GatewayRoute_2) {
    Ip4Address a = Ip4Address::from_string("4.4.4.4");
    Ip4Address b = Ip4Address::from_string("5.5.5.5");
    Ip4Address c = Ip4Address::from_string("6.6.6.6");
    Ip4Address d = Ip4Address::from_string("7.7.7.7");

    //Add gateway route a reachable via b, b reachable
    //via c, c reachable via d.
    AddArp(d.to_string().c_str(), "0a:0b:0c:0d:0e:0f", eth_name_.c_str());
    AddGatewayRoute(Agent::GetDefaultVrf(), c, d);
    AddGatewayRoute(Agent::GetDefaultVrf(), b, c);
    AddGatewayRoute(Agent::GetDefaultVrf(), a, b);
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(a, 32, b, 32, Agent::GetDefaultVrf()));
    EXPECT_TRUE(IsSameNH(b, 32, c, 32, Agent::GetDefaultVrf()));
    EXPECT_TRUE(IsSameNH(c, 32, d, 32, Agent::GetDefaultVrf()));

    DelArp(d.to_string().c_str(), "0a:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(a, 32, b, 32, Agent::GetDefaultVrf()));
    EXPECT_TRUE(IsSameNH(b, 32, c, 32, Agent::GetDefaultVrf()));
    EXPECT_TRUE(IsSameNH(c, 32, d, 32, Agent::GetDefaultVrf()));

    DeleteRoute(NULL, Agent::GetDefaultVrf(), a, 32);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), a ,32));

    DeleteRoute(NULL, Agent::GetDefaultVrf(), b, 32);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), b, 32));

    DeleteRoute(NULL, Agent::GetDefaultVrf(), c, 32);
    EXPECT_FALSE(RouteFind(Agent::GetDefaultVrf(), c, 32));
}

TEST_F(RouteTest, FindLPM) {
    Inet4UcRoute *rt;
    AddResolveRoute(lpm1_ip_, 8);
    client->WaitForIdle();
    AddResolveRoute(lpm2_ip_, 16);
    client->WaitForIdle();
    AddResolveRoute(lpm3_ip_, 24);
    client->WaitForIdle();
    AddArp(lpm4_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    AddArp(lpm5_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0a", eth_name_.c_str());
    client->WaitForIdle();

    rt = Agent::GetDefaultInet4UcRouteTable()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm4_ip_, rt->GetIpAddress());
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm4_ip_, 32);
    client->WaitForIdle();
    rt = Agent::GetDefaultInet4UcRouteTable()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm3_ip_, rt->GetIpAddress());
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm3_ip_, 24);
    client->WaitForIdle();
    rt = Agent::GetDefaultInet4UcRouteTable()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm2_ip_, rt->GetIpAddress());
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm2_ip_, 16);
    client->WaitForIdle();
    rt = Agent::GetDefaultInet4UcRouteTable()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm1_ip_, rt->GetIpAddress());
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm1_ip_, 8);
    client->WaitForIdle();
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm5_ip_, 32);
    client->WaitForIdle();
}

TEST_F(RouteTest, VlanNHRoute_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind(vrf_name_, local_vm_ip_, 32));
    Inet4UcRoute *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt != NULL);
    if (rt) {
        EXPECT_TRUE(rt->GetDestVnName() == "vn1");
    }

    // Add service interface-1
    AddVrf("vrf2");
    AddVmPortVrf("ser1", "2.2.2.1", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");

    client->WaitForIdle();
    // Validate service vlan route
    rt = RouteGet("vrf2", Ip4Address::from_string("2.2.2.1"), 32);
    EXPECT_TRUE(rt != NULL);

    // Add a route using NH created for service interface
    client->WaitForIdle();
    const VlanNH *vlan_nh = static_cast<const VlanNH *>(rt->GetActiveNextHop());
    AddVlanNHRoute("vrf1", "2.2.2.0", 24, 1, 1, rt->GetMplsLabel(), "TestVn");
    rt = RouteGet("vrf1", Ip4Address::from_string("2.2.2.0"), 24);
    EXPECT_TRUE(rt != NULL);
    if (rt) {
        EXPECT_TRUE(rt->GetDestVnName() == "TestVn");
    }

    AddVmPortVrf("ser1", "2.2.2.1", 10);
    client->WaitForIdle();

    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    DelVmPortVrf("ser1");
    int i = 0;
    while (i++ < 50) {
        rt = RouteGet("vrf2", Ip4Address::from_string("2.2.2.1"), 32);
        if (rt == NULL) {
            break;
        }
        client->WaitForIdle();
    }
    EXPECT_TRUE(rt == NULL);

    Ip4Address addr = Ip4Address::from_string("2.2.2.10");
    DeleteRoute(NULL, "vrf1", addr, 24);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    i = 0;
    while(RouteFind(vrf_name_, local_vm_ip_, 32) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));

    DeleteRoute(NULL, "vrf1", Ip4Address::from_string("2.2.2.0"), 24);
    client->WaitForIdle();
    WAIT_FOR(100, 100,
            (RouteGet("vrf1", Ip4Address::from_string("2.2.2.0"), 24) == NULL));

    DelVrf("vrf2");
    client->WaitForIdle();
}

class TestNhState : public DBState {
public:
    TestNhState() : DBState(), dummy_(0) { };
    int dummy_;
};

class TestNhPeer : public Peer {
public:
    TestNhPeer() : Peer(BGP_PEER, "TestNH"), dummy_(0) { };
    int dummy_;
};

TEST_F(RouteTest, RouteToDeletedNH_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind(vrf_name_, local_vm_ip_, 32));
    Inet4UcRoute *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt->GetDestVnName() == "vn1");

    // Add state to NextHop so that entry is not freed on delete
    DBTableBase::ListenerId id = 
        Agent::GetNextHopTable()->Register(boost::bind(&RouteTest::NhListener, this, _1, _2));
    InterfaceNHKey key(new VmPortInterfaceKey(MakeUuid(1), ""), false);
    NextHop *nh = static_cast<NextHop *>(Agent::GetNextHopTable()->FindActiveEntry(&key));
    TestNhState *state = new TestNhState();
    nh->SetState(Agent::GetNextHopTable(), id, state);

    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetNextHopTable()->FindActiveEntry(&key) == NULL);
    EXPECT_TRUE(Agent::GetNextHopTable()->Find(&key, true) != NULL);

    TestNhPeer *peer = new TestNhPeer();
    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    Inet4UcRouteTable::AddLocalVmRoute(peer, "vrf1", addr, 32, MakeUuid(1), "Test",
                                       10, false);
    client->WaitForIdle();

    Inet4UcRouteTable::DeleteReq(peer, "vrf1", addr, 32);
    client->WaitForIdle();

    nh->ClearState(Agent::GetNextHopTable(), id);
    client->WaitForIdle();
    delete state;
    delete peer;

    Agent::GetNextHopTable()->Unregister(id);
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetNextHopTable()->Find(&key, true) == NULL);
    int i = 0;
    while(RouteFind(vrf_name_, local_vm_ip_, 32) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));
}

TEST_F(RouteTest, RouteToDeletedNH_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    TestNhPeer *peer1 = new TestNhPeer();
    TestNhPeer *peer2 = new TestNhPeer();

    Ip4Address addr = Ip4Address::from_string("1.1.1.1");
    Inet4UcRouteTable::AddLocalVmRoute(peer1, "vrf1", addr, 32, MakeUuid(1), "Test",
                                       10, false);
    Inet4UcRouteTable::AddLocalVmRoute(peer2, "vrf1", addr, 32, MakeUuid(1), "Test",
                                       10, false);
    client->WaitForIdle();

    DelNode("access-control-list", "acl1");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();

    Inet4UcRouteTable::AddLocalVmRoute(peer1, "vrf1", addr, 32, MakeUuid(1), "Test",
                                       10, false);
    client->WaitForIdle();

    Inet4UcRouteTable::DeleteReq(peer1, "vrf1", addr, 32);
    Inet4UcRouteTable::DeleteReq(peer2, "vrf1", addr, 32);
    client->WaitForIdle();

    delete peer1;
    delete peer2;

    DeleteVmportEnv(input, 1, true, 1);
    client->WaitForIdle();

    WAIT_FOR(100, 100, (RouteFind("vrf1", addr, 32) == false));
    EXPECT_FALSE(VmPortFind(input, 0));
}

TEST_F(RouteTest, RouteToInactiveInterface) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind(vrf_name_, local_vm_ip_, 32));
    Inet4UcRoute *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt->GetDestVnName() == "vn1");

    TestNhPeer *peer = new TestNhPeer();
    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    Inet4UcRouteTable::AddLocalVmRoute(peer, "vrf1", addr, 32, MakeUuid(1), "Test",
                                       10, false);
    client->WaitForIdle();
    DelVn("vn1");
    client->WaitForIdle();

    EXPECT_TRUE(VmPortInactive(1));

    Inet4UcRouteTable::AddLocalVmRoute(peer, "vrf1", addr, 32, MakeUuid(1), "Test",
                                       10, false);
    client->WaitForIdle();

    Inet4UcRouteTable::DeleteReq(peer, "vrf1", addr, 32);
    client->WaitForIdle();
    delete peer;

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    int i = 0;
    while(RouteFind(vrf_name_, local_vm_ip_, 32) == true && ++i < 25) {
        client->WaitForIdle();
    }
    EXPECT_FALSE(VmPortFind(input, 0));
}

TEST_F(RouteTest, RtEntryReuse) {
    client->Reset();
    DBTableBase::ListenerId id = 
        Agent::GetDefaultInet4UcRouteTable()->Register(boost::bind(&RouteTest::RtListener, this, _1, _2));

    Inet4UcRoute *rt;
    Inet4UcRoute *rt_hold;
    AddResolveRoute(lpm3_ip_, 24);
    client->WaitForIdle();
    AddArp(lpm4_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();

    rt = Agent::GetDefaultInet4UcRouteTable()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm4_ip_, rt->GetIpAddress());

    boost::scoped_ptr<TestRtState> state(new TestRtState());
    rt->SetState(Agent::GetDefaultInet4UcRouteTable(), id, state.get());
    rt_hold = rt;
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm4_ip_, 32);
    client->WaitForIdle();
    rt = Agent::GetDefaultInet4UcRouteTable()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm3_ip_, rt->GetIpAddress());

    AddArp(lpm4_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    rt = Agent::GetDefaultInet4UcRouteTable()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm4_ip_, rt->GetIpAddress());
    EXPECT_EQ(rt, rt_hold);
    rt->ClearState(Agent::GetDefaultInet4UcRouteTable(), id);

    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm4_ip_, 32);
    client->WaitForIdle();
    DeleteRoute(Agent::GetLocalPeer(), Agent::GetDefaultVrf(), lpm3_ip_, 24);
    client->WaitForIdle();

    Agent::GetDefaultInet4UcRouteTable()->Unregister(id);
}


int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    if (vm.count("config")) {
        eth_itf = Agent::GetIpFabricItfName();
    } else {
        eth_itf = "eth0";
    }

    RouteTest::SetTunnelType(TunnelType::MPLS_GRE);
    int ret = RUN_ALL_TESTS();
    RouteTest::SetTunnelType(TunnelType::MPLS_UDP);
    return ret + RUN_ALL_TESTS();
}
