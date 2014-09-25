/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "test_pkt_util.h"
#include "cmn/agent_cmn.h"

void RouterIdDepInit(Agent *agent) {
}

class PktParseTest : public ::testing::Test {
public:
    virtual void SetUp() {
        client->WaitForIdle();
        client->Reset();
        agent_ = Agent::GetInstance();
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();
    }

    Agent *agent_;
};

uint32_t GetPktModuleCount(PktHandler::PktModuleName mod) {
    PktHandler::PktStats stats =
        Agent::GetInstance()->pkt()->pkt_handler()->GetStats();
    return stats.received[mod];
}

bool CallPktParse(PktInfo *pkt_info, uint8_t *ptr, int len) {
    Interface *intf = NULL;
    uint8_t *pkt;

    pkt_info->pkt = ptr;
    pkt_info->len = len;

    TestPkt0Interface *pkt0 = client->agent_init()->pkt0();
    AgentHdr hdr;
    int hdr_len = pkt0->DecodeAgentHdr(&hdr, ptr, len);
    if (hdr_len <= 0) {
        LOG(ERROR, "Error parsing Agent Header");
        return false;
    }

    AgentStats::GetInstance()->incr_pkt_exceptions();
    pkt_info->agent_hdr = hdr;
    intf = InterfaceTable::GetInstance()->FindInterface(pkt_info->agent_hdr.ifindex);
    if (intf == NULL) {
        LOG(ERROR, "Invalid interface index <" << pkt_info->agent_hdr.ifindex << ">");
        return true;
    }
    pkt_info->vrf = intf->vrf_id();
    pkt_info->type = PktType::INVALID;
    Agent::GetInstance()->pkt()->pkt_handler()->ParseUserPkt(pkt_info, intf,
                                                             pkt_info->type,
                                                             ptr + hdr_len);
    return true;
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
    {"vnet3", 3, "2.1.1.1", "00:00:00:02:01:01", 2, 3},
};

static void Setup() {
    client->Reset();
    CreateVmportEnv(input, 3, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_TRUE(VmPortActive(input, 2));
    EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    EXPECT_TRUE(VmPortPolicyEnable(input, 1));
    EXPECT_TRUE(VmPortPolicyEnable(input, 2));
}

static void Teardown() {
    client->Reset();
    DeleteVmportEnv(input, 3, true, 1);
    client->WaitForIdle();
}

TEST_F(PktParseTest, Stats_1) {
    unsigned int exception_count = AgentStats::GetInstance()->pkt_exceptions();
    VmInterface *vnet1 = VmInterfaceGet(1);

    TxIpPacket(vnet1->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();
    EXPECT_EQ(AgentStats::GetInstance()->pkt_exceptions(), (exception_count + 1));
}

TEST_F(PktParseTest, InvalidAgentHdr_1) {
    unsigned int exception_count = AgentStats::GetInstance()->pkt_exceptions();
    unsigned int drop_count = AgentStats::GetInstance()->pkt_dropped();
    unsigned int err_count = AgentStats::GetInstance()->pkt_invalid_agent_hdr();
    VmInterface *vnet1 = VmInterfaceGet(1);

    std::auto_ptr<PktGen> pkt(new PktGen());
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(vnet1->id(), 0);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr("1.1.1.1", "1.1.1.2", 1);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket
        (ptr, (sizeof(ethhdr) + sizeof(agent_hdr)), pkt->GetBuffLen());

    client->WaitForIdle();
    EXPECT_EQ((exception_count + 1), AgentStats::GetInstance()->pkt_exceptions());
    EXPECT_EQ((drop_count + 1), AgentStats::GetInstance()->pkt_dropped());
    EXPECT_EQ((err_count + 1), AgentStats::GetInstance()->pkt_invalid_agent_hdr());
}

TEST_F(PktParseTest, InvalidIntf_1) {
    unsigned int exception_count = AgentStats::GetInstance()->pkt_exceptions();
    unsigned int drop_count = AgentStats::GetInstance()->pkt_dropped();
    unsigned int err_count = AgentStats::GetInstance()->pkt_invalid_interface();

    TxIpPacket(100, "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();
    EXPECT_EQ((exception_count + 1), AgentStats::GetInstance()->pkt_exceptions());
    EXPECT_EQ((err_count + 1), AgentStats::GetInstance()->pkt_invalid_interface());
    EXPECT_EQ((drop_count + 1), AgentStats::GetInstance()->pkt_dropped());
}

TEST_F(PktParseTest, Arp_1) {
    unsigned int exception_count = AgentStats::GetInstance()->pkt_exceptions();
    uint32_t arp_count = GetPktModuleCount(PktHandler::ARP);
    VmInterface *vnet1 = VmInterfaceGet(1);

    std::auto_ptr<PktGen> pkt(new PktGen());
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(vnet1->id(), 0);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x806);
    pkt->AddIpHdr("1.1.1.1", "1.1.1.2", 1);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());

    client->WaitForIdle();
    EXPECT_EQ((exception_count + 1), AgentStats::GetInstance()->pkt_exceptions());
    EXPECT_EQ((arp_count + 1), GetPktModuleCount(PktHandler::ARP));
}

TEST_F(PktParseTest, NonIp_On_Vnet_1) {
    unsigned int exception_count = AgentStats::GetInstance()->pkt_exceptions();
    uint32_t drop_count = GetPktModuleCount(PktHandler::INVALID);
    VmInterface *vnet1 = VmInterfaceGet(1);

    // Packet with VLAN header 0x8100
    std::auto_ptr<PktGen> pkt1(new PktGen());
    pkt1->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt1->AddAgentHdr(vnet1->id(), 0);
    pkt1->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x8100);
    uint8_t *ptr1(new uint8_t[pkt1->GetBuffLen() + 64]);
    memcpy(ptr1, pkt1->GetBuff(), pkt1->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr1, pkt1->GetBuffLen() + 64, pkt1->GetBuffLen() + 64);

    // Packet with VLAN header 0x88a8
    std::auto_ptr<PktGen> pkt2(new PktGen());
    pkt2->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt2->AddAgentHdr(vnet1->id(), 0);
    pkt2->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x88a8);
    uint8_t *ptr2(new uint8_t[pkt2->GetBuffLen() + 64]);
    memcpy(ptr2, pkt2->GetBuff(), pkt2->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr2, pkt2->GetBuffLen() + 64, pkt2->GetBuffLen() + 64);

    // Packet with VLAN header 0x9100
    std::auto_ptr<PktGen> pkt3(new PktGen());
    pkt3->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt3->AddAgentHdr(vnet1->id(), 0);
    pkt3->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x9100);
    uint8_t *ptr3(new uint8_t[pkt3->GetBuffLen() + 64]);
    memcpy(ptr3, pkt3->GetBuff(), pkt3->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr3, pkt3->GetBuffLen() + 64, pkt3->GetBuffLen() + 64);

    // Packet with ether-type 0x100
    std::auto_ptr<PktGen> pkt4(new PktGen());
    pkt4->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt4->AddAgentHdr(vnet1->id(), 0);
    pkt4->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x100);
    uint8_t *ptr4(new uint8_t[pkt4->GetBuffLen() + 64]);
    memcpy(ptr4, pkt4->GetBuff(), pkt4->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr4, pkt4->GetBuffLen() + 64, pkt4->GetBuffLen() + 64);

    client->WaitForIdle();
    EXPECT_EQ((exception_count + 4), AgentStats::GetInstance()->pkt_exceptions());
    EXPECT_EQ((drop_count + 4), GetPktModuleCount(PktHandler::INVALID));
}

TEST_F(PktParseTest, NonIp_On_Eth_1) {
    unsigned int exception_count = AgentStats::GetInstance()->pkt_exceptions();
    uint32_t drop_count = GetPktModuleCount(PktHandler::INVALID);
    PhysicalInterface *eth = EthInterfaceGet("vnet0");

    // Packet with VLAN header 0x8100
    std::auto_ptr<PktGen> pkt1(new PktGen());
    pkt1->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt1->AddAgentHdr(eth->id(), 0);
    pkt1->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x8100);
    uint8_t *ptr1(new uint8_t[pkt1->GetBuffLen() + 64]);
    memcpy(ptr1, pkt1->GetBuff(), pkt1->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr1, pkt1->GetBuffLen() + 64, pkt1->GetBuffLen() + 64);

    // Packet with VLAN header 0x88a8
    std::auto_ptr<PktGen> pkt2(new PktGen());
    pkt2->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt2->AddAgentHdr(eth->id(), 0);
    pkt2->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x88a8);
    uint8_t *ptr2(new uint8_t[pkt2->GetBuffLen() + 64]);
    memcpy(ptr2, pkt2->GetBuff(), pkt2->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr2, pkt2->GetBuffLen() + 64, pkt2->GetBuffLen() + 64);

    // Packet with VLAN header 0x9100
    std::auto_ptr<PktGen> pkt3(new PktGen());
    pkt3->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt3->AddAgentHdr(eth->id(), 0);
    pkt3->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x9100);
    uint8_t *ptr3(new uint8_t[pkt3->GetBuffLen() + 64]);
    memcpy(ptr3, pkt3->GetBuff(), pkt3->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr3,
                                                    pkt3->GetBuffLen() + 64,
                                                    pkt3->GetBuffLen() + 64);

    // Packet with ether-type 0x100
    std::auto_ptr<PktGen> pkt4(new PktGen());
    pkt4->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt4->AddAgentHdr(eth->id(), 0);
    pkt4->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x100);
    uint8_t *ptr4(new uint8_t[pkt4->GetBuffLen() + 64]);
    memcpy(ptr4, pkt4->GetBuff(), pkt4->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr4,
                                                    pkt4->GetBuffLen() + 64,
                                                    pkt4->GetBuffLen() + 64);

    client->WaitForIdle();
    EXPECT_EQ((exception_count + 4), AgentStats::GetInstance()->pkt_exceptions());
    EXPECT_EQ((drop_count + 4), GetPktModuleCount(PktHandler::INVALID));
}


static bool TestPkt(PktInfo *pkt_info, PktGen *pkt) {
    uint8_t *buff(new uint8_t[pkt->GetBuffLen() + 64]);
    memcpy(buff, pkt->GetBuff(), pkt->GetBuffLen());
    return CallPktParse(pkt_info, buff, pkt->GetBuffLen() + 64);
}

static bool ValidateIpPktInfo(PktInfo *pkt_info, const char *sip,
                              const char *dip, uint16_t proto, uint16_t sport,
                              uint16_t dport) {
    bool ret = true;

    EXPECT_TRUE(pkt_info->ip != NULL);
    if (pkt_info->ip == NULL) {
        ret = false;
    }

    EXPECT_EQ(pkt_info->ip_saddr.to_v4().to_ulong(), htonl(inet_addr(sip)));
    if (pkt_info->ip_saddr.to_v4().to_ulong() != htonl(inet_addr(sip))) {
        ret = false;
    }

    EXPECT_EQ(pkt_info->ip_daddr.to_v4().to_ulong(), htonl(inet_addr(dip)));
    if (pkt_info->ip_daddr.to_v4().to_ulong() != htonl(inet_addr(dip))) {
        ret = false;
    }

    EXPECT_EQ(pkt_info->ip_proto, proto);
    if (pkt_info->ip_proto != proto) {
        ret = false;
    }

    if (proto == IPPROTO_UDP) {
        EXPECT_EQ(pkt_info->type, PktType::UDP);
        if (pkt_info->type != PktType::UDP) {
            ret = false;
        }

        EXPECT_EQ(pkt_info->sport, sport);
        if (pkt_info->sport != sport) {
            ret = false;
        }

        EXPECT_EQ(pkt_info->dport, dport);
        if (pkt_info->dport != dport) {
            ret = false;
        }
    } else if (proto == IPPROTO_TCP) {
        EXPECT_EQ(pkt_info->type, PktType::TCP);
        if (pkt_info->type != PktType::TCP) {
            ret = false;
        }

        EXPECT_EQ(pkt_info->sport, sport);
        if (pkt_info->sport != sport) {
            ret = false;
        }
        EXPECT_EQ(pkt_info->dport, dport);
        if (pkt_info->dport != dport) {
            ret = false;
        }
    } else if (proto == IPPROTO_ICMP) {
        EXPECT_EQ(pkt_info->type, PktType::ICMP);
        if (pkt_info->type != PktType::ICMP) {
            ret = false;
        }
    } else {
        EXPECT_EQ(pkt_info->type, PktType::IP);
        if (pkt_info->type != PktType::IP) {
            ret = false;
        }
    }

    return ret;
}

static bool ValidateIp6PktInfo(PktInfo *pkt_info, const char *sip,
                               const char *dip, uint16_t proto, uint16_t sport,
                               uint16_t dport) {
    bool ret = true;

    EXPECT_TRUE(pkt_info->ip6 != NULL);
    if (pkt_info->ip6 == NULL) {
        ret = false;
    }

    EXPECT_TRUE(pkt_info->ip == NULL);

    EXPECT_TRUE((pkt_info->ip_saddr.to_v6() == Ip6Address::from_string(sip)));
    if (pkt_info->ip_saddr.to_v6() != Ip6Address::from_string(sip)) {
        ret = false;
    }

    EXPECT_TRUE((pkt_info->ip_daddr.to_v6() == Ip6Address::from_string(dip)));
    if (pkt_info->ip_daddr.to_v6() != Ip6Address::from_string(dip)) {
        ret = false;
    }

    EXPECT_EQ(pkt_info->ip_proto, proto);
    if (pkt_info->ip_proto != proto) {
        ret = false;
    }

    if (proto == IPPROTO_UDP) {
        EXPECT_EQ(pkt_info->type, PktType::UDP);
        if (pkt_info->type != PktType::UDP) {
            ret = false;
        }

        EXPECT_EQ(pkt_info->sport, sport);
        if (pkt_info->sport != sport) {
            ret = false;
        }

        EXPECT_EQ(pkt_info->dport, dport);
        if (pkt_info->dport != dport) {
            ret = false;
        }
    } else if (proto == IPPROTO_TCP) {
        EXPECT_EQ(pkt_info->type, PktType::TCP);
        if (pkt_info->type != PktType::TCP) {
            ret = false;
        }

        EXPECT_EQ(pkt_info->sport, sport);
        if (pkt_info->sport != sport) {
            ret = false;
        }
        EXPECT_EQ(pkt_info->dport, dport);
        if (pkt_info->dport != dport) {
            ret = false;
        }
    } else if (proto == IPPROTO_ICMP) {
        EXPECT_EQ(pkt_info->type, PktType::ICMP);
        if (pkt_info->type != PktType::ICMP) {
            ret = false;
        }
    } else if (proto == IPPROTO_ICMPV6) {
        EXPECT_EQ(pkt_info->type, PktType::ICMPV6);
        if (pkt_info->type != PktType::ICMPV6) {
            ret = false;
        }
    } else {
        EXPECT_EQ(pkt_info->type, PktType::IP);
        if (pkt_info->type != PktType::IP) {
            ret = false;
        }
    }

    return ret;
}

TEST_F(PktParseTest, IP_On_Vnet_1) {
    VmInterface *vnet1 = VmInterfaceGet(1);
    std::auto_ptr<PktGen> pkt(new PktGen());

    pkt->Reset();
    MakeIpPacket(pkt.get(), vnet1->id(), "1.1.1.1", "1.1.1.2", 1, 1, -1);
    PktInfo pkt_info1(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info1, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info1, "1.1.1.1", "1.1.1.2", 1, 0, 0));

    pkt->Reset();
    MakeUdpPacket(pkt.get(), vnet1->id(), "1.1.1.1", "1.1.1.2", 1, 2, 2, -1);
    PktInfo pkt_info2(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info2, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info2, "1.1.1.1", "1.1.1.2", IPPROTO_UDP,
                                  1, 2));

    pkt->Reset();
    MakeTcpPacket(pkt.get(), vnet1->id(), "1.1.1.1", "1.1.1.2", 1, 2, false, 3, -1);
    PktInfo pkt_info3(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info3, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info3, "1.1.1.1", "1.1.1.2", IPPROTO_TCP, 
                                  1, 2));
}

TEST_F(PktParseTest, IPv6_On_Vnet_1) {
    VmInterface *vnet1 = VmInterfaceGet(1);
    PktGen *pkt = new PktGen();
    PktInfo pkt_info(NULL, 0, 0, 0);

    pkt->Reset();
    MakeIp6Packet(pkt, vnet1->id(), "1::1", "1::2",
                  IPPROTO_ICMPV6, 1, -1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIp6PktInfo(&pkt_info, "1::1", "1::2", IPPROTO_ICMPV6,
                                   0, 0));

    pkt->Reset();
    MakeUdp6Packet(pkt, vnet1->id(), "1::1", "1::2", 1, 2, 2, -1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIp6PktInfo(&pkt_info, "1::1", "1::2", IPPROTO_UDP,
                                   1, 2));

    pkt->Reset();
    MakeTcp6Packet(pkt, vnet1->id(), "1::1", "1::2", 1, 2, false, 3, -1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIp6PktInfo(&pkt_info, "1::1", "1::2", IPPROTO_TCP,
                                   1, 2));
}

TEST_F(PktParseTest, IP_On_Eth_1) {
    PhysicalInterface *eth = EthInterfaceGet("vnet0");
    std::auto_ptr<PktGen> pkt(new PktGen());

    pkt->Reset();
    MakeIpPacket(pkt.get(), eth->id(), "1.1.1.1", "1.1.1.2", 1, 1, -1);
    PktInfo pkt_info1(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info1, pkt.get());
    client->WaitForIdle();
    EXPECT_EQ(pkt_info1.type, PktType::INVALID);

    pkt->Reset();
    MakeUdpPacket(pkt.get(), eth->id(), "1.1.1.1", "1.1.1.2", 1, 2, 2, -1);
    PktInfo pkt_info2(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info2, pkt.get());
    client->WaitForIdle();
    EXPECT_EQ(pkt_info2.type, PktType::INVALID);

    pkt->Reset();
    MakeTcpPacket(pkt.get(), eth->id(), "1.1.1.1", "1.1.1.2", 1, 2, false, 3, -1);
    PktInfo pkt_info3(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info3, pkt.get());
    client->WaitForIdle();
    EXPECT_EQ(pkt_info3.type, PktType::INVALID);
}

TEST_F(PktParseTest, IPv6_On_Eth_1) {
    PhysicalInterface *eth = EthInterfaceGet("vnet0");
    PktGen *pkt = new PktGen();
    PktInfo pkt_info(NULL, 0, 0, 0);

    pkt->Reset();
    MakeIp6Packet(pkt, eth->id(), "1::1", "1::2", IPPROTO_ICMPV6, 1, -1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_EQ(pkt_info.type, PktType::INVALID);

    pkt->Reset();
    MakeUdp6Packet(pkt, eth->id(), "1::1", "1::2", 1, 2, 2, -1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_EQ(pkt_info.type, PktType::INVALID);

    pkt->Reset();
    MakeTcp6Packet(pkt, eth->id(), "1::1", "1::2", 1, 2, false, 3, -1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_EQ(pkt_info.type, PktType::INVALID);
}

TEST_F(PktParseTest, GRE_On_Vnet_1) {
    VmInterface *vnet1 = VmInterfaceGet(1);
    std::auto_ptr<PktGen> pkt(new PktGen());

    pkt->Reset();
    MakeIpMplsPacket(pkt.get(), vnet1->id(), "1.1.1.1", "1.1.1.2", 1,
                     "10.10.10.10", "11.11.11.11", 1, 1);
    PktInfo pkt_info1(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info1, pkt.get());
    client->WaitForIdle();
    EXPECT_EQ(pkt_info1.type, PktType::IP);
    EXPECT_EQ(pkt_info1.tunnel.label, 0xFFFFFFFF);

    pkt->Reset();
    MakeUdpMplsPacket(pkt.get(), vnet1->id(), "1.1.1.1", "1.1.1.2", 1,
                      "10.10.10.10", "11.11.11.11", 1, 2, 2);
    PktInfo pkt_info2(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info2, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info2, "1.1.1.1", "1.1.1.2", 47, 0, 0));
    EXPECT_EQ(pkt_info2.tunnel.label, 0xFFFFFFFF);

    pkt->Reset();
    MakeUdpMplsPacket(pkt.get(), vnet1->id(), "1.1.1.1", "1.1.1.2", 1,
                      "10.10.10.10", "11.11.11.11", 1, 2, 3);
    PktInfo pkt_info3(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info3, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info3, "1.1.1.1", "1.1.1.2", 47, 0, 0));
    EXPECT_EQ(pkt_info3.tunnel.label, 0xFFFFFFFF);
}

TEST_F(PktParseTest, IPv6_GRE_On_Vnet_1) {
    VmInterface *vnet1 = VmInterfaceGet(1);
    PktGen *pkt = new PktGen();
    PktInfo pkt_info(NULL, 0, 0, 0);

    pkt->Reset();
    MakeIp6MplsPacket(pkt, vnet1->id(), "1.1.1.1", "1.1.1.2", 1,
                      "10::10", "11::11", IPPROTO_ICMPV6, 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_EQ(pkt_info.type, PktType::IP);
    EXPECT_EQ(pkt_info.tunnel.label, 0xFFFFFFFF);

    pkt->Reset();
    MakeUdp6MplsPacket(pkt, vnet1->id(), "1.1.1.1", "1.1.1.2", 1,
                      "10::10", "11::11", 1, 2, 2);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info, "1.1.1.1", "1.1.1.2", 47, 0, 0));
    EXPECT_EQ(pkt_info.tunnel.label, 0xFFFFFFFF);

    pkt->Reset();
    MakeUdp6MplsPacket(pkt, vnet1->id(), "1.1.1.1", "1.1.1.2", 1,
                      "10::10", "11::11", 1, 2, 3);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info, "1.1.1.1", "1.1.1.2", 47, 0, 0));
    EXPECT_EQ(pkt_info.tunnel.label, 0xFFFFFFFF);
}

TEST_F(PktParseTest, GRE_On_Enet_1) {
    PhysicalInterface *eth = EthInterfaceGet("vnet0");
    VmInterface *vnet1 = VmInterfaceGet(1);
    std::auto_ptr<PktGen> pkt(new PktGen());

    pkt->Reset();
    MakeIpMplsPacket(pkt.get(), eth->id(), "1.1.1.1", "10.1.1.1",
                     vnet1->label(), "10.10.10.10", "11.11.11.11", 1, 1);
    PktInfo pkt_info1(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info1, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info1, "10.10.10.10", "11.11.11.11", 1, 0, 0));
    EXPECT_EQ(pkt_info1.tunnel.label, vnet1->label());

    pkt->Reset();
    MakeUdpMplsPacket(pkt.get(), eth->id(), "1.1.1.1", "10.1.1.1",
                      vnet1->label(), "10.10.10.10", "11.11.11.11", 1, 2, 1);
    PktInfo pkt_info2(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info2, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info2, "10.10.10.10", "11.11.11.11",
                                  IPPROTO_UDP, 1, 2));
    EXPECT_EQ(pkt_info2.tunnel.label, vnet1->label());

    pkt->Reset();
    MakeTcpMplsPacket(pkt.get(), eth->id(), "1.1.1.1", "10.1.1.1",
                      vnet1->label(), "10.10.10.10", "11.11.11.11", 1, 2, 
                      false, 1);
    PktInfo pkt_info3(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info3, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIpPktInfo(&pkt_info3, "10.10.10.10", "11.11.11.11",
                                  IPPROTO_TCP, 1, 2));
    EXPECT_EQ(pkt_info3.tunnel.label, vnet1->label());
}

TEST_F(PktParseTest, IPv6_GRE_On_Enet_1) {
    PhysicalInterface *eth = EthInterfaceGet("vnet0");
    VmInterface *vnet1 = VmInterfaceGet(1);
    PktGen *pkt = new PktGen();
    PktInfo pkt_info(NULL, 0, 0, 0);

    pkt->Reset();
    MakeIp6MplsPacket(pkt, eth->id(), "1.1.1.1", "10.1.1.1",
                      vnet1->label(), "10::10", "11::11", IPPROTO_ICMPV6, 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIp6PktInfo(&pkt_info, "10::10", "11::11",
                                   IPPROTO_ICMPV6, 0, 0));
    EXPECT_EQ(pkt_info.tunnel.label, vnet1->label());

    pkt->Reset();
    MakeUdp6MplsPacket(pkt, eth->id(), "1.1.1.1", "10.1.1.1",
                       vnet1->label(), "10::10", "11::11", 1, 2, 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIp6PktInfo(&pkt_info, "10::10", "11::11",
                                  IPPROTO_UDP, 1, 2));
    EXPECT_EQ(pkt_info.tunnel.label, vnet1->label());

    pkt->Reset();
    MakeTcp6MplsPacket(pkt, eth->id(), "1.1.1.1", "10.1.1.1",
                       vnet1->label(), "10::10", "11::11", 1, 2, 
                      false, 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(ValidateIp6PktInfo(&pkt_info, "10::10", "11::11",
                                  IPPROTO_TCP, 1, 2));
    EXPECT_EQ(pkt_info.tunnel.label, vnet1->label());
}

TEST_F(PktParseTest, Invalid_GRE_On_Enet_1) {
    PhysicalInterface *eth = EthInterfaceGet("vnet0");
    VmInterface *vnet1 = VmInterfaceGet(1);
    std::auto_ptr<PktGen> pkt(new PktGen());

    // Invalid Label
    pkt->Reset();
    MakeIpMplsPacket(pkt.get(), eth->id(), "1.1.1.1", "10.1.1.1",
                     1000, "10.10.10.10", "11.11.11.11", 1, 1);
    PktInfo pkt_info1(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info1, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(pkt_info1.ip != NULL);
    EXPECT_EQ(pkt_info1.type, PktType::INVALID);

    // Invalid IP-DA
    pkt->Reset();
    MakeUdpMplsPacket(pkt.get(), eth->id(), "1.1.1.1", "10.1.1.2",
                      vnet1->label(), "10.10.10.10", "11.11.11.11", 1, 2, 1);
    PktInfo pkt_info2(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info2, pkt.get());
    client->WaitForIdle();
    EXPECT_EQ(pkt_info2.type, PktType::INVALID);
    EXPECT_EQ(pkt_info2.tunnel.label, 0xFFFFFFFF);

    // Invalid Protocol in GRE header
    pkt->Reset();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(eth->id(), AgentHdr::TRAP_FLOW_MISS);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr("1.1.1.1", "10.1.1.1", IPPROTO_GRE);
    pkt->AddGreHdr(0x800);
    pkt->AddMplsHdr(vnet1->label(), true);
    pkt->AddIpHdr("1.1.1.1", "2.2.2.2", 1);
    PktInfo pkt_info3(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info3, pkt.get());
    client->WaitForIdle();
    EXPECT_EQ(pkt_info3.type, PktType::INVALID);
    EXPECT_EQ(pkt_info3.tunnel.label, 0xFFFFFFFF);

    // Pkt with MPLS Label stack
    pkt->Reset();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(eth->id(), AgentHdr::TRAP_FLOW_MISS);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr("1.1.1.1", "10.1.1.1", IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(vnet1->label(), false);
    pkt->AddMplsHdr(vnet1->label(), true);
    pkt->AddIpHdr("1.1.1.1", "2.2.2.2", 1);
    PktInfo pkt_info4(Agent::GetInstance(), 100, 0, 0);
    TestPkt(&pkt_info4, pkt.get());
    client->WaitForIdle();
    EXPECT_TRUE(pkt_info4.ip != NULL);
    EXPECT_EQ(pkt_info4.type, PktType::INVALID);
}

TEST_F(PktParseTest, IPv6_Invalid_GRE_On_Enet_1) {
    PhysicalInterface *eth = EthInterfaceGet("vnet0");
    VmInterface *vnet1 = VmInterfaceGet(1);
    PktGen *pkt = new PktGen();
    PktInfo pkt_info(NULL, 0, 0, 0);

    // Invalid Label
    pkt->Reset();
    MakeIp6MplsPacket(pkt, eth->id(), "1.1.1.1", "10.1.1.1",
                      1000, "10::10", "11::11", 1, 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(pkt_info.ip != NULL);
    EXPECT_EQ(pkt_info.type, PktType::INVALID);

    // Invalid IP-DA
    pkt->Reset();
    MakeUdp6MplsPacket(pkt, eth->id(), "1.1.1.1", "10.1.1.2",
                      vnet1->label(), "10::10", "11::11", 1, 2, 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_EQ(pkt_info.type, PktType::INVALID);
    EXPECT_EQ(pkt_info.tunnel.label, 0xFFFFFFFF);

    // Invalid Protocol in GRE header
    pkt->Reset();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(eth->id(), AGENT_TRAP_FLOW_MISS);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr("1.1.1.1", "10.1.1.1", IPPROTO_GRE);
    pkt->AddGreHdr(0x800);
    pkt->AddMplsHdr(vnet1->label(), true);
    pkt->AddIp6Hdr("1::1", "2::2", 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_EQ(pkt_info.type, PktType::INVALID);
    EXPECT_EQ(pkt_info.tunnel.label, 0xFFFFFFFF);

    // Pkt with MPLS Label stack
    pkt->Reset();
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(eth->id(), AGENT_TRAP_FLOW_MISS);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr("1.1.1.1", "10.1.1.1", IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(vnet1->label(), false);
    pkt->AddMplsHdr(vnet1->label(), true);
    pkt->AddIp6Hdr("1::1", "2::2", 1);
    TestPkt(&pkt_info, pkt);
    client->WaitForIdle();
    EXPECT_TRUE(pkt_info.ip != NULL);
    EXPECT_EQ(pkt_info.type, PktType::INVALID);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    ksync_init = false;
    client = TestInit(init_file, ksync_init, true, true, true);
    Setup();
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1"));

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    Teardown();
    TestShutdown();
    delete client;
    return ret;
}
