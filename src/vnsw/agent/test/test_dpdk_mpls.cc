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

    uint16_t base_count = NH_PER_VM;
    EXPECT_TRUE(label1 == MplsTable::kStartLabel + base_count);
    EXPECT_TRUE(label2 == MplsTable::kStartLabel + base_count + 1);

    FreeLabel(label1);
    FreeLabel(label2);
}

TEST_F(MplsTest, mpls_label_usage_limit) {
    VrLimitExceeded &vr_limits = agent_->get_vr_limits_exceeded_map();
    VrLimitExceeded::iterator vr_limit_itr = vr_limits.find("vr_mpls_labels");
    EXPECT_EQ(vr_limit_itr->second, "Normal");
    uint32_t default_label_count = agent_->vrouter_max_labels();
    agent_->set_vrouter_max_labels(30);

    uint32_t default_high_watermark = agent_->vr_limit_high_watermark();
    agent_->set_vr_limit_high_watermark(60);

    uint32_t default_low_watermark = agent_->vr_limit_low_watermark();
    agent_->set_vr_limit_low_watermark(40);

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2},
        {"vnet3", 3, "1.1.1.3", "00:00:00:01:01:03", 1, 3},
        {"vnet4", 4, "1.1.1.4", "00:00:00:01:01:04", 1, 4},
        {"vnet2", 5, "1.1.1.5", "00:00:00:01:01:05", 1, 5},
    };
    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.254", true},
    };

    struct PortInfo input1[] = {
        {"vnet21", 1, "2.2.2.1", "00:00:00:01:02:01", 2, 1},
        {"vnet22", 2, "2.2.2.2", "00:00:00:01:02:02", 2, 2},
        {"vnet23", 3, "2.2.2.3", "00:00:00:01:02:03", 2, 3},
    };
    IpamInfo ipam_info1[] = {
        {"2.2.2.0", 24, "2.2.2.254", true},
    };

    AddIPAM("vn1", ipam_info, 1);
    AddIPAM("vn2", ipam_info1, 2);
    client->WaitForIdle();
    CreateVmportEnv(input, 5);
    CreateVmportEnv(input1, 3);
    client->WaitForIdle();

    // total 34 labels are added, check TableLimit is set
    WAIT_FOR(100, 100, (agent_->mpls_table()->LabelIndexCount() >= 32));
    EXPECT_EQ(vr_limit_itr->second, "TableLimit");

    DeleteVmportEnv(input, 1, false);
    client->WaitForIdle();

    // now label count is 29, greater 95% of TableLimit (30), still TableLimit
    WAIT_FOR(100, 100, (agent_->mpls_table()->LabelIndexCount() >= 29));
    WAIT_FOR(100, 100, (vr_limit_itr->second, "TableLimit"));

    DeleteVmportEnv(input, 2, false);
    client->WaitForIdle();

    // labels greater than 18 highwatermark is 18 Exceeded is set
    WAIT_FOR(100, 100, (agent_->mpls_table()->LabelIndexCount() >= 18));
    WAIT_FOR(100, 100, (vr_limit_itr->second, "Exceeded"));

    DeleteVmportEnv(input, 3, false);
    client->WaitForIdle();

    // now label count is 14, greater than low watermark 12, Exceeded is set
    WAIT_FOR(100, 100, (agent_->mpls_table()->LabelIndexCount() >= 14));
    WAIT_FOR(100, 100, (vr_limit_itr->second, "Exceeded"));

    DeleteVmportEnv(input, 5, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    // label count less than low watermark label usage is set to Normal
    WAIT_FOR(100, 100, (agent_->mpls_table()->LabelIndexCount() < 12));
    WAIT_FOR(100, 100, (vr_limit_itr->second, "Normal"));

    DeleteVmportEnv(input1, 3, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    agent_->set_vrouter_max_labels(default_label_count);
    agent_->set_vr_limit_high_watermark(default_high_watermark);
    agent_->set_vr_limit_low_watermark(default_low_watermark);
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

