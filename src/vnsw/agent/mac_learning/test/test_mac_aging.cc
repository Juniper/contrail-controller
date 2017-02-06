/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "testing/gunit.h"

#include <unistd.h>
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
#include "mac_learning/mac_learning.h"
#include "mac_learning/mac_learning_proto.h"
#include "mac_learning/mac_aging.h"
#include "ksync/ksync_sock.h"
#include "ksync/ksync_sock_user.h"

// Create vm-port and vn
struct PortInfo input[] = {
    {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
};

struct PortInfo input1[] = {
    {"vnet3", 3, "2.1.1.1", "00:00:00:01:01:01", 2, 3},
    {"vnet4", 4, "2.1.1.2", "00:00:00:01:01:02", 2, 4}
};

IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

IpamInfo ipam_info1[] = {
    {"2.1.1.0", 24, "2.1.1.10"},
};


class MacAgingTest : public ::testing::Test {
public:
    MacAgingTest() {
        agent_ = Agent::GetInstance();
    };

    virtual void SetUp() {
        CreateVmportEnv(input, 2);
        CreateVmportEnv(input1, 2);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        EXPECT_TRUE(VmPortActive(2));
        AddIPAM("vn1", ipam_info, 1);
        AddIPAM("vn2", ipam_info1, 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true);
        DeleteVmportEnv(input1, 2, true);
        client->WaitForIdle();
        EXPECT_FALSE(VmPortFindRetDel(1));
        EXPECT_FALSE(VrfFind("vrf1", true));
        client->WaitForIdle();
        DelIPAM("vn1");
        DelIPAM("vn2");
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

TEST_F(MacAgingTest, Test1) {
    uint32_t timeout1 = 1000 * 1000;//1 second timeout
    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);

    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.1", "1.1.1.11", 1, 100, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    VrfEntry *vrf = VrfGet("vrf1");
    vrf->set_mac_aging_time(1);
    uint32_t table_id = agent_->mac_learning_proto()->Hash(vrf->vrf_id(), smac);
    MacLearningPartition *table = agent_->mac_learning_proto()->Find(table_id);
    table->aging_partition()->Find(vrf->vrf_id())->set_timeout(100);
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);

    usleep(timeout1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) == NULL));
}

TEST_F(MacAgingTest, Test2) {
    uint32_t timeout1 = 1000 * 1000; //1 second
    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);

    VrfEntry *vrf1 = VrfGet("vrf1");
    VrfEntry *vrf2 = VrfGet("vrf2");

    vrf1->set_mac_aging_time(1);
    vrf2->set_mac_aging_time(2);

    const VmInterface *intf1 = static_cast<const VmInterface *>(VmPortGet(1));
    const VmInterface *intf2 = static_cast<const VmInterface *>(VmPortGet(3));
    TxL2Packet(intf1->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.1", "1.1.1.11", 1, 100, intf1->vrf()->vrf_id(), 1, 1);
    TxL2Packet(intf2->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
            "1.1.1.1", "1.1.1.11", 1, 100, intf2->vrf()->vrf_id(), 1, 2);
    client->WaitForIdle();

    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    EXPECT_TRUE(EvpnRouteGet("vrf2", smac, Ip4Address(0), 0) != NULL);

    usleep(timeout1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) == NULL));
    EXPECT_TRUE(EvpnRouteGet("vrf2", smac, Ip4Address(0), 0) != NULL);

    usleep(timeout1);
    client->WaitForIdle();
    WAIT_FOR(1000, 1000, (EvpnRouteGet("vrf2", smac, Ip4Address(0), 0) == NULL));
}

TEST_F(MacAgingTest, Test3) {
    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);
    const VmInterface *intf = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.1", "1.1.1.11", 1, 100, intf->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    VrfEntry *vrf = VrfGet("vrf1");
    uint32_t table_id = agent_->mac_learning_proto()->Hash(vrf->vrf_id(), smac);
    MacLearningPartition *table = agent_->mac_learning_proto()->Find(table_id);

    MacAgingTable *aging_table = table->aging_partition()->Find(vrf->vrf_id());

    //Set aging timeout to 1 second
    vrf->set_mac_aging_time(1);

    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(1000) == 1000);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(100) ==
                MacAgingTable::kMinEntriesPerScan);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(10000) == 10000);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(5000) == 5000);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(4639) == 4639);

    vrf->set_mac_aging_time(5);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(1000) == 200);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(100) ==
            MacAgingTable::kMinEntriesPerScan);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(10000) == 2000);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(5000) == 1000);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(4639) == 4639/5);

    //Timeout of 3 mins
    vrf->set_mac_aging_time(180);
    EXPECT_TRUE(aging_table->CalculateEntriesPerIteration(100000) == 100000/180);
}

TEST_F(MacAgingTest, Test4) {
    uint32_t timeout1 = 100; //100 ms
    VrfEntry *vrf1 = VrfGet("vrf1");
    MacAddress smac(0x00, 0x00, 0x00, 0x11, 0x22, 0x33);

    const VmInterface *intf1 = static_cast<const VmInterface *>(VmPortGet(1));
    TxL2Packet(intf1->id(), "00:00:00:11:22:33", "00:00:00:33:22:11",
               "1.1.1.1", "1.1.1.11", 1, 100, intf1->vrf()->vrf_id(), 1, 1);
    client->WaitForIdle();

    vrf1->set_mac_aging_time(1);

    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
    KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
    vr_bridge_entry *entry = sock->GetBridgeEntry(1);
    entry->be_packets += 1;

    int i = 0;
    while (i < 10) {
        usleep(timeout1 / 2);
        entry->be_packets += 1;
        i++;
    }
    EXPECT_TRUE(EvpnRouteGet("vrf1", smac, Ip4Address(0), 0) != NULL);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    return ret;
}
