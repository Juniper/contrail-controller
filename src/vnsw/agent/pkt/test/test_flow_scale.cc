/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:01:01:01:01", 1, 1},
};


void RouterIdDepInit(Agent *agent) {
}

extern Peer *bgp_peer_;
class FlowTest : public ::testing::Test {
public:
    virtual void SetUp() {
        CreateVmportEnv(input, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(input, 0));

        vnet = VmInterfaceGet(1);
        strcpy(vnet_addr, vnet->ip_addr().to_string().c_str());

        boost::system::error_code ec;
        Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(bgp_peer_, "vrf1", 
                                        Ip4Address::from_string("5.0.0.0", ec),
                                        8, Ip4Address::from_string("1.1.1.2", ec),
                                        TunnelType::AllType(), 16, "TestVn",
                                        SecurityGroupList());
        client->WaitForIdle();
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    }

    virtual void TearDown() {
        int count = Agent::GetInstance()->pkt()->flow_table()->Size();
        
        client->EnqueueFlowFlush();
        WAIT_FOR(count, 10000, (0 == Agent::GetInstance()->pkt()->flow_table()->Size()));
        int a = count / 500;
        if (a == 0)
            a = 1;
        client->WaitForIdle(a);
        boost::system::error_code ec;
        Inet4UnicastAgentRouteTable::DeleteReq(bgp_peer_, "vrf1",
                                     Ip4Address::from_string("5.0.0.0", ec), 8);
        DeleteVmportEnv(input, 1, 1);
        client->WaitForIdle();
    }

    VmInterface *vnet;
    char vnet_addr[32];
};

TEST_F(FlowTest, FlowScaling_1) {
    char env[100];
    int count = 50;
    if (getenv("AGENT_FLOW_SCALE_COUNT")) {
        strcpy(env, getenv("AGENT_FLOW_SCALE_COUNT"));
        count = strtoul(env, NULL, 0);
    }
    int flow_count = Agent::GetInstance()->pkt()->flow_table()->Size();

    for (int i = 0; i < count; i++) {
        Ip4Address addr(0x05000000 + i);
        TxIpPacket(vnet->id(), vnet_addr,
                   addr.to_string().c_str(), 1);
    }

    count = count * 2;
    WAIT_FOR(count, 10000,
             (count == flow_count + (int) Agent::GetInstance()->pkt()->flow_table()->Size()));
}

int main(int argc, char *argv[]) {
    int ret = 0;

    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
