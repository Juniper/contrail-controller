/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <io/test/event_manager_test.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
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
#include "vr_types.h"

#include "openstack/instance_service_server.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/test/xmpp_test_util.h"
#include "vr_types.h"
#include "control_node_mock.h"
#include "xml/xml_pugi.h"
#include "controller/controller_peer.h"
#include "controller/controller_export.h"
#include "controller/controller_vrf_export.h"

using namespace pugi;

EventManager evm1;
ServerThread *thread1;
test::ControlNodeMock *bgp_peer1;

EventManager evm2;
ServerThread *thread2;
test::ControlNodeMock *bgp_peer2;

void RouterIdDepInit(Agent *agent) {
    Agent::GetInstance()->controller()->Connect();
}

void StartControlNodeMock() {
    thread1 = new ServerThread(&evm1);
    bgp_peer1 = new test::ControlNodeMock(&evm1, "127.0.0.1");
    
    Agent::GetInstance()->SetXmppServer("127.0.0.1", 0);
    Agent::GetInstance()->SetXmppPort(bgp_peer1->GetServerPort(), 0);
    Agent::GetInstance()->SetDnsXmppServer("", 0);
    Agent::GetInstance()->SetDnsXmppPort(bgp_peer1->GetServerPort(), 0);

    thread2 = new ServerThread(&evm2);
    bgp_peer2 = new test::ControlNodeMock(&evm2, "127.0.0.1");
    
    Agent::GetInstance()->SetXmppServer("127.0.0.1", 1);
    Agent::GetInstance()->SetXmppPort(bgp_peer2->GetServerPort(), 1);
    Agent::GetInstance()->SetDnsXmppServer("", 1);
    Agent::GetInstance()->SetDnsXmppPort(bgp_peer2->GetServerPort(), 1);
    thread1->Start();
    thread2->Start();
}

void StopControlNodeMock() {
    Agent::GetInstance()->controller()->DisConnect();
    client->WaitForIdle();
    TcpServerManager::DeleteServer(Agent::GetInstance()->GetAgentXmppClient(0));
    TcpServerManager::DeleteServer(Agent::GetInstance()->GetAgentXmppClient(1));

    bgp_peer1->Shutdown();
    client->WaitForIdle();
    delete bgp_peer1;

    evm1.Shutdown();
    thread1->Join();
    delete thread1;

    bgp_peer2->Shutdown();
    client->WaitForIdle();
    delete bgp_peer2;

    evm2.Shutdown();
    thread2->Join();
    delete thread2;

    client->WaitForIdle();
}

class RouteTest : public ::testing::Test {
protected:
    RouteTest() {
    };

    virtual void SetUp() {
        agent = Agent::GetInstance();
        client->Reset();
    }

    virtual void TearDown() {
        //Clean up any pending routes in conrol node mock
        bgp_peer1->Clear();
        bgp_peer2->Clear();
    }

    Agent *agent;
};

//Create a port, route gets exported to BGP
//Ensure the active path in route is BGP injected path
//and both BGP path and local path are pointing to same NH
TEST_F(RouteTest, RouteTest_1) {
    struct PortInfo input[] = {
      {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };
    Ip4Address local_vm_ip = Ip4Address::from_string("1.1.1.10");

    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", local_vm_ip, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", local_vm_ip, 32);

    WAIT_FOR(100, 10000, rt->GetPathList().size() == 3);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);

    //Get Local path
    const NextHop *local_nh = 
        rt->FindPath(Agent::GetInstance()->local_vm_peer())->nexthop(agent);
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    EXPECT_TRUE(local_nh == bgp_nh);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", local_vm_ip, 32));
}

//Create a port, route gets exported to BGP
//Ensure the active path in route is BGP injected path,
//and both BGP path and local path are pointing to same NH
//Add acl on the port, and verify that both BGP path and 
//local vm path point to policy enabled NH
TEST_F(RouteTest, RouteTest_2) {
    struct PortInfo input[] = {
      {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    };
    Ip4Address local_vm_ip = Ip4Address::from_string("1.1.1.10");
 
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(RouteFind("vrf1", local_vm_ip, 32));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", local_vm_ip, 32);

    WAIT_FOR(100, 10000, rt->GetPathList().size() == 3);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);

    //Local path and BGP path would point to same NH
    const NextHop *local_nh = 
        rt->FindPath(Agent::GetInstance()->local_vm_peer())->nexthop(agent);
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    EXPECT_TRUE(local_nh == bgp_nh);

    //Attach policy to VN
    AddAcl("acl1", 1);
    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortPolicyEnabled(input, 0));
    local_nh = rt->FindPath(Agent::GetInstance()->local_vm_peer())->nexthop(agent);
    bgp_nh = rt->GetActiveNextHop();
    EXPECT_TRUE(local_nh->PolicyEnabled() == true);
    EXPECT_TRUE(bgp_nh->PolicyEnabled() == true);
    EXPECT_TRUE(local_nh == bgp_nh);

    //Detach policy and verify that both BGP and local path
    //point to non policy enabled NH
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortPolicyEnabled(input, 0));
    local_nh = rt->FindPath(Agent::GetInstance()->local_vm_peer())->nexthop(agent);
    bgp_nh = rt->GetActiveNextHop();
    EXPECT_FALSE(local_nh->PolicyEnabled() == true);
    EXPECT_FALSE(bgp_nh->PolicyEnabled() == true);
    EXPECT_TRUE(local_nh == bgp_nh);

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", local_vm_ip, 32));
}

//Verify ECMP route creation
TEST_F(RouteTest, BgpEcmpRouteTest_1) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Add couple of routes hosted at different servers
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");

    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");

    //Create VRF resulting in susbcribe to routing-instance
    AddVn("vn1", 1);
    AddVrf("vrf1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, RouteFind("vrf1", ip, 32));
    WAIT_FOR(100, 10000, PathCount("vrf1", ip, 32) == 2);

    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    //Expect route to point to composite NH
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    EXPECT_TRUE(bgp_nh->GetType() == NextHop::COMPOSITE);
   
    //Two servers have exported same ip route, expect composite NH to have
    //2 component NH 
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2); 

    //Verfiy that all members of component NH are correct 
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    EXPECT_TRUE((*component_nh_it)->label() == 16);
    const TunnelNH *tun_nh = 
        static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "10.10.10.10");

    component_nh_it++;
    EXPECT_TRUE((*component_nh_it)->label() == 17);
    tun_nh = static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "11.11.11.11");

    DelVrf("vrf1");
    DelVn("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
}

//Test transition of route from Tunnel NH to composite NH
TEST_F(RouteTest, BgpEcmpRouteTest_2) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Add route to both BGP peer
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");

    //Create VRF resulting in susbcribe to routing-instance
    AddVn("vn1", 1);
    AddVrf("vrf1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, RouteFind("vrf1", ip, 32));
    WAIT_FOR(100, 10000, PathCount("vrf1", ip, 32) == 2);

    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    //Expect route to point to composite NH
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    EXPECT_TRUE(bgp_nh->GetType() == NextHop::TUNNEL);
  
    //Add one more route, hosted on different server 
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    //Two servers have exported same ip route, expect composite NH to have
    //2 component NH 
    bgp_nh = rt->GetActiveNextHop();
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2); 

    //Verfiy that all members of component NH are correct 
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    EXPECT_TRUE((*component_nh_it)->label() == 16);
    const TunnelNH *tun_nh = 
        static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "10.10.10.10");

    component_nh_it++;
    EXPECT_TRUE((*component_nh_it)->label() == 17);
    tun_nh = static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "11.11.11.11");

    DelVrf("vrf1");
    DelVn("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
}

//Test transition from composite NH to tunnel NH,
//upon path deletes from servers
TEST_F(RouteTest, BgpEcmpRouteTest_3) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Add couple of routes hosted at different servers
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");

    //Create VRF resulting in susbcribe to routing-instance
    AddVn("vn1", 1);
    AddVrf("vrf1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, RouteFind("vrf1", ip, 32));
    WAIT_FOR(100, 10000, PathCount("vrf1", ip, 32) == 2);

    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    EXPECT_TRUE(bgp_nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2); 

    //Verfiy that all members of component NH are correct 
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    EXPECT_TRUE((*component_nh_it)->label() == 16);
    const TunnelNH *tun_nh = 
        static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "10.10.10.10");

    component_nh_it++;
    EXPECT_TRUE((*component_nh_it)->label() == 17);
    tun_nh = static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "11.11.11.11");

    //Delete one of the component route
    bgp_peer1->DeleteRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer2->DeleteRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    //Verify that route now points to tunnel NH
    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::TUNNEL);
    tun_nh = static_cast<const TunnelNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "11.11.11.11");
    EXPECT_TRUE(rt->GetMplsLabel() == 17);

    DelVrf("vrf1");
    DelVn("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
}

//Test ECMP route modification
//1.1.1.1 -> <server1, server2, server3>
//Delete a VM on one of the server and verify new route
//1.1.1.1 -> <server1, server3>
TEST_F(RouteTest, BgpEcmpRouteTest_4) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Add couple of routes hosted at different servers
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "12.12.12.12", 18, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "12.12.12.12", 18, "vn1");

    //Create VRF resulting in susbcribe to routing-instance
    AddVn("vn1", 1);
    AddVrf("vrf1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, RouteFind("vrf1", ip, 32));
    WAIT_FOR(100, 10000, PathCount("vrf1", ip, 32) == 2);

    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    EXPECT_TRUE(bgp_nh->GetType() == NextHop::COMPOSITE);
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3); 

    //Delete one of the server VM
    bgp_peer1->DeleteRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    bgp_peer2->DeleteRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    //Verfiy that all members of component NH are correct 
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    EXPECT_TRUE((*component_nh_it)->label() == 16);
    const TunnelNH *tun_nh = 
        static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "10.10.10.10");

    //Component NH at index 2 was deleted
    component_nh_it++;
    WAIT_FOR(100, 10000, *component_nh_it == NULL);

    component_nh_it++;
    EXPECT_TRUE((*component_nh_it)->label() == 18);
    tun_nh = static_cast<const TunnelNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE((*(tun_nh->GetDip())).to_string() == "12.12.12.12");

    DelVrf("vrf1");
    DelVn("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
}

//Create VM with same VIP, and verify BGP path
//and local vm path have same NH
TEST_F(RouteTest, EcmpRouteTest_1) {
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2},
        {"vnet3", 3, "1.1.1.1", "00:00:00:02:02:03", 1, 3},
    };
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
 
    CreateVmportEnv(input1, 3);
    client->WaitForIdle();

    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    WAIT_FOR(100, 10000, rt->GetPathList().size() == 3);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    const NextHop *bgp_nh = rt->GetActiveNextHop();
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    WAIT_FOR(100, 10000, comp_nh->ComponentNHCount() == 3);

    const NextHop *local_nh = rt->FindPath(Agent::GetInstance()->local_vm_peer())->nexthop(agent);
    EXPECT_TRUE(local_nh->GetType() == NextHop::COMPOSITE);
    comp_nh = static_cast<const CompositeNH *>(local_nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 3);

    DeleteVmportEnv(input1, 3, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
}


//Create VM with same VIP, and verify BGP path
//and local vm path have same NH
TEST_F(RouteTest, EcmpRouteTest_2) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Add couple of routes hosted at different servers
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 16, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 16, "vn1");

    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2},
    };
 
    CreateVmportEnv(input1, 2);
    client->WaitForIdle();
    //Check that route points to composite NH,
    //with 5 members
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    WAIT_FOR(100, 10000, rt->GetPathList().size() == 3);
    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    const NextHop *bgp_nh = rt->GetActiveNextHop();
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    WAIT_FOR(100, 10000, comp_nh->ComponentNHCount() == 4);

    const NextHop *local_nh = rt->FindPath(Agent::GetInstance()->local_vm_peer())->nexthop(agent);
    EXPECT_TRUE(local_nh->GetType() == NextHop::COMPOSITE);
    comp_nh = static_cast<const CompositeNH *>(local_nh);
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 2);

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
}

//Create VM with same FIP, and verify BGP path
//and local vm path have same NH
TEST_F(RouteTest, EcmpRouteTest_3) {
    Ip4Address ip = Ip4Address::from_string("2.2.2.2");
    //Add couple of routes hosted at different servers
    bgp_peer1->AddRoute("vrf2", "2.2.2.2/32", "10.10.10.10", 16, "vn2");
    bgp_peer1->AddRoute("vrf2", "2.2.2.2/32", "11.11.11.11", 16, "vn2");
    bgp_peer2->AddRoute("vrf2", "2.2.2.2/32", "10.10.10.10", 16, "vn2");
    bgp_peer2->AddRoute("vrf2", "2.2.2.2/32", "11.11.11.11", 16, "vn2");

    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:02:02:01", 1, 2},
    };
 
    CreateVmportEnv(input1, 2);
    client->WaitForIdle();

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    AddLink("virtual-network", "vn2", "routing-instance", "vrf2");
    client->WaitForIdle();
    //Create floating IP pool
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "2.2.2.2");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn2");

    //Associate vnet1 with floating IP
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    AddLink("virtual-machine-interface", "vnet2", "floating-ip", "fip1");
    client->WaitForIdle();

    //Check that route points to composite NH,
    //with 3 members
    WAIT_FOR(100, 10000, RouteFind("vrf2", ip, 32) == true);
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", ip, 32);
    EXPECT_TRUE(rt != NULL);
    WAIT_FOR(100, 10000, rt->GetPathList().size() == 3);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);

    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    WAIT_FOR(100, 10000, comp_nh->ComponentNHCount() == 4);

    const NextHop *local_nh = rt->FindPath(Agent::GetInstance()->local_vm_peer())->nexthop(agent);
    EXPECT_TRUE(local_nh->GetType() == NextHop::COMPOSITE);
    EXPECT_TRUE(bgp_nh != local_nh);

    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    DelVrf("vrf2");
    DelVn("vn2");
    client->WaitForIdle();
}

//Leak ECMP route from one vrf to another VRF
TEST_F(RouteTest, EcmpRouteTest_4) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2},
    };

    CreateVmportEnv(input1, 2);
    client->WaitForIdle();

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    client->WaitForIdle();

    //Leak route to vrf2
    bgp_peer1->AddRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    bgp_peer2->AddRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    client->WaitForIdle();

    WAIT_FOR(100, 10000, (RouteGet("vrf2", ip, 32) != NULL));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", ip, 32);
    EXPECT_TRUE(rt != NULL);
    WAIT_FOR(100, 10000, rt->GetPathList().size() == 2);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    const NextHop *bgp_nh = rt->GetActiveNextHop();
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    WAIT_FOR(100, 10000, comp_nh->ComponentNHCount() == 2);

    DeleteVmportEnv(input1, 2, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 0);
 
    //Delete route leaked to vrf2
    bgp_peer1->DeleteRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    bgp_peer2->DeleteRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, RouteFind("vrf2", ip, 32) == false);
    DelVrf("vrf2");
    DelVn("vn2");
}
 
TEST_F(RouteTest, EcmpRouteTest_5) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Create mutliple VM interface with same IP
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.1", "00:00:00:02:02:01", 1, 2},
    };

    CreateVmportEnv(input1, 2);
    client->WaitForIdle();

    AddVn("vn2", 2);
    AddVrf("vrf2", 2);
    client->WaitForIdle();

    //Leak route to vrf2
    bgp_peer1->AddRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    bgp_peer2->AddRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    client->WaitForIdle();

    WAIT_FOR(100, 10000, (RouteGet("vrf2", ip, 32) != NULL));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf2", ip, 32);
    EXPECT_TRUE(rt != NULL);
    WAIT_FOR(100, 10000, rt->GetPathList().size() == 2);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    const NextHop *bgp_nh = rt->GetActiveNextHop();
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);
    WAIT_FOR(100, 10000, comp_nh->ComponentNHCount() == 2);

    //Add one more VM with same VIP
    struct PortInfo input2[] = {
        {"vnet3", 3, "1.1.1.1", "00:00:00:01:01:01", 1, 3},
    };
    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, comp_nh->ComponentNHCount() == 3);

    DeleteVmportEnv(input1, 2, false);
    client->WaitForIdle();
    EXPECT_TRUE(comp_nh->ComponentNHCount() == 0);
    DeleteVmportEnv(input2, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(RouteFind("vrf1", ip, 32));
 
    //Delete route leaked to vrf2
    bgp_peer1->DeleteRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    bgp_peer2->DeleteRoute("vrf2", "1.1.1.1/32", 
            Agent::GetInstance()->GetRouterId().to_string().c_str(), 18, "vn1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, RouteFind("vrf2", ip, 32) == false);
    DelVrf("vrf2");
    DelVn("vn2");
}
 
//Test to ensure composite NH, only has non policy enabled
//interface nexthopss 
TEST_F(RouteTest, EcmpRouteTest_7) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.1");
    //Create VM interface with polcy
    struct PortInfo input1[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    CreateVmportEnv(input1, 1, 1);
    client->WaitForIdle();

    //Add couple of routes hosted at different servers
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer1->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");

    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "10.10.10.10", 16, "vn1");
    bgp_peer2->AddRoute("vrf1", "1.1.1.1/32", "11.11.11.11", 17, "vn1");
    client->WaitForIdle();

    WAIT_FOR(100, 10000, (RouteGet("vrf1", ip, 32) != NULL));
    Inet4UnicastRouteEntry *rt = RouteGet("vrf1", ip, 32);
    EXPECT_TRUE(rt != NULL);
    WAIT_FOR(100, 10000, rt->GetPathList().size() == 3);
    EXPECT_TRUE(rt->GetActivePath()->peer()->GetType() == Peer::BGP_PEER);
    WAIT_FOR(100, 10000, rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE);

    //Expect route to point to composite NH
    const NextHop *bgp_nh = rt->GetActiveNextHop();
    const CompositeNH *comp_nh = static_cast<const CompositeNH *>(bgp_nh);

    //Verfiy that all members of component NH are correct 
    CompositeNH::ComponentNHList::const_iterator component_nh_it =
        comp_nh->begin();
    EXPECT_TRUE((*component_nh_it)->GetNH()->GetType() == NextHop::INTERFACE);
    const  InterfaceNH *intf_nh =
        static_cast<const InterfaceNH *>((*component_nh_it)->GetNH());
    EXPECT_TRUE(intf_nh->PolicyEnabled() == false);
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, false);

    StartControlNodeMock();
    RouterIdDepInit(Agent::GetInstance());
    int ret = RUN_ALL_TESTS();
    StopControlNodeMock();

    return ret;
}
