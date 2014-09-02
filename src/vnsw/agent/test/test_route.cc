/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "oper/tunnel_nh.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "oper/path_preference.h"
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "kstate/test/test_kstate_util.h"
#include "vr_types.h"

#include <controller/controller_export.h> 

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

        if (Agent::GetInstance()->router_id_configured()) {
            vhost_ip_ = Agent::GetInstance()->router_id();
        } else {
            vhost_ip_ = Ip4Address::from_string("10.1.1.10");
        }
        if (Agent::GetInstance()->vhost_default_gateway() != default_dest_ip_) {
            is_gateway_configured = true;
            fabric_gw_ip_ = Agent::GetInstance()->vhost_default_gateway();
        } else {
            is_gateway_configured = false;
            fabric_gw_ip_ = Ip4Address::from_string("10.1.1.254");
        }
        foreign_gw_ip_ = Ip4Address::from_string("10.10.10.254");
        server1_ip_ = Ip4Address::from_string("10.1.1.11");
        server2_ip_ = Ip4Address::from_string("10.1.122.11");
        local_vm_ip_ = Ip4Address::from_string("1.1.1.10");
        subnet_vm_ip_1_ = Ip4Address::from_string("1.1.1.0");
        subnet_vm_ip_2_ = Ip4Address::from_string("2.2.2.96");
        subnet_vm_ip_3_ = Ip4Address::from_string("3.3.0.0");
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
        PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                                eth_name_,
                                Agent::GetInstance()->fabric_vrf_name(),
                                false);
        AddResolveRoute(server1_ip_, 24);
        client->WaitForIdle();

        agent_ = Agent::GetInstance();
    }

    virtual void TearDown() {
        TestRouteTable table1(1);
        WAIT_FOR(100, 100, (table1.Size() == 0));
        EXPECT_EQ(table1.Size(), 0U);

        TestRouteTable table2(2);
        WAIT_FOR(100, 100, (table2.Size() == 0));
        EXPECT_EQ(table2.Size(), 0U);

        TestRouteTable table3(3);
        WAIT_FOR(100, 100, (table3.Size() == 0));
        EXPECT_EQ(table3.Size(), 0U);

        VrfDelReq(vrf_name_.c_str());
        client->WaitForIdle();
        WAIT_FOR(100, 100, (VrfFind(vrf_name_.c_str()) != true));
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 1);
    }

    void AddHostRoute(Ip4Address addr) {
        Agent::GetInstance()->fabric_inet4_unicast_table()->AddHostRoute(
               vrf_name_, addr, 32, Agent::GetInstance()->fabric_vn_name());
        client->WaitForIdle();
    }

    void AddVhostRoute() {
        Agent::GetInstance()->fabric_inet4_unicast_table()->AddVHostRecvRouteReq(
                                                Agent::GetInstance()->local_peer(),
                                                Agent::GetInstance()->fabric_vrf_name(),
                                                "vhost0", vhost_ip_, 32, "", false);
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(const Ip4Address &remote_vm_ip, 
                          const Ip4Address &server_ip, uint32_t plen, 
                          uint32_t label, TunnelType::TypeBmap bmap) {
        //Passing vn name as vrf name itself
        Inet4TunnelRouteAdd(NULL, vrf_name_, remote_vm_ip, plen, server_ip, 
                            bmap, label, vrf_name_,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(const Ip4Address &remote_vm_ip, 
                          const Ip4Address &server_ip, uint32_t plen, 
                          uint32_t label) {
        AddRemoteVmRoute(remote_vm_ip, server_ip, plen, label,
                         TunnelType::AllType());
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        Agent::GetInstance()->fabric_inet4_unicast_table()->AddResolveRoute(
                Agent::GetInstance()->fabric_vrf_name(), server_ip, plen);
        client->WaitForIdle();
    }

    void AddGatewayRoute(const std::string &vrf_name, 
                         const Ip4Address &ip, int plen,
                         const Ip4Address &server) {
        Agent::GetInstance()->fabric_inet4_unicast_table()->AddGatewayRouteReq
            (vrf_name, ip, plen, server, "");

        client->WaitForIdle();
    }

    void AddVlanNHRoute(const std::string &vrf_name, const std::string &ip,
                        uint16_t plen, int id, uint16_t tag,
                        uint16_t label, const std::string &vn_name) {

        SecurityGroupList sg_l;
        Agent::GetInstance()->fabric_inet4_unicast_table()->
            AddVlanNHRouteReq(NULL, vrf_name_, Ip4Address::from_string(ip), plen, 
                              MakeUuid(id), tag, label, vn_name, sg_l,
                              PathPreference());
        client->WaitForIdle();
    }

    void DeleteRoute(const Peer *peer, const std::string &vrf_name, 
                     const Ip4Address &addr, uint32_t plen) {
        Agent::GetInstance()->fabric_inet4_unicast_table()->DeleteReq(peer, vrf_name,
                                                                            addr, plen, NULL);
        client->WaitForIdle();
        while (RouteFind(vrf_name, addr, plen) == true) {
            client->WaitForIdle();
        }
    }

    bool IsSameNH(const Ip4Address &ip1, uint32_t plen1, const Ip4Address &ip2, 
                  uint32_t plen2, const string vrf_name) {
        Inet4UnicastRouteEntry *rt1 = RouteGet(vrf_name, ip1, plen1);
        const NextHop *nh1 = rt1->GetActiveNextHop();

        Inet4UnicastRouteEntry *rt2 = RouteGet(vrf_name, ip1, plen1);
        const NextHop *nh2 = rt2->GetActiveNextHop();

        return (nh1 == nh2);
    }

    std::string vrf_name_;
    std::string eth_name_;
    Ip4Address  default_dest_ip_;
    Ip4Address  local_vm_ip_;
    Ip4Address  subnet_vm_ip_1_;
    Ip4Address  subnet_vm_ip_2_;
    Ip4Address  subnet_vm_ip_3_;
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
    Agent *agent_;
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

    DeleteRoute(Agent::GetInstance()->local_peer(), vrf_name_, trap_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, trap_ip_, 32));
}

TEST_F(RouteTest, SubnetRoute_1) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"2.2.2.100", 28, "2.2.2.200", true},
        {"3.3.3.0", 16, "3.3.30.200", true},
    };
    client->Reset();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();
    Inet4UnicastRouteEntry *rt1 = RouteGet(vrf_name_, subnet_vm_ip_1_, 24);
    Inet4UnicastRouteEntry *rt2 = RouteGet(vrf_name_, subnet_vm_ip_2_, 28);
    Inet4UnicastRouteEntry *rt3 = RouteGet(vrf_name_, subnet_vm_ip_3_, 16);

    EXPECT_TRUE(rt1 != NULL);
    EXPECT_TRUE(rt2 != NULL);
    EXPECT_TRUE(rt3 != NULL);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt3->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->IsRPFInvalid());
    EXPECT_TRUE(rt2->IsRPFInvalid());
    EXPECT_TRUE(rt3->IsRPFInvalid());

    const CompositeNH *cnh1 = static_cast<const CompositeNH *>(rt1->GetActiveNextHop());
    const CompositeNH *cnh2 = static_cast<const CompositeNH *>(rt2->GetActiveNextHop());
    const CompositeNH *cnh3 = static_cast<const CompositeNH *>(rt3->GetActiveNextHop());
    EXPECT_TRUE(cnh1->composite_nh_type() == Composite::EVPN);
    EXPECT_TRUE(cnh2->composite_nh_type() == Composite::EVPN);
    EXPECT_TRUE(cnh3->composite_nh_type() == Composite::EVPN);

    //Call for sandesh
    Inet4UcRouteReq *uc_list_req = new Inet4UcRouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    uc_list_req->set_vrf_index(1);
    uc_list_req->HandleRequest();
    client->WaitForIdle();
    uc_list_req->Release();
    client->WaitForIdle();

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
}

/* Change IPAM list and verify clear/add of subnet route */
TEST_F(RouteTest, SubnetRoute_2) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
        {"2.2.2.100", 28, "2.2.2.200", true},
        {"3.3.3.0", 16, "3.3.30.200", true},
    };

    IpamInfo ipam_info_2[] = {
        {"2.2.2.100", 28, "2.2.2.200", true},
    };

    IpamInfo ipam_info_3[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };

    client->Reset();
    CreateVmportEnv(input, 1, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 3);
    client->WaitForIdle();
    Inet4UnicastRouteEntry *rt1 = RouteGet(vrf_name_, subnet_vm_ip_1_, 24);
    Inet4UnicastRouteEntry *rt2 = RouteGet(vrf_name_, subnet_vm_ip_2_, 28);
    Inet4UnicastRouteEntry *rt3 = RouteGet(vrf_name_, subnet_vm_ip_3_, 16);

    EXPECT_TRUE(rt1 != NULL);
    EXPECT_TRUE(rt2 != NULL);
    EXPECT_TRUE(rt3 != NULL);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt3->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt1->IsRPFInvalid());
    EXPECT_TRUE(rt2->IsRPFInvalid());
    EXPECT_TRUE(rt3->IsRPFInvalid());

    AddIPAM("vn1", ipam_info_2, 1);
    client->WaitForIdle();

    rt1 = RouteGet(vrf_name_, subnet_vm_ip_1_, 24);
    rt2 = RouteGet(vrf_name_, subnet_vm_ip_2_, 28);
    rt3 = RouteGet(vrf_name_, subnet_vm_ip_3_, 16);
    EXPECT_TRUE(rt1 == NULL);
    EXPECT_TRUE(rt2 != NULL);
    EXPECT_TRUE(rt3 == NULL);
    EXPECT_TRUE(rt2->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt2->IsRPFInvalid());

    AddIPAM("vn1", ipam_info_3, 1);
    client->WaitForIdle();

    //Just check for sandesh message handling
    Inet4UcRouteReq *uc_list_req = new Inet4UcRouteReq();
    std::vector<int> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateSandeshResponse, _1, result));
    uc_list_req->set_vrf_index(1);
    uc_list_req->HandleRequest();
    client->WaitForIdle();
    uc_list_req->Release();
    client->WaitForIdle();

    rt1 = RouteGet(vrf_name_, subnet_vm_ip_1_, 24);
    rt2 = RouteGet(vrf_name_, subnet_vm_ip_2_, 28);
    rt3 = RouteGet(vrf_name_, subnet_vm_ip_3_, 16);
    EXPECT_TRUE(rt1 != NULL);
    EXPECT_TRUE(rt2 == NULL);
    EXPECT_TRUE(rt3 == NULL);
    EXPECT_TRUE(rt1->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    client->Reset();
    DelIPAM("vn1");
    client->WaitForIdle();
    rt1 = RouteGet(vrf_name_, subnet_vm_ip_1_, 24);
    rt2 = RouteGet(vrf_name_, subnet_vm_ip_2_, 28);
    rt3 = RouteGet(vrf_name_, subnet_vm_ip_3_, 16);
    EXPECT_TRUE(rt1 == NULL);
    EXPECT_TRUE(rt2 == NULL);
    EXPECT_TRUE(rt3 == NULL);

    DeleteVmportEnv(input, 1, 1, 0);
    client->WaitForIdle();
}

TEST_F(RouteTest, VhostRecvRoute_1) {
    //Recv route for IP address set on vhost interface
    //Add and delete recv route on fabric VRF
    AddVhostRoute();
    EXPECT_TRUE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), vhost_ip_, 32));
    
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), vhost_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), vhost_ip_, 32));
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
    Inet4UnicastRouteEntry *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");
    EXPECT_TRUE(rt->GetActivePath()->vxlan_id() == VxLanTable::kInvalidvxlan_id);
    EXPECT_TRUE(rt->GetActivePath()->tunnel_bmap() == TunnelType::MplsType());
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
    Inet4UnicastRouteEntry *rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    EXPECT_TRUE(rt->dest_vn_name() == vrf_name_);
    EXPECT_TRUE(rt->GetActiveLabel() == MplsTable::kStartLabel);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::TUNNEL);

    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));
}

TEST_F(RouteTest, RemoteVmRoute_2) {
    //Add remote VM route, make it point to a server
    //whose ARP is not resolved, since there is no resolve
    //route, tunnel NH will be marked invalid.
    AddRemoteVmRoute(remote_vm_ip_, server1_ip_, 32, MplsTable::kStartLabel);

    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UnicastRouteEntry *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
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
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), 
                server1_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server1_ip_, 32));
    DelArp(server1_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
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
    Inet4UnicastRouteEntry *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
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
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), 
                server1_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server1_ip_, 32));

    //Delete Resolve route
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), 
                server1_ip_, 24);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server1_ip_, 24));
    DelArp(server1_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
}

TEST_F(RouteTest, RemoteVmRoute_5) {
    if (!is_gateway_configured) {
        Agent::GetInstance()->set_vhost_default_gateway(fabric_gw_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                        default_dest_ip_, 0, fabric_gw_ip_);
        client->WaitForIdle();
    }
    //Add remote VM route IP, pointing to 0.0.0.0
    AddRemoteVmRoute(remote_vm_ip_, server2_ip_, 32, MplsTable::kStartLabel);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UnicastRouteEntry *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    const NextHop *addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == false);

    //Resolve ARP for gw
    AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", 
           eth_name_.c_str());
    client->WaitForIdle();
    addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == true);

    //Delete ARP for gw
    DelArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f",
           eth_name_.c_str());
    client->WaitForIdle();
    addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == false);

    //Delete remote server route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    if (!is_gateway_configured) {
        Agent::GetInstance()->set_vhost_default_gateway(default_dest_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                        default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }
}

TEST_F(RouteTest, RemoteVmRoute_no_gw) {
    if (is_gateway_configured) {
        Agent::GetInstance()->set_vhost_default_gateway(default_dest_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                        default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }
    //Add remote VM route IP, pointing to 0.0.0.0
    AddRemoteVmRoute(remote_vm_ip_, server2_ip_, 32, MplsTable::kStartLabel);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UnicastRouteEntry *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    const NextHop *addr_nh = addr_rt->GetActiveNextHop();
    EXPECT_TRUE(addr_nh->IsValid() == true);
    EXPECT_TRUE(addr_nh->GetType() == NextHop::TUNNEL);
    if (addr_nh->GetType() == NextHop::TUNNEL) {
        const TunnelNH *tun = static_cast<const TunnelNH *>(addr_nh);
        EXPECT_TRUE(tun->GetRt()->GetActiveNextHop()->GetType() == NextHop::DISCARD);

        Agent::GetInstance()->set_vhost_default_gateway(fabric_gw_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
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

        Agent::GetInstance()->set_vhost_default_gateway(default_dest_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                        default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }

    //Delete remote server route
    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    if (is_gateway_configured) {
        Agent::GetInstance()->set_vhost_default_gateway(fabric_gw_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                        default_dest_ip_, 0, fabric_gw_ip_);
        client->WaitForIdle();
    }
}

TEST_F(RouteTest, RemoteVmRoute_foreign_gw) {

    Agent::GetInstance()->set_vhost_default_gateway(foreign_gw_ip_);
    AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                    default_dest_ip_, 0, foreign_gw_ip_);
    client->WaitForIdle();

    //Add remote VM route IP, pointing to 0.0.0.0
    AddRemoteVmRoute(remote_vm_ip_, server2_ip_, 32, MplsTable::kStartLabel);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UnicastRouteEntry *addr_rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
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
        Agent::GetInstance()->set_vhost_default_gateway(fabric_gw_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                        default_dest_ip_, 0, fabric_gw_ip_);
        client->WaitForIdle();
    } else {
        Agent::GetInstance()->set_vhost_default_gateway(default_dest_ip_);
        AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(),
                        default_dest_ip_, 0, default_dest_ip_);
        client->WaitForIdle();
    }
}

TEST_F(RouteTest, GatewayRoute_1) {
    //Addition and deletion of gateway route.
    //We add a gateway route as below
    //server2_ip ----->GW---->ARP NH
    //Server2 route and GW route, should have same NH
    AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(), server2_ip_, 32,
                    fabric_gw_ip_);
    EXPECT_TRUE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server2_ip_, 32));

    //Resolve ARP for subnet gateway route
    AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", 
           eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(server2_ip_, 32, fabric_gw_ip_, 32,
                         Agent::GetInstance()->fabric_vrf_name()));

    //Change mac, and verify that nexthop of gateway route
    //also get updated
    AddArp(fabric_gw_ip_.to_string().c_str(), "0a:0b:0c:0d:0e:0f", 
           eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(server2_ip_, 32, fabric_gw_ip_, 32,
                         Agent::GetInstance()->fabric_vrf_name()));

    //Delete indirect route
    DeleteRoute(Agent::GetInstance()->local_peer(),
                Agent::GetInstance()->fabric_vrf_name(), server2_ip_, 32);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server2_ip_, 32));

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
    AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(), c, 32, d);
    AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(), b, 32, c);
    AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(), a, 32, b);
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(a, 32, b, 32, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(IsSameNH(b, 32, c, 32, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(IsSameNH(c, 32, d, 32, Agent::GetInstance()->fabric_vrf_name()));

    DelArp(d.to_string().c_str(), "0a:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    EXPECT_TRUE(IsSameNH(a, 32, b, 32, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(IsSameNH(b, 32, c, 32, Agent::GetInstance()->fabric_vrf_name()));
    EXPECT_TRUE(IsSameNH(c, 32, d, 32, Agent::GetInstance()->fabric_vrf_name()));

    DeleteRoute(Agent::GetInstance()->local_peer(),
                Agent::GetInstance()->fabric_vrf_name(), a, 32);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), a ,32));

    DeleteRoute(Agent::GetInstance()->local_peer(),
                Agent::GetInstance()->fabric_vrf_name(), b, 32);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), b, 32));

    DeleteRoute(Agent::GetInstance()->local_peer(),
                Agent::GetInstance()->fabric_vrf_name(), c, 32);
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), c, 32));
}

// Delete unresolved gateway route
TEST_F(RouteTest, GatewayRoute_3) {
    Ip4Address a = Ip4Address::from_string("4.4.4.4");
    Ip4Address gw = Ip4Address::from_string("5.5.5.254");

    // Add gateway route. Gateway
    AddGatewayRoute(agent_->fabric_vrf_name(), a, 32, gw);
    client->WaitForIdle();

    Inet4UnicastRouteEntry* rt = RouteGet(agent_->fabric_vrf_name(), a, 32);
    EXPECT_TRUE(rt != NULL);

    DeleteRoute(agent_->local_peer(), agent_->fabric_vrf_name(), a, 32);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind(agent_->fabric_vrf_name(), a ,32));

    gw = Ip4Address::from_string("10.1.1.253");
    AddGatewayRoute(agent_->fabric_vrf_name(), a, 32, gw);
    client->WaitForIdle();

    rt = RouteGet(agent_->fabric_vrf_name(), a, 32);
    EXPECT_TRUE(rt != NULL);

    DeleteRoute(agent_->local_peer(), agent_->fabric_vrf_name(), a, 32);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind(agent_->fabric_vrf_name(), a ,32));
}

TEST_F(RouteTest, ResyncUnresolvedRoute_1) {
    // There should be no unresolved route
    Inet4UnicastAgentRouteTable *table =
        Agent::GetInstance()->fabric_inet4_unicast_table();
    EXPECT_EQ(table->unresolved_route_size(), 0);
    Ip4Address gw = Ip4Address::from_string("1.1.1.2");

    // Add an unresolved gateway route.
    // Add a route to force RESYNC of unresolved route
    AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(), server1_ip_, 32,
                    gw);
    EXPECT_TRUE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server1_ip_,
                          32));
    // One unresolved route should be added
    EXPECT_EQ(table->unresolved_route_size(), 1);

    Inet4UnicastRouteEntry *rt =
        RouteGet(Agent::GetInstance()->fabric_vrf_name(), server1_ip_, 32);
    Inet4UnicastAgentRouteTable::ReEvaluatePaths(rt->vrf()->GetName(),
                                                 rt->addr(),
                                                 rt->plen());
    client->WaitForIdle();
    EXPECT_EQ(table->unresolved_route_size(), 1);

    // Add second route.
    AddGatewayRoute(Agent::GetInstance()->fabric_vrf_name(), server2_ip_, 32,
                    gw);
    EXPECT_TRUE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server2_ip_,
                          32));
    WAIT_FOR(100, 1000, (table->unresolved_route_size() == 2));

    DeleteRoute(Agent::GetInstance()->local_peer(),
                Agent::GetInstance()->fabric_vrf_name(), server1_ip_, 32);
    DeleteRoute(Agent::GetInstance()->local_peer(),
                Agent::GetInstance()->fabric_vrf_name(), server2_ip_, 32);

    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server1_ip_,
                           32));
    EXPECT_FALSE(RouteFind(Agent::GetInstance()->fabric_vrf_name(), server2_ip_,
                           32));
}

TEST_F(RouteTest, FindLPM) {
    Inet4UnicastRouteEntry *rt;
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

    rt = Agent::GetInstance()->fabric_inet4_unicast_table()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm4_ip_, rt->addr());
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm4_ip_, 32);
    client->WaitForIdle();
    rt = Agent::GetInstance()->fabric_inet4_unicast_table()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm3_ip_, rt->addr());
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm3_ip_, 24);
    client->WaitForIdle();
    rt = Agent::GetInstance()->fabric_inet4_unicast_table()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm2_ip_, rt->addr());
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm2_ip_, 16);
    client->WaitForIdle();
    rt = Agent::GetInstance()->fabric_inet4_unicast_table()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm1_ip_, rt->addr());
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm1_ip_, 8);
    client->WaitForIdle();
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm5_ip_, 32);
    client->WaitForIdle();
    DelArp(lpm4_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    DelArp(lpm5_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0a", eth_name_.c_str());
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
    Inet4UnicastRouteEntry *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt != NULL);
    if (rt) {
        EXPECT_TRUE(rt->dest_vn_name() == "vn1");
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
    AddVlanNHRoute("vrf1", "2.2.2.0", 24, 1, 1, rt->GetActiveLabel(), "TestVn");
    rt = RouteGet("vrf1", Ip4Address::from_string("2.2.2.0"), 24);
    EXPECT_TRUE(rt != NULL);
    if (rt) {
        EXPECT_TRUE(rt->dest_vn_name() == "TestVn");
    }
    EXPECT_TRUE(rt->GetActivePath()->tunnel_bmap() == TunnelType::MplsType());

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
    Inet4UnicastRouteEntry *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");

    // Add state to NextHop so that entry is not freed on delete
    DBTableBase::ListenerId id = 
        Agent::GetInstance()->nexthop_table()->Register(
               boost::bind(&RouteTest::NhListener, this, _1, _2));
    InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                          MakeUuid(1), ""),
                       false, InterfaceNHFlags::INET4);
    NextHop *nh = 
        static_cast<NextHop *>(Agent::GetInstance()->nexthop_table()->FindActiveEntry(&key));
    TestNhState *state = new TestNhState();
    nh->SetState(Agent::GetInstance()->nexthop_table(), id, state);

    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->nexthop_table()->FindActiveEntry(&key) == NULL);
    EXPECT_TRUE(Agent::GetInstance()->nexthop_table()->Find(&key, true) != NULL);

    TestNhPeer *peer = new TestNhPeer();
    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq(peer, "vrf1",
                                                            addr, 32,
                                                            MakeUuid(1), "Test",
                                                            10,
                                                            SecurityGroupList(),
                                                            false,
                                                            PathPreference(),
                                                            Ip4Address(0));
    client->WaitForIdle();

    Inet4UnicastAgentRouteTable::DeleteReq(peer, "vrf1", addr, 32, NULL);
    client->WaitForIdle();

    nh->ClearState(Agent::GetInstance()->nexthop_table(), id);
    client->WaitForIdle();
    delete state;
    delete peer;

    Agent::GetInstance()->nexthop_table()->Unregister(id);
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(Agent::GetInstance()->nexthop_table()->Find(&key, true) == NULL);
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
    agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq(peer1, "vrf1",
                                                            addr, 32,
                                                            MakeUuid(1),
                                                            "Test", 10,
                                                            SecurityGroupList(),
                                                            false,
                                                            PathPreference(),
                                                            Ip4Address(0));
    agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq(peer2, "vrf1",
                                                            addr, 32,
                                                            MakeUuid(1),
                                                            "Test", 10,
                                                            SecurityGroupList(),
                                                            false,
                                                            PathPreference(),
                                                            Ip4Address(0));
    client->WaitForIdle();

    DelNode("access-control-list", "acl1");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();

    agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq(peer1, "vrf1",
                                                            addr, 32,
                                                            MakeUuid(1),
                                                            "Test", 10,
                                                            SecurityGroupList(),
                                                            false,
                                                            PathPreference(),
                                                            Ip4Address(0));
    client->WaitForIdle();

    Inet4UnicastAgentRouteTable::DeleteReq(peer1, "vrf1", addr, 32, NULL);
    Inet4UnicastAgentRouteTable::DeleteReq(peer2, "vrf1", addr, 32, NULL);
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
    Inet4UnicastRouteEntry *rt = RouteGet(vrf_name_, local_vm_ip_, 32);
    EXPECT_TRUE(rt->dest_vn_name() == "vn1");

    TestNhPeer *peer = new TestNhPeer();
    Ip4Address addr = Ip4Address::from_string("1.1.1.10");
    agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq(peer, "vrf1",
                                                            addr, 32,
                                                            MakeUuid(1),
                                                            "Test", 10,
                                                            SecurityGroupList(),
                                                            false,
                                                            PathPreference(),
                                                            Ip4Address(0));
    client->WaitForIdle();
    DelVn("vn1");
    client->WaitForIdle();

    EXPECT_TRUE(VmPortInactive(1));

    agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq(peer, "vrf1",
                                                            addr, 32,
                                                            MakeUuid(1),
                                                            "Test", 10,
                                                            SecurityGroupList(),
                                                            false,
                                                            PathPreference(),
                                                            Ip4Address(0));
    client->WaitForIdle();

    Inet4UnicastAgentRouteTable::DeleteReq(peer, "vrf1", addr, 32, NULL);
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
        Agent::GetInstance()->fabric_inet4_unicast_table()->Register(
                                               boost::bind(&RouteTest::RtListener, 
                                               this, _1, _2));

    Inet4UnicastRouteEntry *rt;
    Inet4UnicastRouteEntry *rt_hold;
    AddResolveRoute(lpm3_ip_, 24);
    client->WaitForIdle();
    AddArp(lpm4_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();

    rt = Agent::GetInstance()->fabric_inet4_unicast_table()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm4_ip_, rt->addr());

    boost::scoped_ptr<TestRtState> state(new TestRtState());
    rt->SetState(Agent::GetInstance()->fabric_inet4_unicast_table(), id, state.get());
    rt_hold = rt;
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm4_ip_, 32);
    client->WaitForIdle();
    rt = Agent::GetInstance()->fabric_inet4_unicast_table()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm3_ip_, rt->addr());

    AddArp(lpm4_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    rt = Agent::GetInstance()->fabric_inet4_unicast_table()->FindLPM(lpm4_ip_);
    EXPECT_EQ(lpm4_ip_, rt->addr());
    EXPECT_EQ(rt, rt_hold);
    rt->ClearState(Agent::GetInstance()->fabric_inet4_unicast_table(), id);

    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm4_ip_, 32);
    client->WaitForIdle();
    DeleteRoute(Agent::GetInstance()->local_peer(), Agent::GetInstance()->fabric_vrf_name(), lpm3_ip_, 24);
    client->WaitForIdle();

    DelArp(lpm4_ip_.to_string().c_str(), "0d:0b:0c:0d:0e:0f", eth_name_.c_str());
    client->WaitForIdle();
    Agent::GetInstance()->fabric_inet4_unicast_table()->Unregister(id);
}

TEST_F(RouteTest, ScaleRouteAddDel_1) {
    uint32_t i = 0;
    for (i = 0; i < 1000; i++) {
        AddRemoteVmRoute(remote_vm_ip_, fabric_gw_ip_, 32, 
                         MplsTable::kStartLabel);
        Inet4UnicastAgentRouteTable::DeleteReq(NULL, "vrf1", remote_vm_ip_, 32, NULL);
    }
    client->WaitForIdle(5);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));
}

TEST_F(RouteTest, ScaleRouteAddDel_2) {
    uint32_t repeat = 1000;
    uint32_t i = 0;
    for (i = 0; i < repeat; i++) {
        AddRemoteVmRoute(remote_vm_ip_, fabric_gw_ip_, 32, 
                         MplsTable::kStartLabel);
        if (i != (repeat - 1)) {
            Inet4UnicastAgentRouteTable::DeleteReq
                (Agent::GetInstance()->local_peer(), "vrf1", remote_vm_ip_,
                 32, NULL);
        }
    }
    client->WaitForIdle(5);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));

    Inet4UnicastRouteEntry *rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    EXPECT_TRUE(rt->GetActiveNextHop()->IsDeleted() == false);

    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    TunnelNHKey key(Agent::GetInstance()->fabric_vrf_name(), 
                    Agent::GetInstance()->router_id(), fabric_gw_ip_, false,  
                    TunnelType::DefaultType());
    EXPECT_FALSE(FindNH(&key));
}

//Test scale add and delete of composite routes
TEST_F(RouteTest, ScaleRouteAddDel_3) {
    ComponentNHKeyList comp_nh_list;

    int remote_server_ip = 0x0A0A0A0A;
    int label = 16;
    int nh_count = 3;

    for(int i = 0; i < nh_count; i++) {
        ComponentNHKeyPtr comp_nh(new ComponentNHKey(label,
            Agent::GetInstance()->fabric_vrf_name(),
            Agent::GetInstance()->router_id(), Ip4Address(remote_server_ip++),
            false, TunnelType::AllType()));
        comp_nh_list.push_back(comp_nh);
        label++;
    }

    SecurityGroupList sg_id_list;
    for (uint32_t i = 0; i < 1000; i++) {    
        EcmpTunnelRouteAdd(NULL, vrf_name_, remote_vm_ip_, 32, 
                           comp_nh_list, false, "test", sg_id_list,
                           PathPreference());
        DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    }
    client->WaitForIdle(5);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    CompositeNHKey key(Composite::ECMP, true, comp_nh_list, vrf_name_);
    EXPECT_FALSE(FindNH(&key));
}

//Test scale add and delete of composite routes
TEST_F(RouteTest, ScaleRouteAddDel_4) {
    ComponentNHKeyList comp_nh_list;
    int remote_server_ip = 0x0A0A0A0A;
    int label = 16;
    int nh_count = 3;

    for(int i = 0; i < nh_count; i++) {
        ComponentNHKeyPtr comp_nh(new ComponentNHKey(label,
                Agent::GetInstance()->fabric_vrf_name(),
                Agent::GetInstance()->router_id(),
                Ip4Address(remote_server_ip++),
                false, TunnelType::AllType()));
        comp_nh_list.push_back(comp_nh);
        label++;
    }

    uint32_t repeat = 1000;
    SecurityGroupList sg_id_list;
    sg_id_list.push_back(1);
    for (uint32_t i = 0; i < repeat; i++) {    
        EcmpTunnelRouteAdd(NULL, vrf_name_, remote_vm_ip_, 32, 
                           comp_nh_list, -1, "test", sg_id_list,
                           PathPreference());
        if (i != (repeat - 1)) {
            DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
        }
    }
    client->WaitForIdle(5);
    EXPECT_TRUE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    Inet4UnicastRouteEntry *rt = RouteGet(vrf_name_, remote_vm_ip_, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(rt->GetActiveNextHop()->IsDeleted() == false);
    const SecurityGroupList &sg = rt->GetActivePath()->sg_list();
    EXPECT_TRUE(sg[0] == 1);

    DeleteRoute(NULL, vrf_name_, remote_vm_ip_, 32);
    EXPECT_FALSE(RouteFind(vrf_name_, remote_vm_ip_, 32));
    CompositeNHKey key(Composite::ECMP, true, comp_nh_list, vrf_name_);
    EXPECT_FALSE(FindNH(&key));
}

//Check path with highest preference gets priority
TEST_F(RouteTest, PathPreference) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:01:01:01", 3, 3},
        {"vnet4", 4, "1.1.1.1", "00:00:00:01:01:01", 3, 4},
    };

    CreateVmportEnv(input, 2);
    client->WaitForIdle();


    VmInterface *vnet3 = VmInterfaceGet(3);
    VmInterface *vnet4 = VmInterfaceGet(4);

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf3", ip, 32);

    //Enqueue traffic seen from vnet3 interface
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vnet4->id(), vnet4->vrf()->vrf_id());
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActivePath()->peer() == vnet4->peer());

    //Enqueue traffic seen from vnet3 interface
    Agent::GetInstance()->oper_db()->route_preference_module()->
        EnqueueTrafficSeen(ip, 32, vnet3->id(), vnet3->vrf()->vrf_id());
    client->WaitForIdle();
    //Check that path from vnet3 is preferred path
    EXPECT_TRUE(rt->GetActivePath()->peer() == vnet3->peer());

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
}

//If ecmp flag is removed from instance ip, verify that path gets removed from
//ecmp peer path
TEST_F(RouteTest, EcmpPathDelete) {
    client->Reset();
    struct PortInfo input[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:01:01:01", 3, 3},
        {"vnet4", 4, "1.1.1.1", "00:00:00:01:01:01", 3, 4},
    };

    CreateVmportWithEcmp(input, 2);
    client->WaitForIdle();

    VmInterface *vnet3 = VmInterfaceGet(3);
    VmInterface *vnet4 = VmInterfaceGet(4);

    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf3", ip, 32);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    //One of the interface becomes active path
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);

    CreateVmportWithEcmp(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    EXPECT_TRUE(RouteGet("vrf3", ip, 32) == NULL);
    //Make sure vrf and all routes are deleted
    EXPECT_TRUE(VrfFind("vrf3", true) == false);
}

TEST_F(RouteTest, Enqueue_uc_route_add_on_deleted_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VrfEntryRef vrf_ref = VrfGet(vrf_name_.c_str());
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    Inet4TunnelRouteAdd(NULL, vrf_name_, remote_vm_ip_, 32, server1_ip_,
                        TunnelType::AllType(), MplsTable::kStartLabel,
                        vrf_name_,
                        SecurityGroupList(), PathPreference());
    vrf_ref = NULL;
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

TEST_F(RouteTest, Enqueue_uc_route_del_on_deleted_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VrfEntryRef vrf_ref = VrfGet(vrf_name_.c_str());
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    Inet4UnicastAgentRouteTable::DeleteReq(NULL, vrf_name_, remote_vm_ip_, 32,
                                           NULL);
    vrf_ref = NULL;
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

TEST_F(RouteTest, Enqueue_mc_route_add_on_deleted_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VrfEntryRef vrf_ref = VrfGet(vrf_name_.c_str());
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    ComponentNHKeyList component_nh_key_list;
    Inet4MulticastAgentRouteTable::AddMulticastRoute(vrf_name_, "vn1",
                                   Ip4Address::from_string("0.0.0.0"),
                                   Ip4Address::from_string("255.255.255.255"),
                                   component_nh_key_list);

    vrf_ref = NULL;
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

TEST_F(RouteTest, Enqueue_mc_route_del_on_deleted_vrf) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    VrfEntryRef vrf_ref = VrfGet(vrf_name_.c_str());
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    TaskScheduler::GetInstance()->Stop();
    Inet4MulticastAgentRouteTable::DeleteMulticastRoute(vrf_name_,
                                   Ip4Address::from_string("0.0.0.0"),
                                   Ip4Address::from_string("255.255.255.255"));
    vrf_ref = NULL;
    TaskScheduler::GetInstance()->Start();
    client->WaitForIdle();
}

TEST_F(RouteTest, SubnetGwForRoute_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    AddIPAM("vn1", ipam_info, 1, NULL, "vdns1");
    client->WaitForIdle();

    //Check if the subnet gateway is set to 1.1.1.200 for a route
    Ip4Address vm_ip = Ip4Address::from_string("1.1.1.10");
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", vm_ip, 32);
    Ip4Address subnet_gw_ip = Ip4Address::from_string("1.1.1.200");
    EXPECT_TRUE(rt->GetActivePath()->subnet_gw_ip() == subnet_gw_ip);

    //Update ipam to have different gw address
    IpamInfo ipam_info2[] = {
        {"1.1.1.0", 24, "1.1.1.201", true},
    };
    AddIPAM("vn1", ipam_info2, 1, NULL, "vdns1");
    client->WaitForIdle();

    subnet_gw_ip = Ip4Address::from_string("1.1.1.201");
    EXPECT_TRUE(rt->GetActivePath()->subnet_gw_ip() == subnet_gw_ip);

    DelIPAM("vn1", "vdns1");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    //Make sure vrf and all routes are deleted
    EXPECT_TRUE(VrfFind("vrf1", true) == false);
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
