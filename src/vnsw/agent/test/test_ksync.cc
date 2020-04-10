/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"

class TestNhPeer : public Peer {
public:
    TestNhPeer() : Peer(BGP_PEER, "TestNH", false), dummy_(0) { };
    int dummy_;
};

class TestKSync : public ::testing::Test {
public:
    TestKSync() { }
    virtual ~TestKSync() { }

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        server_ip_ = Ip4Address::from_string("10.1.1.11");

        struct PortInfo input[] = {
            {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        };
        CreateVmportEnv(input, 1, 0);
        client->WaitForIdle();
        vmi_ = static_cast<VmInterface *>(VmPortGet(1));
        EXPECT_TRUE(vmi_->vrf() != NULL);

        vrf_ = VrfGet(vmi_->vrf()->GetName().c_str());
        peer_ = new TestNhPeer();
        sock_ = KSyncSock::Get(0);
    }

    uint32_t GetSeqNo() {
        return (uint32_t)sock_->AllocSeqNo(IoContext::IOC_KSYNC, 0);
    }

    virtual void TearDown() {
        struct PortInfo input[] = {
            {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        };
        DeleteVmportEnv(input, 1, 1, 0);
        client->WaitForIdle();

        delete peer_;
    }

    void AddRemoteVmRoute(uint32_t addr) {
        Ip4Address ip(addr);
        VnListType vn_list;
        vn_list.insert("Test");
        agent_->fabric_inet4_unicast_table()->AddLocalVmRouteReq
            (peer_, vmi_->vrf()->GetName(), ip, 32, MakeUuid(1),
             vn_list, 10, SecurityGroupList(), TagList(), CommunityList(),
             false, PathPreference(), Ip4Address(0), EcmpLoadBalance(), false,
             false, false);
        client->WaitForIdle();
    }

    void DeleteRoute(uint32_t addr) {
        Ip4Address ip(addr);
         agent_->fabric_inet4_unicast_table()->DeleteReq
         (peer_, vmi_->vrf()->GetName(), ip, 32, NULL);
        client->WaitForIdle();
    }

    AgentRoute *GetRoute(uint32_t addr) {
        Ip4Address ip(addr);
        return RouteGet(vmi_->vrf()->GetName(), ip, 32);
    }

    Agent *agent_;
    VmInterface *vmi_;
    Ip4Address server_ip_;
    TestNhPeer *peer_;
    KSyncSock *sock_;
    VrfEntryRef vrf_;
};

TEST_F(TestKSync, SeqNum_1) {
    // Add 100 routes
    uint32_t ip = 0x0A0A0000;
    for (int i = 0; i < 100; i++) {
        AddRemoteVmRoute(ip + i);
        client->WaitForIdle();
        EXPECT_TRUE(GetRoute(ip + i) != NULL);
    }

    sock_->SetSeqno(0xFFFFFFF0);

    for (int i = 0; i < 100; i++) {
        DeleteRoute(ip + i);
        client->WaitForIdle();
        EXPECT_TRUE(GetRoute(ip + i) == NULL);
    }

    EXPECT_EQ(0U, sock_->WaitTreeSize());
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
