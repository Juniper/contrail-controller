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
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface_common.h>
#include <oper/tunnel_nh.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

using namespace std;
using namespace boost::assign;

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.200", true},
};

void RouterIdDepInit(Agent *agent) {
}

static void ValidateSandeshResponse(Sandesh *sandesh, vector<int> &result) {
    //TBD
    //Validate the response by the expectation
}

class TsnElectorTest : public ::testing::Test {
public:
    TsnElectorTest() : agent_(Agent::GetInstance()) {
    }

    virtual void SetUp() {
        bgp_peer_ = CreateBgpPeer("127.0.0.1", "remote");
        client->WaitForIdle();
    }
    virtual void TearDown() {
        DeleteBgpPeer(bgp_peer_);
        client->WaitForIdle();
    }
    Agent *agent_;
    BgpPeer *bgp_peer_;
};

bool IsEvpnCompositePresent(const std::string vrf_name, const Agent *agent) {
    BridgeRouteEntry *mc_route =
        L2RouteGet(vrf_name, MacAddress::FromString("ff:ff:ff:ff:ff:ff"));
    const NextHop *nh = mc_route->GetActiveNextHop();
    const CompositeNH *cnh1 = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(cnh1->composite_nh_type() == Composite::L2COMP);
    for (ComponentNHList::const_iterator it = cnh1->component_nh_list().begin();
         it != cnh1->component_nh_list().end(); it++) {
        const CompositeNH *cnh2 = static_cast<const CompositeNH *>((*it)->nh());
        if (cnh2->composite_nh_type() == Composite::EVPN) {
            return true;
        }
    }
    return false;
}

bool AreEvpnComponentPresent(const std::string vrf_name,
                             std::vector<std::string> vtep,
                             const Agent *agent) {
    BridgeRouteEntry *mc_route =
        L2RouteGet(vrf_name, MacAddress::FromString("ff:ff:ff:ff:ff:ff"));
    const NextHop *nh = mc_route->GetActiveNextHop();
    const CompositeNH *cnh1 = static_cast<const CompositeNH *>(nh);
    EXPECT_TRUE(cnh1->composite_nh_type() == Composite::L2COMP);
    for (ComponentNHList::const_iterator it = cnh1->component_nh_list().begin();
         it != cnh1->component_nh_list().end(); it++) {
        const CompositeNH *cnh2 = static_cast<const CompositeNH *>((*it)->nh());
        if (cnh2->composite_nh_type() != Composite::EVPN) {
            continue;
        }
        ComponentNHList::const_iterator it2 = cnh2->begin();
        while (it2 != cnh2->end()) {
            const ComponentNH *comp_nh = (*it2).get();
            const TunnelNH *tunnel = dynamic_cast<const TunnelNH *>
                (comp_nh->nh());
            if (!tunnel)
                return false;
            std::vector<std::string>::iterator vtep_it =
                std::find(vtep.begin(), vtep.end(),
                          tunnel->GetDip()->to_string());
            if (vtep_it == vtep.end())
                return false;
            vtep.erase(vtep_it);
            it2++;
        }
    }
    if (vtep.empty())
        return true;
    return false;
}

void FillOlist(Agent *agent, std::string ip, TunnelOlist &olist) {
    olist.push_back(OlistTunnelEntry(boost::uuids::nil_uuid(), 10,
                                     IpAddress::from_string(ip).to_v4(),
                                     TunnelType::VxlanType()));
}

void AddEvpnList(Agent *agent, TunnelOlist &olist, BgpPeer *bgp_peer) {
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent->
                                                                   oper_db()->multicast());
    //Send explicit evpn olist
    mc_handler->ModifyEvpnMembers(bgp_peer,
                                 "vrf1",
                                 olist,
                                 0,
                                 1);
}

void DelEvpnList(Agent *agent, BgpPeer *bgp_peer) {
    MulticastHandler *mc_handler = static_cast<MulticastHandler *>(agent->
                                                                   oper_db()->multicast());
    TunnelOlist olist;
    //Send explicit evpn olist
    mc_handler->ModifyEvpnMembers(bgp_peer,
                                 "vrf1",
                                 olist,
                                 0,
                                 ControllerPeerPath::kInvalidPeerIdentifier);
}

TEST_F(TsnElectorTest, Test_1) {
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    VrfEntry *fabric_vrf = agent_->fabric_policy_vrf();
    InetUnicastAgentRouteTable *inet4_table = fabric_vrf->
        GetInet4UnicastRouteTable();
    InetUnicastRouteEntry *vhost_rt = inet4_table->FindRoute(agent_->
                                      params()->vhost_addr());
    EXPECT_TRUE(vhost_rt != NULL);
    EXPECT_TRUE(vhost_rt->GetActivePath()->inactive() == false);

    InetUnicastRouteEntry *vm_rt = RouteGet("vrf1",
        Ip4Address::from_string("1.1.1.10"), 32);
    EXPECT_TRUE(vm_rt != NULL);
    EXPECT_TRUE(vm_rt->GetActivePath()->inactive());
    InetUnicastRouteEntry *subnet_rt = RouteGet("vrf1",
        Ip4Address::from_string("1.1.1.200"), 32);
    EXPECT_TRUE(subnet_rt != NULL);
    EXPECT_TRUE(subnet_rt->GetActivePath()->inactive() == false);

    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));
    //Add evpn
    TunnelOlist olist;
    FillOlist(agent_, "11.1.2.1", olist);
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.0.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    olist.clear();
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.2.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    olist.clear();
    FillOlist(agent_, "11.1.0.1", olist);
    FillOlist(agent_, "11.1.2.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    DelEvpnList(agent_, bgp_peer_);
    client->WaitForIdle();
    //Delete
    DeleteVmportEnv(input, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
}

TEST_F(TsnElectorTest, Test_2) {
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));

    Inet4TunnelRouteAdd(bgp_peer_, agent_->fabric_policy_vrf_name(),
                        IpAddress::from_string("10.1.0.1").to_v4(), 32,
                        IpAddress::from_string("8.8.8.8").to_v4(),
                        TunnelType::MplsType(), 101,
                        agent_->fabric_policy_vrf_name(),
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    //Add evpn
    TunnelOlist olist;
    FillOlist(agent_, "11.1.2.1", olist);
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.0.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));

    DeleteRoute(agent_->fabric_policy_vrf_name().c_str(), "10.1.0.1", 32,
                bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    DelEvpnList(agent_, bgp_peer_);
    client->WaitForIdle();
    //Delete
    DeleteVmportEnv(input, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
}

TEST_F(TsnElectorTest, Test_3) {
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));

    //Add evpn
    TunnelOlist olist;
    FillOlist(agent_, "11.1.2.1", olist);
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.0.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    Inet4TunnelRouteAdd(bgp_peer_, agent_->fabric_policy_vrf_name(),
                        IpAddress::from_string("10.1.0.1").to_v4(), 32,
                        IpAddress::from_string("8.8.8.8").to_v4(),
                        TunnelType::MplsType(), 101,
                        agent_->fabric_policy_vrf_name(),
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));

    DeleteRoute(agent_->fabric_policy_vrf_name().c_str(), "10.1.0.1", 32,
                bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    DelEvpnList(agent_, bgp_peer_);
    client->WaitForIdle();
    //Delete
    DeleteVmportEnv(input, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
}

TEST_F(TsnElectorTest, Test_4) {
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));

    Inet4TunnelRouteAdd(bgp_peer_, agent_->fabric_policy_vrf_name(),
                        IpAddress::from_string("10.1.2.1").to_v4(), 32,
                        IpAddress::from_string("8.8.8.8").to_v4(),
                        TunnelType::MplsType(), 101,
                        agent_->fabric_policy_vrf_name(),
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    //Add evpn
    TunnelOlist olist;
    FillOlist(agent_, "11.1.2.1", olist);
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.0.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    DeleteRoute(agent_->fabric_policy_vrf_name().c_str(), "10.1.2.1", 32,
                bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    DelEvpnList(agent_, bgp_peer_);
    client->WaitForIdle();
    //Delete
    DeleteVmportEnv(input, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
}

TEST_F(TsnElectorTest, Test_5) {
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));

    //Add evpn
    TunnelOlist olist;
    FillOlist(agent_, "11.1.2.1", olist);
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.0.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    Inet4TunnelRouteAdd(bgp_peer_, agent_->fabric_policy_vrf_name(),
                        IpAddress::from_string("10.1.2.1").to_v4(), 32,
                        IpAddress::from_string("8.8.8.8").to_v4(),
                        TunnelType::MplsType(), 101,
                        agent_->fabric_policy_vrf_name(),
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    DeleteRoute(agent_->fabric_policy_vrf_name().c_str(), "10.1.2.1", 32,
                bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));

    DelEvpnList(agent_, bgp_peer_);
    client->WaitForIdle();
    //Delete
    DeleteVmportEnv(input, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
}

TEST_F(TsnElectorTest, Test_6) {
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    VrfEntry *fabric_vrf = agent_->fabric_policy_vrf();
    InetUnicastAgentRouteTable *inet4_table = fabric_vrf->
        GetInet4UnicastRouteTable();
    InetUnicastRouteEntry *vhost_rt = inet4_table->FindRoute(agent_->
                                      params()->vhost_addr());
    EXPECT_TRUE(vhost_rt != NULL);
    EXPECT_TRUE(vhost_rt->GetActivePath()->inactive() == false);

    InetUnicastRouteEntry *vm_rt = RouteGet("vrf1",
        Ip4Address::from_string("1.1.1.10"), 32);
    EXPECT_TRUE(vm_rt != NULL);
    EXPECT_TRUE(vm_rt->GetActivePath()->inactive());
    InetUnicastRouteEntry *subnet_rt = RouteGet("vrf1",
        Ip4Address::from_string("1.1.1.200"), 32);
    EXPECT_TRUE(subnet_rt != NULL);
    EXPECT_TRUE(subnet_rt->GetActivePath()->inactive() == false);

    EXPECT_FALSE(IsEvpnCompositePresent("vrf1", agent_));
    //Add physical locator
    AddPhysicalDeviceWithIp(1, "tor-1", "", "11.1.0.1", "11.1.0.100", "",
                            agent_);
    client->WaitForIdle();
    //Add evpn
    TunnelOlist olist;
    FillOlist(agent_, "11.1.2.1", olist);
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.0.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));
    std::vector<std::string> vtep_list;
    vtep_list.push_back("11.1.0.1");
    EXPECT_TRUE(AreEvpnComponentPresent("vrf1", vtep_list, agent_));

    //add another physical locator
    AddPhysicalDeviceWithIp(2, "tor-2", "", "11.1.1.1", "11.1.1.100", "",
                            agent_);
    client->WaitForIdle();
    vtep_list.push_back("11.1.1.1");
    EXPECT_TRUE(AreEvpnComponentPresent("vrf1", vtep_list, agent_));

    olist.clear();
    FillOlist(agent_, "11.1.1.1", olist);
    FillOlist(agent_, "11.1.2.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));
    vtep_list.clear();
    vtep_list.push_back("11.1.1.1");
    EXPECT_TRUE(AreEvpnComponentPresent("vrf1", vtep_list, agent_));

    DelPhysicalDeviceWithIp(agent_, 1);
    olist.clear();
    FillOlist(agent_, "11.1.0.1", olist);
    FillOlist(agent_, "11.1.2.1", olist);
    AddEvpnList(agent_, olist, bgp_peer_);
    client->WaitForIdle();
    EXPECT_TRUE(IsEvpnCompositePresent("vrf1", agent_));
    vtep_list.clear();
    EXPECT_TRUE(AreEvpnComponentPresent("vrf1", vtep_list, agent_));

    DelPhysicalDeviceWithIp(agent_, 2);
    client->WaitForIdle();
    DelEvpnList(agent_, bgp_peer_);
    client->WaitForIdle();
    //Delete
    DeleteVmportEnv(input, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
}

//Refer: https://bugs.launchpad.net/juniperopenstack/+bug/1724064
//Routes in fabric vrf should remain active in tsn-no-forwarding-mode
TEST_F(TsnElectorTest, Test_7) {
    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    VrfEntry *fabric_vrf = agent_->fabric_vrf();
    InetUnicastAgentRouteTable *inet4_table = fabric_vrf->
        GetInet4UnicastRouteTable();
    InetUnicastRouteEntry *default_rt =
        inet4_table->FindRoute(Ip4Address::from_string("0.0.0.0"));
    EXPECT_FALSE(default_rt->GetActivePath()->inactive());

    //Delete
    DeleteVmportEnv(input, 1, true);
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortActive(input, 0));
}

int main(int argc, char **argv) {
    GETUSERARGS();
    strcpy(init_file, DEFAULT_VNSW_TSN_NO_FORWARDING_CONFIG_FILE);
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
