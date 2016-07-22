/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

TestQosConfigData data1 = {"qos_config1", 1, "default", 1};
TestQosConfigData data2 = {"qos_config2", 2, "default", 1};
TestQosConfigData data3 = {"qos_config3", 3, "default", 1};

class FlowQosTest : public ::testing::Test {

public:
    FlowQosTest(): agent(Agent::GetInstance()) {
    }

    ~FlowQosTest() {}

    virtual void SetUp() {
        CreateVmportEnv(input, 2);
        client->WaitForIdle();
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
        AddQosConfig(data1);
        AddQosConfig(data2);
        AddQosConfig(data3);
        AddQosAcl("acl1", 1, "vn1", "vn1", "pass", "qos_config1");
        AddLink("virtual-network", "vn1", "access-control-list", "acl1");
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        intf1 = static_cast<VmInterface *>(VmPortGet(1));
        intf2 = static_cast<VmInterface *>(VmPortGet(2));

        boost::system::error_code ec;
        bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                                 "xmpp channel");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true);
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        DelNode("qos-config", "qos_config1");
        DelNode("qos-config", "qos_config2");
        DelNode("qos-config", "qos_config3");
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VrfFind("vrf1", true));
        client->WaitForIdle();
        DeleteBgpPeer(bgp_peer_);
        client->WaitForIdle();
    }

    const VmInterface *intf1;
    const VmInterface *intf2;
    Agent *agent;
};

TEST_F(FlowQosTest, Test_1) {
    TestFlow flow[] = {
        //Send an ICMP flow from VM1 to VM2
        {   
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {   
                new VerifyVn("vn1", "vn1"),
                new VerifyQosAction("qos_config1", "qos_config1")
            }
        }
    };

    CreateFlow(flow, 1);
}

TEST_F(FlowQosTest, Test_2) {
    char vm_ip[]  = "1.1.1.3";
    char server_ip[] = "10.1.1.3";
    Inet4TunnelRouteAdd(bgp_peer_, "vrf1", vm_ip, 32, server_ip,
                        TunnelType::AllType(), 16, "vn1",
                        SecurityGroupList(), PathPreference());
    client->WaitForIdle();

    TestFlow flow[] = {
        //Send an ICMP flow from remote VM in vn3 to local VM in vn5
        {   
            TestFlowPkt(Address::INET, "1.1.1.3", "1.1.1.1", 1, 0, 0, "vrf1",
                        "10.1.1.1", intf1->label()),
            {   
                new VerifyVn("vn1", "vn1"),
                new VerifyQosAction("qos_config1", "qos_config1")
            }
        },
    };

    CreateFlow(flow, 1);

    Ip4Address ip = Ip4Address::from_string("1.1.1.3");
    InetUnicastAgentRouteTable::
        DeleteReq(bgp_peer_, "vrf1", ip, 32, new ControllerVmRoute(bgp_peer_));
}

TEST_F(FlowQosTest, Test_3) {
    TestFlow tflow[] = {
        //Send an ICMP flow from VM1 to VM2
        {   
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {   
                new VerifyVn("vn1", "vn1"),
                new VerifyQosAction("qos_config1", "qos_config1")
            }
        }
    };

    CreateFlow(tflow, 1);
    AddQosAcl("acl1", 1, "vn1", "vn1", "pass", "qos_config2");
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(0, "1.1.1.1", "1.1.1.2", 1, 0, 0,
                              intf1->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(QosConfigGetByIndex(flow->data().qos_config_idx)->name() ==
                "qos_config2");
    EXPECT_TRUE(QosConfigGetByIndex(rflow->data().qos_config_idx)->name() ==
                "qos_config2");
}

TEST_F(FlowQosTest, Test_4) {
    TestFlow tflow[] = {
        //Send an ICMP flow from VM1 to VM2
        {   
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                    intf1->id()),
            {   
                new VerifyVn("vn1", "vn1"),
                new VerifyQosAction("qos_config1", "qos_config1")
            }
        }
    };

    CreateFlow(tflow, 1);
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(0, "1.1.1.1", "1.1.1.2", 1, 0, 0, 
                              intf1->flow_key_nh()->id());
    EXPECT_TRUE(flow->data().qos_config_idx == AgentQosConfigTable::kInvalidIndex);
    EXPECT_TRUE(flow->data().qos_config_idx == AgentQosConfigTable::kInvalidIndex);
}

TEST_F(FlowQosTest, Test_5) {
    //Verify that interface qos config takes precedence
    TestFlow tflow[] = {
        //Send an ICMP flow from VM1 to VM2
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                    intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
                new VerifyQosAction("qos_config1", "qos_config1")
            }
        }
    };

    CreateFlow(tflow, 1);
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "intf1",
            "qos-config", "qos_config2");
    AddLink("virtual-machine-interface", "intf2",
            "qos-config", "qos_config1");
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(0, "1.1.1.1", "1.1.1.2", 1, 0, 0,
                              intf1->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();

    EXPECT_TRUE(QosConfigGetByIndex(flow->data().qos_config_idx)->name() ==
                "qos_config2");
    EXPECT_TRUE(QosConfigGetByIndex(rflow->data().qos_config_idx)->name() ==
                "qos_config1");

    DelLink("virtual-machine-interface", "intf1",
            "qos-config", "qos_config1");
    DelLink("virtual-machine-interface", "intf2",
            "qos-config", "qos_config1");
    client->WaitForIdle();
}

TEST_F(FlowQosTest, Test_6) {
    //Verify that interface qos config takes precedence
    TestFlow tflow[] = {
        //Send an ICMP flow from VM1 to VM2
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                    intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
                new VerifyQosAction("qos_config1", "qos_config1")
            }
        }
    };

    CreateFlow(tflow, 1);
    client->WaitForIdle();

    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    AddLink("virtual-network", "vn1",
            "qos-config", "qos_config2");
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(0, "1.1.1.1", "1.1.1.2", 1, 0, 0,
                              intf1->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(QosConfigGetByIndex(flow->data().qos_config_idx)->name() ==
                "qos_config2");
    EXPECT_TRUE(QosConfigGetByIndex(rflow->data().qos_config_idx)->name() ==
                "qos_config2");

    DelLink("virtual-network", "vn1", "qos-config", "qos_config1");
    client->WaitForIdle();
}

TEST_F(FlowQosTest, Test_7) {
    AddLink("virtual-machine-interface", "intf1",
            "qos-config", "qos_config3");
    AddLink("virtual-machine-interface", "intf2",
            "qos-config", "qos_config3");
    AddLink("virtual-network", "vn1",
            "qos-config", "qos_config2");
    client->WaitForIdle();

    //Verify that interface qos config takes precedence
    TestFlow tflow[] = {
        //Send an ICMP flow from VM1 to VM2
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                    intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
                new VerifyQosAction("qos_config3", "qos_config3")
            }
        }
    };

    CreateFlow(tflow, 1);
    client->WaitForIdle();

    DelLink("virtual-machine-interface", "intf1",
            "qos-config", "qos_config3");
    DelLink("virtual-machine-interface", "intf2",
            "qos-config", "qos_config3");
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    AddLink("virtual-network", "vn1",
            "qos-config", "qos_config2");
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(0, "1.1.1.1", "1.1.1.2", 1, 0, 0,
                              intf1->flow_key_nh()->id());
    FlowEntry *rflow = flow->reverse_flow_entry();
    EXPECT_TRUE(QosConfigGetByIndex(flow->data().qos_config_idx)->name() ==
                "qos_config2");
    EXPECT_TRUE(QosConfigGetByIndex(rflow->data().qos_config_idx)->name() ==
                "qos_config2");

    AddLink("virtual-network", "vn1",
            "access-control-list", "acl1");
    AddLink("virtual-machine-interface", "intf1",
            "qos-config", "qos_config3");
    AddLink("virtual-machine-interface", "intf2",
            "qos-config", "qos_config3");
    client->WaitForIdle();
    EXPECT_TRUE(QosConfigGetByIndex(flow->data().qos_config_idx)->name() ==
            "qos_config3");
    EXPECT_TRUE(QosConfigGetByIndex(rflow->data().qos_config_idx)->name() ==
            "qos_config3");

    DelLink("virtual-machine-interface", "intf1",
            "qos-config", "qos_config3");
    DelLink("virtual-machine-interface", "intf2",
            "qos-config", "qos_config3");
    DelLink("virtual-network", "vn1", "qos-config", "qos_config1");
    client->WaitForIdle();
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
