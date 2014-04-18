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
        local_vm_ip_ = Ip4Address::from_string("1.1.1.10");
        remote_vm_ip_ = Ip4Address::from_string("1.1.1.11");
        local_vm_mac_ = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        remote_vm_mac_ = (struct ether_addr *)malloc(sizeof(struct ether_addr));
        memcpy (local_vm_mac_, ether_aton("00:00:01:01:01:10"), 
                sizeof(struct ether_addr));
        memcpy (remote_vm_mac_, ether_aton("00:00:01:01:01:11"), 
                sizeof(struct ether_addr));
    };
    ~TunnelEncapTest() { };

    virtual void SetUp() {
        agent = Agent::GetInstance();
        IpamInfo ipam_info[] = {
            {"1.1.1.0", 24, "1.1.1.200"}
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
            GetDefaultInet4UnicastRouteTable()->AddResolveRoute(
                Agent::GetInstance()->GetDefaultVrf(), server_ip, plen);
        client->WaitForIdle();
    }

    void AddRemoteVmRoute(TunnelType::TypeBmap l3_bmap, 
                          TunnelType::TypeBmap l2_bmap) {
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->
            AddRemoteVmRouteReq(Agent::GetInstance()->local_peer(), 
                                vrf_name_, remote_vm_ip_, 32, server1_ip_,
                                l3_bmap, 1000, vrf_name_,
                                SecurityGroupList());
        client->WaitForIdle();

        Layer2AgentRouteTable::AddRemoteVmRouteReq(
            Agent::GetInstance()->local_peer(), vrf_name_,
            l2_bmap, server1_ip_, 2000, *remote_vm_mac_, remote_vm_ip_, 32);
        client->WaitForIdle();

        TunnelOlist olist_map;
        olist_map.push_back(OlistTunnelEntry(3000, 
                            IpAddress::from_string("8.8.8.8").to_v4(),
                            TunnelType::MplsType()));
        MulticastHandler::ModifyFabricMembers(vrf_name_,
                            IpAddress::from_string("1.1.1.255").to_v4(),
                            IpAddress::from_string("0.0.0.0").to_v4(),
                            1111, olist_map);
        MulticastHandler::ModifyFabricMembers(vrf_name_,
                            IpAddress::from_string("255.255.255.255").to_v4(),
                            IpAddress::from_string("0.0.0.0").to_v4(),
                            1112, olist_map);
        AddArp("8.8.8.8", "00:00:08:08:08:08", 
               Agent::GetInstance()->GetIpFabricItfName().c_str());
        client->WaitForIdle();
    }

    void DeleteRemoteVmRoute() {
        Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->
            DeleteReq(Agent::GetInstance()->local_peer(), vrf_name_,
                      remote_vm_ip_, 32);
        client->WaitForIdle();
        Layer2AgentRouteTable::DeleteReq(Agent::GetInstance()->local_peer(), 
                                         vrf_name_,
                                         *remote_vm_mac_);
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
        Inet4UnicastRouteEntry *route = RouteGet(vrf_name_, local_vm_ip_, 32);
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
    }

    void VerifyLayer2UnicastRoutes(TunnelType::Type type) {
        Layer2RouteEntry *route = L2RouteGet(vrf_name_, *local_vm_mac_);
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
        
        route = L2RouteGet(vrf_name_, *remote_vm_mac_);
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
        CompositeNHKey flood_fabric_key(vrf_name_, 
                           IpAddress::from_string("255.255.255.255").to_v4(), 
                           IpAddress::from_string("0.0.0.0").to_v4(), false,
                           Composite::FABRIC);
        const CompositeNH *flood_fabric_cnh = 
            static_cast<CompositeNH *>(Agent::GetInstance()->GetNextHopTable()->
                                       FindActiveEntry(&flood_fabric_key));
        ASSERT_TRUE(flood_fabric_cnh != NULL);
        const ComponentNH *component_nh = 
            static_cast<const ComponentNH *>(flood_fabric_cnh->
                                          GetComponentNHList()->Get(0));
        ASSERT_TRUE(flood_fabric_cnh->ComponentNHCount() == 1);
        const TunnelNH *tnh = 
            static_cast<const TunnelNH *>(component_nh->GetNH());
        ASSERT_TRUE(tnh->GetTunnelType().GetType() == type);

        CompositeNHKey subnet_fabric_key(vrf_name_, 
                           IpAddress::from_string("1.1.1.255").to_v4(), 
                           IpAddress::from_string("0.0.0.0").to_v4(), false,
                           Composite::FABRIC);
        const CompositeNH *subnet_fabric_cnh = 
            static_cast<const CompositeNH *>(Agent::GetInstance()->
                                             GetNextHopTable()->
                                       FindActiveEntry(&subnet_fabric_key));
        ASSERT_TRUE(subnet_fabric_cnh != NULL);
        ASSERT_TRUE(subnet_fabric_cnh->ComponentNHCount() == 1);
        component_nh = 
            static_cast<const ComponentNH *>(subnet_fabric_cnh->
                                             GetComponentNHList()->Get(0));
        tnh = static_cast<const TunnelNH *>(component_nh->GetNH());
        ASSERT_TRUE(tnh->GetTunnelType().GetType() == type);
    }

    Agent *agent;
    TunnelType::Type default_tunnel_type_;
    std::string vrf_name_;
    Ip4Address  local_vm_ip_;
    Ip4Address  remote_vm_ip_;
    Ip4Address  server1_ip_;
    struct ether_addr *local_vm_mac_;
    struct ether_addr *remote_vm_mac_;
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
    return ret;
}
