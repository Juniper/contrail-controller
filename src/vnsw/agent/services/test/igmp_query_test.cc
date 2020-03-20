/*
 * Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
 */

#include "igmp_common_test.h"

TEST_F(IgmpTest, IgmpQuerySend) {

    bool ret = false;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(true);
    uint32_t idx = 0;
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, true);
    }

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t sleep = 1;
    uint32_t vms = sizeof(input)/sizeof(struct PortInfo);

    //usleep((sleep * qivl * MSECS_PER_SEC) );
    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    // 7 VMs and 2 queries in 10 secs
    ret = WaitForTxCount(idx, true, vms*(sleep+1));
    EXPECT_EQ(ret, true);

    usleep(lmqt);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpQueryDisabled) {

    bool ret = false;

    TestEnvInit(UT_IGMP_VERSION_3, true);

    IgmpGlobalEnable(false);
    uint32_t idx = 0;
    for (idx = 0; idx < sizeof(input)/sizeof(PortInfo); idx++) {
        IgmpVmiEnable(idx, false);
    }

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t sleep = 1;
    uint32_t vms = sizeof(input)/sizeof(struct PortInfo);

    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    // 7 VMs and 2 queries in 10 secs
    ret = WaitForTxCount(idx, true, 0);
    EXPECT_EQ(ret, true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

TEST_F(IgmpTest, IgmpQuery_EnDisable) {

    bool ret = false;
    uint32_t idx = 0;

    TestEnvInit(UT_IGMP_VERSION_3, true);
    client->WaitForIdle();

    IgmpGlobalEnable(true);
    client->WaitForIdle();

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    uint32_t sleep = 1;
    uint32_t vms = sizeof(input)/sizeof(struct PortInfo);

    //usleep((sleep * qivl * MSECS_PER_SEC) );
    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    // 7 VMs and 2 queries in 10 secs
    ret = WaitForTxCount(idx, true, vms*(sleep+1));
    EXPECT_EQ(ret, true);

    IgmpGlobalEnable(false);
    client->WaitForIdle();

    Agent::GetInstance()->GetIgmpProto()->ClearStats();
    Agent::GetInstance()->GetIgmpProto()->GetGmpProto()->ClearStats();
    client->WaitForIdle();

    usleep((sleep * qivl * MSECS_PER_SEC) + 3000000);
    client->WaitForIdle();

    idx = 0;
    // 7 VMs and 2 queries in 10 secs
    ret = WaitForTxCount(idx, true, 0);
    EXPECT_EQ(ret, true);

    IgmpGlobalClear();

    TestEnvDeinit();
    client->WaitForIdle();

    return;
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(DEFAULT_VNSW_CONFIG_FILE, ksync_init, true, true);
    client->WaitForIdle();

    StartControlNodeMock();

    Agent *agent = Agent::GetInstance();
    RouterIdDepInit(agent);

    // Disable MVPN mode for normal IGMP testing.
    agent->params()->set_mvpn_ipv4_enable(false);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();

    StopControlNodeMock();

    TestShutdown();
    delete client;

    return ret;
}

