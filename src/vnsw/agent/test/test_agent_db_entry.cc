/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "test_cmn_util.h"

void RouterIdDepInit(Agent *agent) {
}

class AgentDbEntry : public ::testing::Test {
public:
    AgentDbEntry() {
        agent_ = Agent::GetInstance();
    }
    virtual ~AgentDbEntry() { }
    Agent *agent_;
};

TEST_F(AgentDbEntry, db_entry_self_reference) {
   struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.10", "00:00:01:01:01:10", 1, 1},
    };

    client->Reset();
    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    VmInterfaceKey *intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                  MakeUuid(1), "vnet1");
    InterfaceNHKey intf_nh_key(intf_key, false,
                               InterfaceNHFlags::INET4,
                               MacAddress::FromString("00:00:01:01:01:10"));
    NextHop *nh = GetNH(&intf_nh_key);
    EXPECT_TRUE(nh->GetRefCount() != 0);
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, false);
    
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
