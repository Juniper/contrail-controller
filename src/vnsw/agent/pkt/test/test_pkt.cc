/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <boost/uuid/string_generator.hpp>

#include <io/event_manager.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "ksync/ksync_init.h"

#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "pkt/pkt_handler.h"

#include "vr_interface.h"
#include "vr_types.h"

#include "test/test_cmn_util.h"
#include "test/pkt_gen.h"
#include <controller/controller_vrf_export.h>

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
};

class PktTest : public ::testing::Test {
public:
    void CheckSandeshResponse(Sandesh *sandesh) {
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    }

    void SetUp() {
        agent_ = client->agent();
        pkt_info_.reset(new PktInfo(agent_, 1024, PktHandler::ARP, 0));
        handler_.reset(new ArpHandler(agent_, pkt_info_,
                                     *(agent_->event_manager()->io_service())));

        client->Reset();
        CreateVmportEnv(input, 1, 1);
        client->WaitForIdle();

        EXPECT_TRUE(VmPortActive(input, 0));
        EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    }

    void TearDown() {
        FlushFlowTable();
        client->WaitForIdle();
        DeleteVmportEnv(input, 1, true, 1);
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (VmPortGet(1) == NULL));
    }

    Agent *agent_;
    boost::shared_ptr<ArpHandler> handler_;
    boost::shared_ptr<PktInfo> pkt_info_;
};

static void MakeIpPacket(PktGen *pkt, int ifindex, const char *sip,
                         const char *dip, int proto) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, 0);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(sip, dip, proto);
    return;
}

static void TxIpPacket(int ifindex, const char *sip, const char *dip, 
                            int proto) {
    PktGen *pkt = new PktGen();
    MakeIpPacket(pkt, ifindex, sip, dip, proto);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

static void MakeMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label, 
                            const char *sip, const char *dip, 
                            int proto) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_FLOW_MISS);
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddIpHdr(out_sip, out_dip, IPPROTO_GRE);
    pkt->AddGreHdr();
    pkt->AddMplsHdr(label, true);
    pkt->AddIpHdr(sip, dip, proto);
}

static void TxMplsPacket(int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label, 
                            const char *sip, const char *dip, 
                            int proto) {
    PktGen *pkt = new PktGen();
    MakeMplsPacket(pkt, ifindex, out_sip, out_dip, label, sip, dip, proto);
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
                                                    pkt->GetBuffLen());
    delete pkt;
}

TEST_F(PktTest, FlowAdd_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1, 1);
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    EXPECT_EQ(4U, Agent::GetInstance()->interface_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vm_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->vn_table()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->interface_config_table()->Size());

    // Generate packet and enqueue
    VmInterface *intf = VmInterfaceGet(input[0].intf_id);
    assert(intf);
    TxIpPacket(intf->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                            "vnet0", Agent::GetInstance()->fabric_vrf_name(),
                             PhysicalInterface::FABRIC);
    client->WaitForIdle();
    TxMplsPacket(2, "1.1.1.2", "10.1.1.1", 0, "2.2.2.2", "3.3.3.3", 1);
    
    TxMplsPacket(2, "1.1.1.3", "10.1.1.1", 0, "2.2.2.3", "3.3.3.4", 1);
    TxMplsPacket(2, "1.1.1.4", "10.1.1.1", 0, "2.2.2.4", "3.3.3.5", 1);
    TxMplsPacket(2, "1.1.1.5", "10.1.1.1", 0, "2.2.2.5", "3.3.3.6", 1);
    client->WaitForIdle();

    // Fetch introspect data
    AgentStatsReq *sand = new AgentStatsReq();
    Sandesh::set_response_callback(boost::bind(&PktTest::CheckSandeshResponse,
                                               this, _1));
    sand->HandleRequest();
    client->WaitForIdle();
    sand->Release();

    client->WaitForIdle();
    DeleteVmportEnv(input, 1, true, 1);
}

TEST_F(PktTest, tx_no_vlan_1) {
    int len;
    PktInfo pkt_info(agent_, 1024, PktHandler::ARP, 0);

    char *buff = (char *)pkt_info.pkt;
    uint16_t *data_p = (uint16_t *)buff;
    pkt_info.eth = (struct ether_header *) buff;

    len = handler_->EthHdr(buff, ARP_TX_BUFF_LEN, MacAddress::BroadcastMac(),
                         MacAddress::BroadcastMac(), ETHERTYPE_ARP,
                         VmInterface::kInvalidVlanId);
    EXPECT_TRUE(len == 14);
    EXPECT_TRUE(pkt_info.eth->ether_type == htons(ETHERTYPE_ARP));
    EXPECT_TRUE(*(data_p + 6) == htons(ETHERTYPE_ARP));

    VmInterface *intf = VmInterfaceGet(input[0].intf_id);
    len = handler_->EthHdr(buff, ARP_TX_BUFF_LEN, intf,
                         MacAddress::BroadcastMac(), MacAddress::BroadcastMac(),
                         ETHERTYPE_ARP);
    EXPECT_TRUE(len == 14);
    EXPECT_TRUE(pkt_info.eth->ether_type == htons(ETHERTYPE_ARP));
    EXPECT_TRUE(*(data_p + 6) == htons(ETHERTYPE_ARP));

    len = handler_->EthHdr(buff, ARP_TX_BUFF_LEN, agent_->vhost_interface(),
                        MacAddress::BroadcastMac(), MacAddress::BroadcastMac(),
                        ETHERTYPE_ARP);
    EXPECT_TRUE(len == 14);
    EXPECT_TRUE(pkt_info.eth->ether_type == htons(ETHERTYPE_ARP));
    EXPECT_TRUE(*(data_p + 6) == htons(ETHERTYPE_ARP));
}

TEST_F(PktTest, tx_vlan_1) {
    int len;
    PktInfo pkt_info(agent_, 1024, PktHandler::ARP, 0);

    pkt_info.AllocPacketBuffer(agent_, PktHandler::ARP, ARP_TX_BUFF_LEN, 0);
    char *buff = (char *)pkt_info.pkt;
    uint16_t *data_p = (uint16_t *)buff;
    pkt_info.eth = (struct ether_header *) buff;

    len = handler_->EthHdr(buff, ARP_TX_BUFF_LEN, MacAddress::BroadcastMac(),
                           MacAddress::BroadcastMac(), ETHERTYPE_ARP, 1);
    EXPECT_TRUE(len == 18);
    EXPECT_TRUE(*(data_p + 6) == htons(ETHERTYPE_VLAN));
    EXPECT_TRUE(*(data_p + 8) == htons(ETHERTYPE_ARP));

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, MakeUuid(2),
                                     "vm-itf-2"));
    req.data.reset(new VmInterfaceAddData(Ip4Address::from_string("1.1.1.2"),
                                          "00:00:00:00:00:01",
                                          "vm-1", MakeUuid(1), 1, 2, "vnet0",
                                          Ip6Address()));
    agent_->interface_table()->Enqueue(&req);
    client->WaitForIdle();

    VmInterface *intf = VmInterfaceGet(2);
    len = handler_->EthHdr(buff, ARP_TX_BUFF_LEN, intf,
                         MacAddress::BroadcastMac(), MacAddress::BroadcastMac(),
                         ETHERTYPE_ARP);
    EXPECT_TRUE(len == 18);
    EXPECT_TRUE(*(data_p + 6) == htons(ETHERTYPE_VLAN));
    EXPECT_TRUE(*(data_p + 8) == htons(ETHERTYPE_ARP));

    DBRequest req1(DBRequest::DB_ENTRY_DELETE);
    req1.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, MakeUuid(2),
                                     "vm-itf-2"));
    agent_->interface_table()->Enqueue(&req1);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->set_router_id(Ip4Address::from_string("10.1.1.1"));

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
