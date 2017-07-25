#include "base/os.h"
#include "test/test_cmn_util.h"

void RouterIdDepInit(Agent *agent) {
}

class MplsTest : public ::testing::Test {

    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->WaitForIdle();
    }
protected:
    Agent *agent_;
};

TEST_F(MplsTest, agent_dpdk_mode) {
    uint16_t label1 = AllocLabel("test_dpdk_1");
    uint16_t label2 = AllocLabel("test_dpdk_2");

    EXPECT_TRUE(label1 == MplsTable::kStartLabel + 4);
    EXPECT_TRUE(label2 == MplsTable::kStartLabel + 5);

    FreeLabel(label1);
    FreeLabel(label2);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(DEFAULT_VNSW_DPDK_CONFIG_FILE,
                      ksync_init, true, true, true,
                      AgentParam::kAgentStatsInterval,
                      AgentParam::kFlowStatsInterval, true, false);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    return ret;
}

