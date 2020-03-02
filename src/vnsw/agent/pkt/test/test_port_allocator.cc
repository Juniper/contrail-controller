/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include <base/os.h>
#include <base/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "pkt/flow_table.h"
#include "pkt/flow_entry.h"

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.10", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "10.1.1.1", "00:00:00:01:01:02", 1, 2}
};

IpamInfo ipam_info[] = {
   {"1.1.1.0", 24, "1.1.1.10"},
   {"10.1.1.0", 24, "10.1.1.10"},
};

class PortAllocationTest : public ::testing::Test {
public:
    PortAllocationTest() : agent_(Agent::GetInstance()),
    sip1_(0x1010101), dip1_(0x1010102), sip2_(0x1010103), dip2_(0x1010104) {
    }

protected:
    virtual void SetUp() {
        port_table_ = new PortTable(agent_, 1000, IPPROTO_TCP);
        PortConfig pc;
        pc.port_count = 10;
        pc.Trim();
        port_table_->UpdatePortConfig(&pc);
        client->WaitForIdle();

        AddVn(agent_->fabric_vn_name().c_str(), 100);
        client->WaitForIdle();

        CreateVmportEnv(input, 2);
        AddIPAM("vn1", ipam_info, 2);
        client->WaitForIdle();

        AddVrfWithSNat("vrf1", 1, true, true);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        agent_->pkt()->FlushFlows();
        delete port_table_;
        DelIPAM("vn1");
        DeleteVmportEnv(input, 2, true);
        client->WaitForIdle();
        DelVn(agent_->fabric_vn_name().c_str());
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

TEST_F(PortAllocationTest, Test4) {
    AddPortTranslationConfig();
    client->WaitForIdle();
    PortTableManager *p = agent_->pkt()->get_flow_proto()->port_table_manager();
    const PortTable *pt = p->GetPortTable(IPPROTO_TCP);
    const PortTable *pu = p->GetPortTable(IPPROTO_UDP);
    EXPECT_EQ(pt->port_config()->port_count, 101);
    EXPECT_EQ(pu->port_config()->port_count, 101);
    EXPECT_EQ(pt->port_config()->port_range[0].port_start, 400);
    EXPECT_EQ(pt->port_config()->port_range[0].port_end, 500);
    EXPECT_EQ(pu->port_config()->port_range[0].port_start, 600);
    EXPECT_EQ(pu->port_config()->port_range[0].port_end, 700);
    DeleteGlobalVrouterConfig();
    client->WaitForIdle();
    EXPECT_EQ(pt->port_config()->port_count, 0);
    EXPECT_EQ(pu->port_config()->port_count, 0);

}

TEST_F(PortAllocationTest, Range) {
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(50000, 50001));
    pc.Trim();

    port_table_->UpdatePortConfig(&pc);
    client->WaitForIdle();

    FlowKey key1(10, sip1_, dip1_, IPPROTO_TCP, 10, 20);

    port_table_->set_timeout(10 * 1000 * 1000);

    uint16_t port = port_table_->Allocate(key1);
    EXPECT_TRUE(port == 50000 || port == 50001);

    port_table_->Free(key1, port, false);
}

TEST_F(PortAllocationTest, PortExhaust) {
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(50000, 50000));
    pc.Trim();

    port_table_->UpdatePortConfig(&pc);
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

    VmInterface::FloatingIpSet::const_iterator it =
        vmi->floating_ip_list().list_.begin();
    EXPECT_TRUE(it->vn_->GetName() == agent()->fabric_vn_name());

    DelVn(agent()->fabric_vn_name().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(vmi->floating_ip_list().list_.size() == 0);

    AddVn(agent()->fabric_vn_name().c_str(), 100);
    client->WaitForIdle();
    EXPECT_TRUE(vmi->floating_ip_list().list_.size() == 1);

    AddVrfWithSNat("vrf1", 1, true, false);
    client->WaitForIdle();

    EXPECT_TRUE(vmi->floating_ip_list().list_.size() == 0);
}

//For Now no ICMP id translation hence just SNAT to vhost IP
TEST_F(PortAllocationTest, DISABLED_IcmpFlow) {
    TxIpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
                              1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->IsNatFlow() == true);
}

TEST_F(PortAllocationTest, ShortUdpFlow) {
    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    PortConfig pc;
    pm->UpdatePortConfig(IPPROTO_UDP, &pc);
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
    PortConfig pc;
    pc.port_count = 10;
    pc.Trim();
    pm->UpdatePortConfig(IPPROTO_UDP, &pc);
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
    PortConfig pc;
    pc.port_count = 10;
    pm->UpdatePortConfig(IPPROTO_UDP, &pc);
    pc.port_count = 0;
    pm->UpdatePortConfig(IPPROTO_TCP, &pc);
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
    PortConfig pc;
    pc.port_count = 10;
    pm->UpdatePortConfig(IPPROTO_TCP, &pc);
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
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(50001, 50100));
    pc.Trim();

    port_table_->UpdatePortConfig(&pc);
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
    PortConfig pc;
    pc.port_count = 3;
    pc.port_range.push_back(PortConfig::PortRange(50000, 50002));
    pc.Trim();

    port_table_->UpdatePortConfig(&pc);
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
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(50001, 50100));
    pc.Trim();

    port_table_->UpdatePortConfig(&pc);
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

    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(50000, 50010));
    pc.Trim();

    pm->UpdatePortConfig(IPPROTO_TCP, &pc);
    client->WaitForIdle();

    const PortTable *pt = pm->GetPortTable(IPPROTO_TCP);
    client->WaitForIdle();

    for (uint32_t i = 0; i < 10; i++) {
        TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100 + i, 100, false, 100 + i);
        client->WaitForIdle();
    }

    PortConfig pc1;
    pc1.port_range.push_back(PortConfig::PortRange(50005, 50010));
    pc1.Trim();

    pm->UpdatePortConfig(IPPROTO_TCP, &pc1);
    client->WaitForIdle();

    //All ports get relocated hence all flow should be deleted
    for (uint32_t i = 0; i < 10; i++) {
        FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
                6, 100 + i, 100, GetFlowKeyNH(1));
        if (i == 5) {
            //Port 50005 will hold on to its index
            EXPECT_TRUE(flow->IsShortFlow() == false);
            EXPECT_TRUE(flow->data().rpf_nh == VmPortGet(1)->flow_key_nh());
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
    PortConfig pc;
    pm->UpdatePortConfig(IPPROTO_TCP, &pc);
    client->WaitForIdle();

    pc.port_range.push_back(PortConfig::PortRange(50000, 50010));
    pc.Trim();
    pm->UpdatePortConfig(IPPROTO_TCP, &pc);

    const PortTable *pt = pm->GetPortTable(IPPROTO_TCP);
    client->WaitForIdle();

    for (uint32_t i = 0; i < 10; i++) {
        TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100 + i, 100, false, 100 + i);
        client->WaitForIdle();
    }

    //New ports gets added at end, hence no flow should be
    //deleted
    pc.port_count = 30;
    pc.port_range.clear();
    pc.port_range.push_back(PortConfig::PortRange(49980, 50010));
    pc.Trim();

    pm->UpdatePortConfig(IPPROTO_TCP, &pc);
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

TEST_F(PortAllocationTest, PolicyFlow) {
    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(22, 22));
    pc.Trim();

    pm->UpdatePortConfig(IPPROTO_TCP, &pc);
    client->WaitForIdle();

    TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100, 22, false);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
            6, 100, 22, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->IsNatFlow() == true);
    std::string vn1 = "vn1";
    EXPECT_TRUE(VnMatch(flow->data().source_vn_list, vn1));
    EXPECT_FALSE(flow->is_flags_set(FlowEntry::FabricControlFlow));
    EXPECT_FALSE(flow->reverse_flow_entry()->
            is_flags_set(FlowEntry::FabricControlFlow));
}

TEST_F(PortAllocationTest, IntraVn) {
    TxIpPacket(VmPortGetId(1), "1.1.1.10", "10.1.1.1", 1);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "10.1.1.1",
            1, 0, 0, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->IsNatFlow() == false);
}

TEST_F(PortAllocationTest, SecondaryIp) {
    Ip4Address ip = Ip4Address::from_string("1.1.1.3");
    std::vector<Ip4Address> v;
    v.push_back(ip);
    AddAap("vnet1", 1, v);

    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(50000, 50001));
    pc.Trim();

    PortTableManager *pm = agent_->pkt()->get_flow_proto()->port_table_manager();
    pm->UpdatePortConfig(IPPROTO_TCP, &pc);
    client->WaitForIdle();

    TxTcpPacket(VmPortGetId(1), "1.1.1.3", "8.8.8.8", 100, 22, false);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.3", "8.8.8.8",
            6, 100, 22, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->IsNatFlow() == true);
    EXPECT_FALSE(flow->is_flags_set(FlowEntry::FabricControlFlow));
    EXPECT_FALSE(flow->reverse_flow_entry()->
            is_flags_set(FlowEntry::FabricControlFlow));
    EXPECT_TRUE(flow->data().rpf_nh == VmPortGet(1)->flow_key_nh());
}

//Take a floating-ip from a network enabled for distributed SNAT
//ensure access from VM to external world goes thru
TEST_F(PortAllocationTest, FloatingIpWithSNATEnabled) {
    //Disable SNAT on VRF1
    AddVrfWithSNat("vrf1", 1, true, false);
    client->WaitForIdle();

    //Add a floating-ip in vn2 and enable VN2 for
    //fabric SNAT
    AddVn("default-project:vn2", 2);
    AddVrf("default-project:vn2:vn2", 2);
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle();

    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "1.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();

    AddVrfWithSNat("default-project:vn2:vn2", 2,  true, true);
    client->WaitForIdle();

    TxTcpPacket(VmPortGetId(1), "1.1.1.10", "8.8.8.8", 100, 22, false);
    client->WaitForIdle();

    FlowEntry *flow = FlowGet(GetVrfId("vrf1"), "1.1.1.10", "8.8.8.8",
            6, 100, 22, GetFlowKeyNH(1));
    EXPECT_TRUE(flow != NULL);
    EXPECT_TRUE(flow->IsShortFlow() == false);
    EXPECT_TRUE(flow->IsNatFlow() == true);
    EXPECT_TRUE(flow->reverse_flow_entry()->key().dst_addr.to_v4() ==
                agent_->router_id());
    EXPECT_TRUE(flow->data().rpf_nh == VmPortGet(1)->flow_key_nh());

    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelVn("default-project:vn2");
    DelVrf("default-project:vn2:vn2");
    client->WaitForIdle();
}

TEST_F(PortAllocationTest, MultiPortRange) {
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(1, 2));
    pc.port_range.push_back(PortConfig::PortRange(2, 4));
    pc.Trim();

    EXPECT_TRUE(pc.port_count == 4);

    port_table_->UpdatePortConfig(&pc);
    client->WaitForIdle();

    FlowKey key1(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 10, 20);
    FlowKey key2(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 11, 20);
    FlowKey key3(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 12, 20);
    FlowKey key4(10, Ip4Address(1), Ip4Address(2), IPPROTO_TCP, 13, 20);

    EXPECT_TRUE(port_table_->Allocate(key1) == 1);
    EXPECT_TRUE(port_table_->Allocate(key2) == 2);
    EXPECT_TRUE(port_table_->Allocate(key3) == 3);
    EXPECT_TRUE(port_table_->Allocate(key4) == 4);
}

TEST_F(PortAllocationTest, InvalidPortRange) {
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(2, 1));
    pc.port_range.push_back(PortConfig::PortRange(4, 2));
    pc.Trim();

    EXPECT_TRUE(pc.port_count == 0);
    pc.port_range.clear();

    pc.port_range.push_back(PortConfig::PortRange(2, 4));
    pc.port_range.push_back(PortConfig::PortRange(2, 1));
    pc.Trim();
    EXPECT_TRUE(pc.port_count == 3);

    pc.port_range.clear();
    pc.port_range.push_back(PortConfig::PortRange(2, 1));
    pc.port_range.push_back(PortConfig::PortRange(2, 4));
    pc.Trim();
    EXPECT_TRUE(pc.port_count == 3);
}

TEST_F(PortAllocationTest, Subset) {
    PortConfig pc;
    pc.port_range.push_back(PortConfig::PortRange(1, 2));
    pc.port_range.push_back(PortConfig::PortRange(3, 4));
    pc.port_range.push_back(PortConfig::PortRange(5, 6));
    pc.port_range.push_back(PortConfig::PortRange(1, 6));
    pc.Trim();
    EXPECT_TRUE(pc.port_count == 6);

    pc.port_range.clear();
    pc.port_range.push_back(PortConfig::PortRange(1, 6));
    pc.port_range.push_back(PortConfig::PortRange(1, 4));
    pc.port_range.push_back(PortConfig::PortRange(5, 6));
    pc.port_range.push_back(PortConfig::PortRange(1, 6));
    EXPECT_TRUE(pc.port_count == 6);

    pc.port_range.clear();
    pc.port_range.push_back(PortConfig::PortRange(1, 7));
    pc.port_range.push_back(PortConfig::PortRange(1, 8));
    pc.port_range.push_back(PortConfig::PortRange(1, 9));
    pc.port_range.push_back(PortConfig::PortRange(1, 10));
    pc.Trim();
    EXPECT_TRUE(pc.port_count == 10);

    pc.port_range.clear();
    pc.port_range.push_back(PortConfig::PortRange(100, 150));
    pc.port_range.push_back(PortConfig::PortRange(80, 130));
    pc.port_range.push_back(PortConfig::PortRange(90, 140));
    pc.Trim();
    EXPECT_TRUE(pc.port_count == 71);

    pc.port_range.clear();
    pc.port_range.push_back(PortConfig::PortRange(100, 150));
    pc.port_range.push_back(PortConfig::PortRange(200, 250));
    pc.Trim();
    EXPECT_TRUE(pc.port_count == 102);
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
