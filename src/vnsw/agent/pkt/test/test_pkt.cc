/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
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

class PktTest : public ::testing::Test {
public:
    void CheckSandeshResponse(Sandesh *sandesh) {
    }
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
    Agent::GetInstance()->pkt()->pkt_handler()->HandleRcvPkt(ptr, pkt->GetBuffLen());
    delete pkt;
}

static void MakeMplsPacket(PktGen *pkt, int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label, 
                            const char *sip, const char *dip, 
                            int proto) {
    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AGENT_TRAP_FLOW_MISS);
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
    Agent::GetInstance()->pkt()->pkt_handler()->HandleRcvPkt(ptr, pkt->GetBuffLen());
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
    EXPECT_EQ(4U, Agent::GetInstance()->GetInterfaceTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetVmTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetVnTable()->Size());
    EXPECT_EQ(1U, Agent::GetInstance()->GetIntfCfgTable()->Size());

    // Generate packet and enqueue
    VmInterface *intf = VmInterfaceGet(input[0].intf_id);
    assert(intf);
    TxIpPacket(intf->id(), "1.1.1.1", "1.1.1.2", 1);
    client->WaitForIdle();

    PhysicalInterface::CreateReq(Agent::GetInstance()->GetInterfaceTable(),
                            "vnet0", Agent::GetInstance()->GetDefaultVrf());
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
}


int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    Agent::GetInstance()->SetRouterId(Ip4Address::from_string("10.1.1.1"));

    return RUN_ALL_TESTS();
}
