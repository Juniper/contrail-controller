/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "base/os.h"
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "pkt/flow_table.h"
#include "pkt/flow_entry.h"

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
};

class PortAllocationTest : public ::testing::Test {
public:
    PortAllocationTest() : agent_(Agent::GetInstance()),
    sip1_(0x1010101), dip1_(0x1010102), sip2_(0x1010103), dip2_(0x1010104) {
    }

protected:
    virtual void SetUp() {
        port_table_ = new PortTable(agent_, 1000, IPPROTO_TCP);
        port_table_->UpdatePortConfig(10, 0, 0);
        client->WaitForIdle();

        CreateVmportEnv(input, 1);
        client->WaitForIdle();

        AddVrfWithSNat("vrf1", 1, true, true);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        delete port_table_;
        DeleteVmportEnv(input, 1, true);
        client->WaitForIdle();
    }

    Agent *agent() {return agent_;}
public:
    Agent *agent_;
    Ip4Address sip1_;
    Ip4Address dip1_;
    Ip4Address sip2_;
    Ip4Address dip2_;
    PortTable *port_table_;
};

TEST_F(PortAllocationTest, Test1) {
    FlowKey key1(10, sip1_, dip1_, IPPROTO_TCP, 10, 20);

    uint16_t port = port_table_->Allocate(key1);
    EXPECT_TRUE(port != 0);
    port_table_->Free(key1, port, true);
}

//Validate cache entry returns the same port
TEST_F(PortAllocationTest, Test2) {
    FlowKey key1(10, sip1_, dip1_, IPPROTO_TCP, 10, 20);

    uint16_t port = port_table_->Allocate(key1);
    EXPECT_TRUE(port != 0);
    //Find the flow in cache entry, return old port
    EXPECT_TRUE(port == port_table_->Allocate(key1));

    port_table_->Free(key1, port, true);
}

//Validate for same dip + port different port is allocated
//upon new sport and sip
TEST_F(PortAllocationTest, Test3) {
    FlowKey key1(10, sip1_, dip1_, IPPROTO_TCP, 10, 20);
    FlowKey key2(10, sip1_, dip2_, IPPROTO_TCP, 10, 20);
    FlowKey key3(10, sip2_, dip1_, IPPROTO_TCP, 10, 20);

    uint16_t port = port_table_->Allocate(key1);
    EXPECT_TRUE(port != 0);

    EXPECT_TRUE(port == port_table_->Allocate(key2));

    uint16_t port2 = port_table_->Allocate(key3);
    EXPECT_TRUE(port != port2);

    port_table_->Free(key1, port, true);
    port_table_->Free(key2, port, true); 
    port_table_->Free(key3, port2, true); 
}

TEST_F(PortAllocationTest, Range) {
    port_table_->UpdatePortConfig(10, 50000, 50001);
    client->WaitForIdle();

    FlowKey key1(10, sip1_, dip1_, IPPROTO_TCP, 10, 20);

    port_table_->set_timeout(10 * 1000 * 1000);

    uint16_t port = port_table_->Allocate(key1);
    EXPECT_TRUE(port == 50000 || port == 50001);
    
    port_table_->Free(key1, port, false);
}

TEST_F(PortAllocationTest, PortExhaust) {
    port_table_->UpdatePortConfig(1, 50000, 50000);
    client->WaitForIdle();

    FlowKey key1(10, sip1_, dip1_, IPPROTO_TCP, 10, 20);

    port_table_->set_timeout(10 * 1000 * 1000);

    uint16_t port = port_table_->Allocate(key1);
    EXPECT_TRUE(port == 50000);

    FlowKey key2(10, sip1_, dip1_, IPPROTO_TCP, 11, 20);
    uint16_t port2 = port_table_->Allocate(key2);
    EXPECT_TRUE(port2 == 0);

    port_table_->Free(key1, port, false);
}

TEST_F(PortAllocationTest, FloatingIpConfig) {
    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->floating_ip_list().list_.size() == 1);

    AddVrfWithSNat("vrf1", 1, true, false);
    client->WaitForIdle();

    EXPECT_TRUE(vmi->floating_ip_list().list_.size() == 0);
}

//For Now no ICMP id translation hence just SNAT to vhost IP
TEST_F(PortAllocationTest, DISABLED_IcmpFlow) {
    TxIpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 1);
    client->WaitForIdle();
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
                              1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->IsNatFlow() == true);
}

TEST_F(PortAllocationTest, ShortUdpFlow) {
    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    pm->UpdatePortConfig(IPPROTO_UDP, 0, 0, 0);
    client->WaitForIdle();

    TxUdpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100, 100);
    client->WaitForIdle();

    //No port config hence short flow
    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
                              17, 100, 100, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == true);
}

TEST_F(PortAllocationTest, UdpFlow) {
    //Configure port range only for UDP ensure UDP flow is created fine

    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    pm->UpdatePortConfig(IPPROTO_UDP, 10, 0, 0);
    client->WaitForIdle();

	TxUdpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100, 100);
	client->WaitForIdle();

	//No port config hence short flow
	FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
			17, 100, 100, GetFlowKeyNH(1));
	EXPECT_TRUE(flow != NULL);
	EXPECT_TRUE(flow->IsShortFlow() == false);
	EXPECT_TRUE(flow->IsNatFlow() == true);
}

TEST_F(PortAllocationTest, ShortTcpFlow) {
    //TCP flow is short flow as no port config
    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    pm->UpdatePortConfig(IPPROTO_UDP, 10, 0, 0);
    pm->UpdatePortConfig(IPPROTO_TCP, 0, 0, 0);
    client->WaitForIdle();

	TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100, 100, false);
	client->WaitForIdle();

	//No port config hence short flow
	FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
			6, 100, 100, GetFlowKeyNH(1));
	EXPECT_TRUE(flow != NULL);
	EXPECT_TRUE(flow->IsShortFlow() == true);
}

TEST_F(PortAllocationTest, TcpFlow) {
	PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
	pm->UpdatePortConfig(IPPROTO_TCP, 10, 0, 0);
	client->WaitForIdle();

	TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100, 100, false);
	client->WaitForIdle();

	//No port config hence short flow
	FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
			6, 100, 100, GetFlowKeyNH(1));
	EXPECT_TRUE(flow != NULL);
	EXPECT_TRUE(flow->IsShortFlow() == false);
	EXPECT_TRUE(flow->IsNatFlow() == true);
}

TEST_F(PortAllocationTest, NonTcpUdpFlow) {
    TxIpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 15);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
                              15, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == true);
}

TEST_F(PortAllocationTest, IpScale) {
    port_table_->UpdatePortConfig(1, 50000, 50000);
    client->WaitForIdle();

    int success = 0;
    for (uint32_t i = 0; i < 100; i++) {
        FlowKey key1(10, Ip4Address(i), Ip4Address(i), IPPROTO_TCP, 10, 20);
        if (port_table_->Allocate(key1)) {
            success++;
        }
    }
    EXPECT_TRUE(success == 100);
}

TEST_F(PortAllocationTest, PortRangeAllocation) {
    port_table_->UpdatePortConfig(1, 50000, 50002);
    client->WaitForIdle();

    FlowKey key1(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 10, 20);
    FlowKey key2(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 11, 20);
    FlowKey key3(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 12, 20);
    FlowKey key4(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 13, 20);

    EXPECT_TRUE(port_table_->Allocate(key1) == 50000);
    EXPECT_TRUE(port_table_->Allocate(key2) == 50001);
    EXPECT_TRUE(port_table_->Allocate(key3) == 50002);
    EXPECT_TRUE(port_table_->Allocate(key4) == 0);
}


TEST_F(PortAllocationTest, PortScale) {
    port_table_->UpdatePortConfig(1, 50000, 50000);
    client->WaitForIdle();

    int success = 0;
    for (uint32_t i = 0; i < 100; i++) {
        FlowKey key1(10, sip1_, dip1_, IPPROTO_TCP, 10, i);
        if (port_table_->Allocate(key1)) {
            success++;
        }
    }
    EXPECT_TRUE(success == 100);
}

TEST_F(PortAllocationTest, RangeUpdate) {
    //TCP flow is short flow as no port config
    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    pm->UpdatePortConfig(IPPROTO_TCP, 10, 50000, 50010);
    client->WaitForIdle();

    const PortTable *pt = pm->GetPortTable(IPPROTO_TCP);
    client->WaitForIdle();

    for (uint32_t i = 0; i < 10; i++) {
        TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100 + i, 100, false, 100 + i);
        client->WaitForIdle();
    }

    pm->UpdatePortConfig(IPPROTO_TCP, 10, 50005, 50010);
    client->WaitForIdle();

    //All ports get relocated hence all flow should be deleted
    for (uint32_t i = 0; i < 10; i++) {
        FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
                6, 100 + i, 100, GetFlowKeyNH(1));
        if (i == 5) {
            //Port 50005 will hold on to its index
            EXPECT_TRUE(flow->IsShortFlow() == false);
        } else {
            EXPECT_TRUE(flow == NULL);
        }
    }

    EXPECT_TRUE(pt->GetPortIndex(50006) == 0);
    EXPECT_TRUE(pt->GetPortIndex(50007) == 1);
    EXPECT_TRUE(pt->GetPortIndex(50005) == 5);
}

TEST_F(PortAllocationTest, RangeUpdate1) {
    //TCP flow is short flow as no port config
    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    pm->UpdatePortConfig(IPPROTO_TCP, 0, 0, 0);
    client->WaitForIdle();

    pm->UpdatePortConfig(IPPROTO_TCP, 10, 50000, 50010);
    const PortTable *pt = pm->GetPortTable(IPPROTO_TCP);
    client->WaitForIdle();

    for (uint32_t i = 0; i < 10; i++) {
        TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100 + i, 100, false, 100 + i);
        client->WaitForIdle();
    }

    //New ports gets added at end, hence no flow should be
    //deleted
    pm->UpdatePortConfig(IPPROTO_TCP, 10, 49980, 50010);
    client->WaitForIdle();

    for (uint32_t i = 0; i < 10; i++) {
        FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
                6, 100 + i, 100, GetFlowKeyNH(1));
        EXPECT_TRUE(flow != NULL);
    }

    //Verify port index doesnt change
    for (uint16_t i = 0; i < 10; i++) {
        EXPECT_TRUE(pt->GetPortIndex(50000 + i) == i);
    }
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client =
        TestInit(init_file, ksync_init, true, true, true);

    client->agent()->flow_stats_manager()->set_delete_short_flow(false);
 
    int ret = RUN_ALL_TESTS();

    client->WaitForIdle();
    
    TestShutdown();
    delete client;
    return ret;
}
