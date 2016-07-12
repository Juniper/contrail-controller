 /*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_flow_util.h"

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
};

struct PortInfo input1[] = {
    {"intf2", 2, "2.1.1.1", "00:00:00:01:01:01", 2, 2},
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

IpamInfo ipam_info2[] = {
    {"2.1.1.0", 24, "2.1.1.10"},
};

class TestVrfAssignAclFlowFip : public ::testing::Test {
public:
protected:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        fip_dest_ip = Ip4Address::from_string("2.1.1.1");
        remote_server = Ip4Address::from_string("10.10.10.10");
        Ip4Address fip_src_ip = Ip4Address::from_string("2.1.1.100");
 
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        
        CreateVmportFIpEnv(input, 1);
        CreateVmportFIpEnv(input1, 1);
        client->WaitForIdle();
        AddIPAM("default-project:vn1", ipam_info, 1);
        client->WaitForIdle();
        AddIPAM("default-project:vn2", ipam_info2, 1);
        client->WaitForIdle();

        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        client->WaitForIdle();
        
        AddVrf("__internal__");
        client->WaitForIdle();

        //Add src FIP and destination IP route in __internal__ vrf
        vm_intf = static_cast<const VmInterface *>(VmPortGet(1));
        VnListType vn_list;
        vn_list.insert("default-project:vn2");
        agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq(vm_intf->peer(),
                std::string("__internal__"), fip_src_ip, 32, vm_intf->GetUuid(), 
                vn_list, vm_intf->label(), SecurityGroupList(), CommunityList(), 
                false, PathPreference(), Ip4Address(0), EcmpLoadBalance(),
                false);
        client->WaitForIdle();

        Inet4TunnelRouteAdd(bgp_peer_, "__internal__",
                        fip_dest_ip, 32, remote_server,
                        TunnelType::AllType(), 20,
                        "default-project:vn2", SecurityGroupList(),
                        PathPreference());
        client->WaitForIdle();

        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, "2.1.1.100");
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
                "default-project:vn2");
        AddLink("virtual-machine-interface", "intf1", "floating-ip", "fip1");
        client->WaitForIdle();

        AddVrfAssignNetworkAcl("Acl", 10, "default-project:vn2", 
                               "default-project:vn2", "pass", "__internal__");
        AddLink("virtual-network", "default-project:vn2", "access-control-list",
                "Acl");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelLink("virtual-network", "default-project:vn2", "access-control-list",
                "Acl");
        DelNode("access-control-list", "Acl");
        client->WaitForIdle();

        DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        DelLink("floating-ip-pool", "fip-pool1",
                "virtual-network", "default-project:vn2");
        DelLink("virtual-machine-interface", "intf1", "floating-ip", "fip1");
        DelFloatingIp("fip1");
        DelFloatingIpPool("fip-pool1");
        client->WaitForIdle();

        DeleteRoute("__internal__", "2.1.1.100", 32, vm_intf->peer());
        DeleteRoute("__internal__", "2.1.1.1", 32, bgp_peer_);
        client->WaitForIdle();

        DelVrf("__internal__");
        DeleteVmportFIpEnv(input, 1, true);
        DeleteVmportFIpEnv(input1, 1, true);
        client->WaitForIdle();
        DelIPAM("default-project:vn1");
        client->WaitForIdle();
        DelIPAM("default-project:vn2");
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
    }

    Agent *agent_;
    FlowProto *flow_proto_;
    Ip4Address fip_dest_ip;
    Ip4Address remote_server;
    const VmInterface *vm_intf;
};

//Ingress flow
//Fwd flow from VM via floating-ip to external compute node destination
TEST_F(TestVrfAssignAclFlowFip, VrfAssignAcl1) {
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "1.1.1.1", "2.1.1.1", IPPROTO_TCP, 
                       10, 20, "default-project:vn1:vn1", VmPortGet(1)->id()),
        {
            new VerifyVn("default-project:vn2", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE)),
            new VerifyNat("2.1.1.1", "2.1.1.100", IPPROTO_TCP, 20, 10),
            new VerifySrcDstVrf("default-project:vn1:vn1", "__internal__",
                                "__internal__", "default-project:vn1:vn1")
        }
        }
    };
    CreateFlow(flow, 1);
}

//Egress flow
//Fwd flow from remote compute node to local FIP
TEST_F(TestVrfAssignAclFlowFip, VrfAssignAcl2) {
    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(1));
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "2.1.1.1", "2.1.1.100",
                       IPPROTO_TCP, 10, 20, "default-project:vn1:vn1",
                       remote_server.to_string(), vm_intf->label()),
        {
            new VerifyVn("default-project:vn2", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS |
                             (1 << TrafficAction::VRF_TRANSLATE))),
            new VerifyNat("1.1.1.1", "2.1.1.1", IPPROTO_TCP, 20, 10),
            new VerifySrcDstVrf("default-project:vn1:vn1", "default-project:vn1:vn1",
                                "default-project:vn1:vn1", "__internal__")
        }
        }
    };

    CreateFlow(flow, 1);
}

//Verify that interface VRF assign rule doesnt get 
//applied on floating-ip packets
TEST_F(TestVrfAssignAclFlowFip, VrfAssignAcl3) {
    AddVrf("__invalid__");
    client->WaitForIdle();
    AddAddressVrfAssignAcl("intf1", 1, "2.1.1.0", "2.1.1.0", 6, 1, 65535,
                           1, 65535, "__invalid__", "true");

    const VmInterface *vm_intf = static_cast<const VmInterface *>(VmPortGet(1));
    TestFlow flow[] = {
        {  TestFlowPkt(Address::INET, "2.1.1.1", "2.1.1.100",
                       IPPROTO_TCP, 10, 20, "default-project:vn1:vn1",
                       remote_server.to_string(), vm_intf->label()),
        {
            new VerifyVn("default-project:vn2", "default-project:vn2"),
            new VerifyAction((1 << TrafficAction::PASS) |
                             (1 << TrafficAction::VRF_TRANSLATE),
                             (1 << TrafficAction::PASS |
                             (1 << TrafficAction::VRF_TRANSLATE))),
            new VerifyNat("1.1.1.1", "2.1.1.1", IPPROTO_TCP, 20, 10),
            new VerifySrcDstVrf("default-project:vn1:vn1", "default-project:vn1:vn1",
                                "default-project:vn1:vn1", "__internal__")
        }
        }
    };

    CreateFlow(flow, 1);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    boost::system::error_code ec;
    bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer_);
    TestShutdown();
    delete client;
    return ret;
}
