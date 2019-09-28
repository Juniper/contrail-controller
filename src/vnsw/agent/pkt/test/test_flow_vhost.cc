/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/pkt_handler.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

VmInterface *vnet[16];
const VmInterface *vhost;
char vhost_addr[32];
char vnet_addr[16][32];

PhysicalInterface *eth;
int hash_id;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:01", 1, 1},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.1", true},
};

IpamInfo ipam_info_fabric[] = {
    {"10.1.1.0", 24, "10.1.1.254", true},
};

#define DEFAULT_VN "default-domain:default-project:ip-fabric"
#define DEFAULT_POLICY_VRF "default-domain:default-project:ip-fabric:ip-fabric"
class VhostVmi : public ::testing::Test {
public:
    void AddVhostVmi(bool disable_policy=false) {
        DelNode("virtual-machine-interface", "vhost0");
        DelLink("virtual-machine-interface", "vhost0",
                "virtual-network", DEFAULT_VN);
        client->WaitForIdle();
        agent_ = Agent::GetInstance();
        vhost = static_cast<const VmInterface *>(agent_->vhost_interface());

        std::stringstream str;
        str << "<display-name>" << "vhost0" << "</display-name>";
        str << "<virtual-machine-interface-disable-policy>";
        if (disable_policy) {
            str << "true";
        } else {
            str << "false";
        }
        str << "</virtual-machine-interface-disable-policy>";

        AddNode("virtual-machine-interface", "vhost0", 10, str.str().c_str());
        AddLink("virtual-machine-interface", "vhost0",
                "virtual-network", DEFAULT_VN);
    }

    virtual void SetUp() {
        CreateVmportEnv(input, 1);
        AddIPAM("vn1", &ipam_info[0], 1);
        AddVn(DEFAULT_VN, 2);
        AddIPAM(DEFAULT_VN, &ipam_info_fabric[0], 1);
        vnet0_ = EthInterfaceGet("vnet0");
        AddVhostVmi();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        client->WaitForIdle();

        peer = CreateBgpPeer("127.0.0.1", "remote");
        client->WaitForIdle();

        AddAcl("Acl", 1, DEFAULT_VN, "vn1", "pass");
        AddLink("virtual-network", DEFAULT_VN, "access-control-list", "Acl");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelVn(DEFAULT_VN);
        DelNode("virtual-machine-interface", "vhost0");
        DelLink("virtual-machine-interface", "vhost0",
                "virtual-network", DEFAULT_VN);
        DelAcl("Acl");
        DelLink("virtual-network", DEFAULT_VN, "access-control-list", "Acl");
        client->WaitForIdle();

        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        client->WaitForIdle();
        DelIPAM("vn1");
        DeleteVmportEnv(input, 1, true);
        DeleteBgpPeer(peer);
        client->WaitForIdle();
    }

    Agent *agent_;
    FlowProto *flow_proto_;
    BgpPeer *peer;
    PhysicalInterface *vnet0_;
};

TEST_F(VhostVmi, VhostToRemoteVhost) {
    //Add a route to destination Vhost
    AddArp("10.1.1.10", "00:00:01:01:01:01",
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "10.1.1.10", 1, 1,
                false, 1, 0);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "10.1.1.10", 6, 1, 1);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->IsShortFlow() == false);
}

TEST_F(VhostVmi, RemoteVhostToVhost) {
    //Add a route to destination Vhost
    AddArp("10.1.1.10", "00:00:01:01:01:01",
           agent_->fabric_interface_name().c_str());
    client->WaitForIdle();

    TxIpPacket(vnet0_->id(), "10.1.1.10", "10.1.1.1", 2);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, "10.1.1.10", "10.1.1.1", 2, 0, 0,
                            vnet0_->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->IsShortFlow() == false);
}

//Flow setup before resolving the destination
TEST_F(VhostVmi, RemoteVhostToVhostBeforeResolve) {
    TxIpPacket(vnet0_->id(), "10.1.1.10", "10.1.1.1", 2);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, "10.1.1.10", "10.1.1.1", 2, 0, 0,
                            vnet0_->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->IsShortFlow() == false);
}

TEST_F(VhostVmi, RemoteVhostToVhostWithAcl) {
    Ip4Address addr = Ip4Address::from_string("10.1.1.10");
    Ip4Address gw = Ip4Address::from_string("10.1.1.10");
    Inet4TunnelRouteAdd(peer, DEFAULT_POLICY_VRF, addr, 32, gw,
                        TunnelType::AllType() | TunnelType::NativeType(),
                        8, "vn1", SecurityGroupList(),
                        TagList(), PathPreference());
    client->WaitForIdle();

    TxTcpPacket(vnet0_->id(), "10.1.1.10", "10.1.1.1", 10, 20,
                false, 1, 0);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, "10.1.1.10", "10.1.1.1", 6, 10, 20,
                            vnet0_->flow_key_nh()->id());
    WAIT_FOR(1000, 10000,
             fe->match_p().action_info.action == (1 << TrafficAction::PASS));

    DeleteRoute(DEFAULT_POLICY_VRF, "10.1.1.10", 32, peer);
    client->WaitForIdle();
}

TEST_F(VhostVmi, RemoteVhostToVhostWithDenyAcl) {
    Ip4Address addr = Ip4Address::from_string("10.1.1.10");
    Ip4Address gw = Ip4Address::from_string("10.1.1.10");
    Inet4TunnelRouteAdd(peer, DEFAULT_POLICY_VRF, addr, 32, gw,
                        TunnelType::AllType() | TunnelType::NativeType(),
                        8, "vn1", SecurityGroupList(),
                        TagList(), PathPreference());
    client->WaitForIdle();

    TxTcpPacket(vnet0_->id(), "10.1.1.10", "10.1.1.1", 10000, 20,
                false, 1, 0);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, "10.1.1.10", "10.1.1.1", 6, 10000, 20,
                            vnet0_->flow_key_nh()->id());
    WAIT_FOR(1000, 10000,
             fe->match_p().action_info.action ==
             (1 << TrafficAction::IMPLICIT_DENY | 1 << TrafficAction::DENY));

    DeleteRoute(DEFAULT_POLICY_VRF, "10.1.1.10", 32, peer);
    client->WaitForIdle();

    EXPECT_TRUE((fe->data().match_p.action_info.action &
                (1 << TrafficAction::IMPLICIT_DENY)) != 0);
}

TEST_F(VhostVmi, OverlayFromVhost) {
    Ip4Address addr = Ip4Address::from_string("10.1.1.10");
    Ip4Address gw = Ip4Address::from_string("10.1.1.10");
    Inet4TunnelRouteAdd(peer, DEFAULT_POLICY_VRF, addr, 32, gw,
            TunnelType::AllType(), 8, "vn1",
            SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    TxTcpPacket(vhost->id(), "10.1.1.1", "10.1.1.10", 10, 20,
                false, 1, 0);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, "10.1.1.1", "10.1.1.10", 6, 10, 20,
                            vhost->flow_key_nh()->id());
    EXPECT_TRUE(fe->data().dest_vrf == vhost->vrf()->vrf_id());
    EXPECT_TRUE(fe->IsShortFlow() == false);
    client->WaitForIdle();

    DeleteRoute(DEFAULT_POLICY_VRF, "10.1.1.10", 32, peer);
    client->WaitForIdle();
}

TEST_F(VhostVmi, OverlayToVhost) {
    Ip4Address addr = Ip4Address::from_string("10.1.1.10");
    Ip4Address gw = Ip4Address::from_string("10.1.1.10");
    Inet4TunnelRouteAdd(peer, DEFAULT_POLICY_VRF, addr, 32, gw,
            TunnelType::AllType(), 8, "vn1",
            SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();

    TxIpMplsPacket(vnet0_->id(), "10.1.1.10", "10.1.1.1", vhost->label(),
                   "10.1.1.10", "10.1.1.1", 1, 10);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, "10.1.1.1", "10.1.1.10", 1, 0, 0,
                            vhost->flow_key_nh()->id());
    EXPECT_TRUE(fe->data().dest_vrf == vhost->vrf()->vrf_id());
    EXPECT_TRUE(fe->IsShortFlow() == false);
    client->WaitForIdle();

    DeleteRoute(DEFAULT_POLICY_VRF, "10.1.1.10", 32, peer);
    client->WaitForIdle();
}

#if 0
TEST_F(VhostVmi, VmiToLocalVhost) {
    AddLink("virtual-network", "vn1", "virtual-network", DEFAULT_VN);
    client->WaitForIdle();

    Ip4Address sip = Ip4Address::from_string("10.1.1.1");
    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, boost::uuids::nil_uuid(), "vhost0");

    InetUnicastAgentRouteTable *table =
        static_cast<InetUnicastAgentRouteTable *>(
                agent_->fabric_vrf()->GetInet4UnicastRouteTable());
    table->AddVHostRecvRoute(peer, "vrf1", vmi_key, sip,
                             32, agent_->fabric_vn_name(),
                             false);
    client->WaitForIdle();

    const Interface *vm_intf = VmPortGet(1);

    TxTcpPacket(vm_intf->id(), "1.1.1.10", "10.1.1.1", 10, 20,
                false, 1, 0);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(0, "1.1.1.10", "10.1.1.1", IPPROTO_TCP, 10, 20,
                            vm_intf->flow_key_nh()->id());
    EXPECT_TRUE(fe->data().dest_vrf == 0);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "virtual-network", DEFAULT_VN);
    client->WaitForIdle();
    //Packet VRF doesnt change hence VRF change also doenst happen
    //EXPECT_TRUE(fe->data().dest_vrf == vm_intf->vrf()->vrf_id());

    DeleteRoute("vrf1", "10.1.1.1", 32, peer);
    client->WaitForIdle();
}
#endif

TEST_F(VhostVmi, FabricFlow_DisablePolicy_False) {
    agent_->set_controller_ifmap_xmpp_server("127.0.0.1", 0);
    agent_->set_controller_ifmap_xmpp_port(5269, 0);

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "127.0.0.1",
               1000, 5269, false, 1, 0);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "127.0.0.1", 6, 1000, 5269);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    agent_->set_dns_server("127.0.0.1", 0);
    agent_->set_dns_server_port(53, 0);

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "127.0.0.1",
               1000, 53, false, 1, 0);
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "127.0.0.1", 6, 1000, 53);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "127.0.0.1",
               1000, 8086, false, 1, 0);
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "127.0.0.1", 6, 1000, 8086);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    std::string nova_api("128.0.0.1");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back(nova_api);
    TestLinkLocalService service = {
        "metadata", "169.254.169.254", 80, "", fabric_ip_list, 8775
    };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "128.0.0.1",
                1000, 8775, false, 1, 0);
    client->WaitForIdle();

    DelLinkLocalConfig();
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                 "10.1.1.1", "128.0.0.1", 6, 1000, 8775);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "10.1.1.10",
                22, 10000, false, 1, 0);
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "10.1.1.10", 6, 22, 10000);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::FabricControlFlow));
}

TEST_F(VhostVmi, FabricFlow_DisablePolicy_True) {
    AddVhostVmi(true);
    agent_->set_controller_ifmap_xmpp_server("127.0.0.1", 0);
    agent_->set_controller_ifmap_xmpp_port(5269, 0);

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "127.0.0.1",
               1000, 5269, false, 1, 0);
    client->WaitForIdle();

    FlowEntry *fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "127.0.0.1", 6, 1000, 5269);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    agent_->set_dns_server("127.0.0.1", 0);
    agent_->set_dns_server_port(53, 0);

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "127.0.0.1",
               1000, 53, false, 1, 0);
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "127.0.0.1", 6, 1000, 53);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "127.0.0.1",
               1000, 8086, false, 1, 0);
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "127.0.0.1", 6, 1000, 8086);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    std::string nova_api("128.0.0.1");
    std::vector<std::string> fabric_ip_list;
    fabric_ip_list.push_back(nova_api);
    TestLinkLocalService service = {
        "metadata", "169.254.169.254", 80, "", fabric_ip_list, 8775
    };
    AddLinkLocalConfig(&service, 1);
    client->WaitForIdle();

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "128.0.0.1",
                1000, 8775, false, 1, 0);
    client->WaitForIdle();

    DelLinkLocalConfig();
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                 "10.1.1.1", "128.0.0.1", 6, 1000, 8775);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricControlFlow));

    TxTcpPacket(agent_->vhost_interface()->id(), "10.1.1.1", "10.1.1.10",
                22, 10000, false, 1, 0);
    client->WaitForIdle();

    fe = FlowGet(agent_->vhost_interface()->flow_key_nh()->id(),
                            "10.1.1.1", "10.1.1.10", 6, 22, 10000);
    EXPECT_TRUE(fe->IsShortFlow() == false);
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::FabricControlFlow));
}

int main(int argc, char *argv[]) {
    int ret = 0;
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    usleep(100000);
    TestShutdown();
    delete client;
    return ret;
}
