/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
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
#include "openstack/instance_service_server.h"
#include "test_cmn_util.h"
#include "kstate/test/test_kstate_util.h"
#include "vr_types.h"

#include <controller/controller_export.h>

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
};

class TunnelEncapTest : public ::testing::Test {
public:
    TunnelEncapTest() : default_tunnel_type_(TunnelType::MPLS_GRE) {
        vrf_name_ = "vrf1";
        server1_ip_ = Ip4Address::from_string("10.1.1.11");
        server2_ip_ = Ip4Address::from_string("10.1.1.12");
        local_vm_ip_ = Ip4Address::from_string("1.1.1.10");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        remote_ecmp_vm_ip_ = Ip4Address::from_string("1.1.1.12");
        local_vm_mac_ = MacAddress::FromString("00:00:01:01:01:10");
        remote_vm_mac_ = MacAddress::FromString("00:00:01:01:01:11");
    };
    ~TunnelEncapTest() {
    }

    virtual void SetUp() {
        agent = Agent::GetInstance();
        IpamInfo ipam_info[] = {
            {"1.1.1.0", 24, "1.1.1.200", true}
        };

        client->Reset();
        VxLanNetworkIdentifierMode(false);
        client->WaitForIdle();
        AddEncapList("MPLSoGRE", "MPLSoUDP", "VXLAN");
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();

        //Create a VRF
        VrfAddReq(vrf_name_.c_str());
        AddResolveRoute(server1_ip_, 24);
        client->WaitForIdle();
        CreateVmportEnv(input, 1);
        client->WaitForIdle();
        client->Reset();
    }

    virtual void TearDown() {
        client->Reset();
        DelIPAM("vn1");
        client->WaitForIdle();
        client->Reset();
        DeleteVmportEnv(input, 1, true);
        client->WaitForIdle();
        DelEncapList();
        client->WaitForIdle();

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
    }

    void AddResolveRoute(const Ip4Address &server_ip, uint32_t plen) {
        Agent::GetInstance()->
            fabric_inet4_unicast_table()->AddResolveRoute(
                Agent::GetInstance()->fabric_vrf_name(), server_ip, plen);
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(TunnelType::TypeBmap l3_bmap, 
                          TunnelType::TypeBmap l2_bmap) {

        Inet4TunnelRouteAdd(Agent::GetInstance()->local_peer(), 
                            vrf_name_, remote_vm_ip_, 32, server1_ip_,
                            l3_bmap, 1000, vrf_name_,
                            SecurityGroupList(), PathPreference());
        client->WaitForIdle();

        Layer2TunnelRouteAdd(Agent::GetInstance()->local_peer(), vrf_name_,
                             l2_bmap, server1_ip_, 2000, remote_vm_mac_, remote_vm_ip_, 32);
        client->WaitForIdle();

        //Add an ecmp route
        ComponentNHKeyPtr nh_data1(new ComponentNHKey(20, agent->fabric_vrf_name(),
                                                      agent->router_id(),
                                                      server1_ip_,
                                                      false,
                                                      TunnelType::DefaultType()));
        ComponentNHKeyPtr nh_data2(new ComponentNHKey(20, agent->fabric_vrf_name(),
                                                      agent->router_id(),
                                                      server2_ip_,
                                                      false,
                                                      TunnelType::DefaultType()));
        ComponentNHKeyList comp_nh_list1;
        comp_nh_list1.push_back(nh_data1);
        comp_nh_list1.push_back(nh_data2);

        SecurityGroupList sg_id_list;
        EcmpTunnelRouteAdd(agent->local_peer(), vrf_name_,
                           remote_ecmp_vm_ip_, 32,
                           comp_nh_list1, false, "vn1",
                           sg_id_list, PathPreference());
        client->WaitForIdle();

        TunnelOlist olist_map;
        olist_map.push_back(OlistTunnelEntry(3000, 
                            IpAddress::from_string("8.8.8.8").to_v4(),
                            TunnelType::MplsType()));
        MulticastHandler::ModifyFabricMembers(Agent::GetInstance()->
                                              multicast_tree_builder_peer(),
                            vrf_name_,
                            IpAddress::from_string("1.1.1.255").to_v4(),
                            IpAddress::from_string("0.0.0.0").to_v4(),
                            1111, olist_map);
        MulticastHandler::ModifyFabricMembers(Agent::GetInstance()->
                                              multicast_tree_builder_peer(),
                            vrf_name_,
                            IpAddress::from_string("255.255.255.255").to_v4(),
                            IpAddress::from_string("0.0.0.0").to_v4(),
                            1112, olist_map);
        AddArp("8.8.8.8", "00:00:08:08:08:08", 
               Agent::GetInstance()->fabric_interface_name().c_str());
        client->WaitForIdle();
    }

    void DeleteRemoteVmRoute() {
        Agent::GetInstance()->fabric_inet4_unicast_table()->
            DeleteReq(Agent::GetInstance()->local_peer(), vrf_name_,
                      remote_vm_ip_, 32, NULL);
        client->WaitForIdle();
        Layer2AgentRouteTable::DeleteReq(Agent::GetInstance()->local_peer(), 
                                         vrf_name_,
                                         remote_vm_mac_, 0, NULL);
        client->WaitForIdle();
        agent->fabric_inet4_unicast_table()->
            DeleteReq(agent->local_peer(), vrf_name_,
                      remote_ecmp_vm_ip_, 32, NULL);
        client->WaitForIdle();

        DelArp("8.8.8.8", "00:00:08:08:08:08", 
               Agent::GetInstance()->fabric_interface_name().c_str());
        client->WaitForIdle();
    }

    void VerifyLabel(AgentPath *path) {
        const NextHop *nh = path->nexthop(agent);
        const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
        TunnelType::Type type = tnh->GetTunnelType().GetType();
        switch (type) {
        case TunnelType::VXLAN: {
            ASSERT_TRUE(path->GetActiveLabel() == path->vxlan_id());
            break;
        }    
        case TunnelType::MPLS_GRE: 
        case TunnelType::MPLS_UDP: {
            ASSERT_TRUE(path->GetActiveLabel() == path->label());
            break;
        }    
        default: {
            break;
        }                     
        }
    }

    void VerifyInet4UnicastRoutes(TunnelType::Type type) {
        InetUnicastRouteEntry *route = RouteGet(vrf_name_, local_vm_ip_, 32);
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const NextHop *nh = path->nexthop(agent);
            if (nh->GetType() == NextHop::TUNNEL) {
                const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
                ASSERT_TRUE(type == tnh->GetTunnelType().GetType());
            }
        }
        
        route = RouteGet(vrf_name_, remote_vm_ip_, 32);
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const NextHop *nh = path->nexthop(agent);
            if (nh->GetType() == NextHop::TUNNEL) {
                const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
                ASSERT_TRUE(type == tnh->GetTunnelType().GetType());
            }
        }

        route = RouteGet(vrf_name_, remote_ecmp_vm_ip_, 32);
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const NextHop *nh = path->nexthop(agent);
            ASSERT_TRUE(nh->GetType() == NextHop::COMPOSITE);
            if (nh->GetType() == NextHop::COMPOSITE) {
                const CompositeNH *comp_nh =
                    static_cast<const CompositeNH *>(nh);
                for (ComponentNHList::const_iterator it =
                     comp_nh->begin(); it != comp_nh->end();
                     it++) {
                    const TunnelNH *tnh =
                        static_cast<const TunnelNH *>((*it)->nh());
                    ASSERT_TRUE(type == tnh->GetTunnelType().GetType());
                }
            }
        }
    }

    void VerifyLayer2UnicastRoutes(TunnelType::Type type) {
        Layer2RouteEntry *route = L2RouteGet(vrf_name_, local_vm_mac_);
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const NextHop *nh = path->nexthop(agent);
            if (nh->GetType() == NextHop::TUNNEL) {
                const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
                ASSERT_TRUE(type == tnh->GetTunnelType().GetType());
            }
        }  
        
        route = L2RouteGet(vrf_name_, remote_vm_mac_);
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const NextHop *nh = path->nexthop(agent);
            if (nh->GetType() == NextHop::TUNNEL) {
                const TunnelNH *tnh = static_cast<const TunnelNH *>(nh);
                ASSERT_TRUE(type == tnh->GetTunnelType().GetType());
            }
        } 
    }

    void VerifyMulticastRoutes(TunnelType::Type type) {
        Inet4MulticastRouteEntry *mc_rt = MCRouteGet("vrf1", "255.255.255.255");
        ASSERT_TRUE(mc_rt != NULL);
        const CompositeNH *flood_cnh =
            static_cast<const CompositeNH *>(mc_rt->GetActiveNextHop());
        ASSERT_TRUE(flood_cnh != NULL);
        ASSERT_TRUE(flood_cnh->ComponentNHCount() == 2);
        const CompositeNH *flood_fabric_cnh =
            dynamic_cast<const CompositeNH *>(flood_cnh->GetNH(0));
        const TunnelNH *tnh = dynamic_cast<const TunnelNH *>(
            flood_fabric_cnh->GetNH(0));
        ASSERT_TRUE(tnh->GetTunnelType().GetType() == type);

        Ip4Address subnet_broadcast = Ip4Address::from_string("1.1.1.255");
        InetUnicastRouteEntry *uc_rt =
            RouteGet("vrf1", subnet_broadcast, 32);
        const CompositeNH *subnet_cnh =
            static_cast<const CompositeNH *>(mc_rt->GetActiveNextHop());
        ASSERT_TRUE(subnet_cnh->ComponentNHCount() == 2);
        const CompositeNH *subnet_fabric_cnh =
            dynamic_cast<const CompositeNH *>(subnet_cnh->GetNH(0));
        tnh = dynamic_cast<const TunnelNH *>(subnet_fabric_cnh->GetNH(0));
        ASSERT_TRUE(tnh->GetTunnelType().GetType() == type);
    }

    Agent *agent;
    TunnelType::Type default_tunnel_type_;
    std::string vrf_name_;
    Ip4Address  local_vm_ip_;
    Ip4Address  remote_vm_ip_;
    Ip4Address  server1_ip_;
    Ip4Address  server2_ip_;
    Ip4Address  remote_ecmp_vm_ip_;
    MacAddress  local_vm_mac_;
    MacAddress  remote_vm_mac_;
    static TunnelType::Type type_;
};

TEST_F(TunnelEncapTest, dummy) {
    client->Reset();
    AddRemoteVmRoute(TunnelType::MplsType(), TunnelType::AllType());
    client->WaitForIdle();
    VerifyInet4UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyLayer2UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyMulticastRoutes(TunnelType::MPLS_GRE);
    DeleteRemoteVmRoute();
    client->WaitForIdle();
}

TEST_F(TunnelEncapTest, delete_encap_default_to_mpls_gre) {
    client->Reset();
    AddRemoteVmRoute(TunnelType::MplsType(), TunnelType::AllType());
    client->WaitForIdle();
    DelEncapList();
    client->WaitForIdle();
    VerifyInet4UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyLayer2UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyMulticastRoutes(TunnelType::MPLS_GRE);
    DeleteRemoteVmRoute();
    client->WaitForIdle();
}

TEST_F(TunnelEncapTest, gre_to_udp) {
    client->Reset();
    AddRemoteVmRoute(TunnelType::MplsType(), TunnelType::AllType());
    client->WaitForIdle();
    AddEncapList("MPLSoUDP", "MPLSoGRE", "VXLAN");
    client->WaitForIdle();
    VerifyInet4UnicastRoutes(TunnelType::MPLS_UDP);
    VerifyLayer2UnicastRoutes(TunnelType::MPLS_UDP);
    VerifyMulticastRoutes(TunnelType::MPLS_UDP);
    DelEncapList();
    client->WaitForIdle();
    VerifyInet4UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyLayer2UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyMulticastRoutes(TunnelType::MPLS_GRE);
    DeleteRemoteVmRoute();
    client->WaitForIdle();
}

TEST_F(TunnelEncapTest, gre_to_vxlan_udp) {
    client->Reset();
    AddRemoteVmRoute(TunnelType::MplsType(), TunnelType::AllType());
    client->WaitForIdle();
    AddEncapList("VXLAN", "MPLSoUDP", "MPLSoGRE");
    client->WaitForIdle();
    VerifyInet4UnicastRoutes(TunnelType::MPLS_UDP);
    VerifyLayer2UnicastRoutes(TunnelType::VXLAN);
    VerifyMulticastRoutes(TunnelType::MPLS_UDP);
    DelEncapList();
    client->WaitForIdle();
    VerifyInet4UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyLayer2UnicastRoutes(TunnelType::MPLS_GRE);
    VerifyMulticastRoutes(TunnelType::MPLS_GRE);
    DeleteRemoteVmRoute();
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
