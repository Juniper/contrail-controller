/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include <base/task.h>
#include <base/test/task_test_util.h>

VmInterface *vnet[16];
InetInterface *vhost;
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

class BgpServiceTest : public ::testing::Test {
public:
    void AddAap(std::string intf_name, int intf_id, Ip4Address ip,
                const std::string &mac) {
        std::ostringstream buf;
        buf << "<virtual-machine-interface-allowed-address-pairs>";
        buf << "<allowed-address-pair>";
        buf << "<ip>";
        buf << "<ip-prefix>" << ip.to_string() <<"</ip-prefix>";
        buf << "<ip-prefix-len>"<< 32 << "</ip-prefix-len>";
        buf << "</ip>";
        buf << "<mac>" << mac << "</mac>";
        buf << "<flag>" << "act-stby" << "</flag>";
        buf << "</allowed-address-pair>";
        buf << "</virtual-machine-interface-allowed-address-pairs>";
        char cbuf[10000];
        strcpy(cbuf, buf.str().c_str());
        AddNode("virtual-machine-interface", intf_name.c_str(),
                intf_id, cbuf);
        client->WaitForIdle();
    }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        CreateVmportEnv(input, 1);
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
        SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1",
                             "vrf1", "bgpaas-client",
                             false);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, flow_proto_->FlowCount());
        client->WaitForIdle();
        SendBgpServiceConfig("1.1.1.10", 50000, 1, "vnet1",
                             "vrf1", "bgpaas-server",
                             true);
        DelIPAM("vn1");
        DeleteVmportEnv(input, 1, true);
        client->WaitForIdle();
    }

    Agent *agent_;
    FlowProto *flow_proto_;
    BgpPeer *peer;
};

TEST_F(BgpServiceTest, Test_1) {
    peer = CreateBgpPeer("127.0.0.1", "remote");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));

    DeleteBgpPeer(peer);
    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_2) {
    peer = CreateBgpPeer("127.0.0.1", "remote");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "1.1.1.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "1.1.1.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));

    //Explicitly call deleteall on bgp service tree.
    //agent_->pkt()->flow_mgmt_manager()->ControllerNotify(0);
    //agent_->pkt()->flow_mgmt_manager()->ControllerNotify(1);
    client->WaitForIdle();

    DeleteBgpPeer(peer);
    client->WaitForIdle();
}

TEST_F(BgpServiceTest, Test_3) {
    AddAap("vnet1", 1, Ip4Address::from_string("10.10.10.10"), "00:00:01:01:01:01");
    peer = CreateBgpPeer("127.0.0.1", "remote");
    client->WaitForIdle();

    TxTcpPacket(VmInterfaceGet(1)->id(), "10.10.10.10", "1.1.1.1", 10000, 179,
                false);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(VmInterfaceGet(1)->flow_key_nh()->id(),
                            "10.10.10.10", "1.1.1.1", 6, 10000, 179);
    EXPECT_TRUE(fe != NULL);
    EXPECT_TRUE(fe->reverse_flow_entry() != NULL);
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::BgpRouterService));

    DeleteBgpPeer(peer);
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
