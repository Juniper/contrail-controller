/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <base/logging.h>

#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <oper/vrf.h>
#include <pugixml/pugixml.hpp>
#include <services/arp_proto.h>
#include <test/test_cmn_util.h>
#include <test/pkt_gen.h>
#include "vr_types.h"
#include <services/services_sandesh.h>

// Create vm-port and vn
struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

class MacLearningTest : public ::testing::Test {
public:
    MacLearningTest() {
        agent_ = Agent::GetInstance();
    };

    virtual void SetUp() {
        CreateVmportEnv(input, 2);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VrfFind("vrf1", true));
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
    }
protected:
    Agent *agent_;
};

void TxL2Packet(int ifindex, const char *smac, const char *dmac,
                const char *sip, const char *dip,
                int proto, int hash_id, int vrf,
                uint16_t sport, uint16_t dport) {
    PktGen *pkt = new PktGen();

    pkt->AddEthHdr("00:00:00:00:00:01", "00:00:00:00:00:02", 0x800);
    pkt->AddAgentHdr(ifindex, AgentHdr::TRAP_MAC_LEARN, hash_id, vrf) ;
    pkt->AddEthHdr(dmac, smac, 0x800);
    pkt->AddIpHdr(sip, dip, proto);
    if (proto == 1) {
        pkt->AddIcmpHdr();
    } else if (proto == IPPROTO_UDP) {
        pkt->AddUdpHdr(sport, dport, 64);
    }
    uint8_t *ptr(new uint8_t[pkt->GetBuffLen()]);
    memcpy(ptr, pkt->GetBuff(), pkt->GetBuffLen());
    client->agent_init()->pkt0()->ProcessFlowPacket(ptr, pkt->GetBuffLen(),
            pkt->GetBuffLen());
    delete pkt;
}

TEST_F(MacLearningTest, Test1) {
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.1", "1.1.1.11", 1, 100, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);

    VmInterface::Delete(Agent::GetInstance()->interface_table(),
                         MakeUuid(1), VmInterface::INSTANCE_MSG);
    client->WaitForIdle();
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) == NULL);
}

TEST_F(MacLearningTest, Test2) {
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.1", "1.1.1.11", 1, 100, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    AgentRoute *rt = EvpnRouteGet("vrf1", smac, Ip4Address(0), 0);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    const InterfaceNH *intf_nh =
        dynamic_cast<const InterfaceNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(intf_nh->GetInterface() == intf);

    const VmInterface *intf2 = static_cast<const VmInterface *>(VmPortGet(2));
    TxL2Packet(intf2->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.1", "1.1.1.11", 1, 100, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    rt = EvpnRouteGet("vrf1", smac, Ip4Address(0), 0);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    intf_nh = dynamic_cast<const InterfaceNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(intf_nh->GetInterface() == intf2);

    VmInterface::Delete(Agent::GetInstance()->interface_table(),
                        MakeUuid(1), VmInterface::INSTANCE_MSG);
    client->WaitForIdle();
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    rt = EvpnRouteGet("vrf1", smac, Ip4Address(0), 0);
    EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    intf_nh = dynamic_cast<const InterfaceNH *>(rt->GetActiveNextHop());
    EXPECT_TRUE(intf_nh->GetInterface() == intf2);

    VmInterface::Delete(Agent::GetInstance()->interface_table(),
                        MakeUuid(2), VmInterface::INSTANCE_MSG);
    client->WaitForIdle();
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) == NULL);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
