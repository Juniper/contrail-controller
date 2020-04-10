/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <base/os.h>
#include <base/address_util.h>
#include "test/test_cmn_util.h"
#include "ksync/ksync_sock_user.h"
#include "vrouter/ksync/ksync_bridge_table.h"

class BridgeEntryAuditTest : public ::testing::Test {
public:
    BridgeEntryAuditTest() : agent_(Agent::GetInstance()) {
    }

    virtual void SetUp() {
        KPurgeHoldBridgeEntries();
    }

    virtual void TearDown() {
        KPurgeHoldBridgeEntries();
    }

    void RunBridgeEntryAudit() {
        KSyncBridgeMemory *br_memory = agent_->ksync()->ksync_bridge_memory();
        br_memory->AuditProcess();
        // audit timeout set to 10 in case of test code.
        // Sleep for audit duration
        usleep(br_memory->audit_timeout() * 2);
        br_memory->AuditProcess();
    }

    bool KAddHoldBridgeEntry(uint32_t idx, int vrf, uint8_t *mac) {
        KSyncBridgeMemory *br_memory = agent_->ksync()->ksync_bridge_memory();
        if (idx >= br_memory->table_entries_count()) {
            return false;
        }

        vr_route_req req;
        req.set_rtr_vrf_id(vrf);

        std::vector<int8_t> prefix(mac, mac+6);
        req.set_rtr_family(AF_BRIDGE);
        req.set_rtr_mac(prefix);
        req.set_rtr_index(idx);

        KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
        sock->RouteAdd(req);
        sock->UpdateBridgeEntryInactiveFlag(idx, true);
        return true;
    }

    void KPurgeHoldBridgeEntries() {
        KSyncSockTypeMap *sock = KSyncSockTypeMap::GetKSyncSockTypeMap();
        KSyncBridgeMemory *br_memory = agent_->ksync()->ksync_bridge_memory();
        for (size_t count = 0; count < br_memory->table_entries_count();
             count++) {
            sock->UpdateBridgeEntryInactiveFlag(count, false);
        }

        return;
    }

    uint32_t KHoldBridgeEntryCount() {
        uint32_t count = 0;
        uint8_t dummy_gen_id = 0;
        KSyncBridgeMemory *br_memory = agent_->ksync()->ksync_bridge_memory();
        for (size_t idx = 0; idx < br_memory->table_entries_count(); idx++) {
            if (br_memory->IsInactiveEntry(idx, dummy_gen_id)) {
                count++;
            }
        }
        return count;
    }

    Agent *agent() {return agent_;}

public:
    Agent *agent_;
};

// Validate flows audit
TEST_F(BridgeEntryAuditTest, BridgeAudit_1) {
    uint8_t mac1[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    uint8_t mac2[] = { 0x02, 0x02, 0x03, 0x04, 0x05, 0x06 };
    // Create two hold-flows
    EXPECT_TRUE(KAddHoldBridgeEntry(1, 1, mac1));
    EXPECT_TRUE(KAddHoldBridgeEntry(2, 1, mac2));

    EXPECT_EQ(2U, KHoldBridgeEntryCount());
    RunBridgeEntryAudit();
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (0 == KHoldBridgeEntryCount()));
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
