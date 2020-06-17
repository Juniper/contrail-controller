/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "base/task.cc"
#include <base/sandesh/task_types.h>

void RouterIdDepInit(Agent *agent) {
}

class AgentTaskTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->WaitForIdle();
    }

    Agent *agent_;
};

TEST_F(AgentTaskTest, Test_BFD_RecvQueue_Exclusion) {
    int task_id  = TaskScheduler::GetInstance()->GetTaskId("sandesh::RecvQueue");
    client->WaitForIdle();
    TaskGroup *group = TaskScheduler::GetInstance()->QueryTaskGroup(task_id);
    client->WaitForIdle();
    SandeshTaskGroup resp_group;
    group->GetSandeshData(&resp_group, false);
    client->WaitForIdle();
    std::vector<SandeshTaskPolicyEntry> policy_list;
    policy_list = resp_group.get_task_policy_list();
    std::vector<SandeshTaskPolicyEntry>::iterator it;
    bool bfd_task_exclusion= false;
    // Check BFD task is present in exclusion policy list
    for (it = policy_list.begin(); it < policy_list.end(); it++) {
        if (it->task_name == "BFD")
            bfd_task_exclusion = true;
    }
    client->WaitForIdle();
    EXPECT_TRUE(bfd_task_exclusion);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, false, true, false);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
