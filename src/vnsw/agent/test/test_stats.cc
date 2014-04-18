/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_cmn.h"
#include "pkt/pkt_init.h"
#include "pkt/flow_table.h"
#include "test_cmn_util.h"
#include <uve/agent_uve.h>
 
#define MAX_VNET 6

int fd_table[MAX_VNET];

void RouterIdDepInit(Agent *agent) {
}

class StatsTest : public ::testing::Test {
};

TEST_F(StatsTest, IntfStatsTest) {
    unsigned int vn_count = 0;
    uint32_t bytes0 = 0, pkts0 = 0, bytes1 = 0, pkts1 = 0;

    struct PortInfo input[] = {
        {"test0", 8, "1.1.1.1", "00:00:00:01:01:01", 3, 1},
        {"test1", 9, "2.1.1.2", "00:00:00:02:02:02", 3, 2},
    };

    client->Reset();
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    vn_count++;

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_EQ(4U, Agent::GetInstance()->GetInterfaceTable()->Size());
    EXPECT_EQ(2U, Agent::GetInstance()->GetVmTable()->Size());
    EXPECT_EQ(vn_count, Agent::GetInstance()->GetVnTable()->Size());
    EXPECT_EQ(2U, Agent::GetInstance()->GetIntfCfgTable()->Size());

    /* Wait for stats-collector task to be run */

    cout << "sleep for: " << ((AgentStatsCollector::AgentStatsInterval + 1)*1000) << endl;
    usleep((AgentStatsCollector::AgentStatsInterval + 1)*1000);

    /* Verify that interface stats query succeeds 
     * These should return the present value of stats
     */
    EXPECT_TRUE(VmPortGetStats(input, 0, bytes0, pkts0));
    EXPECT_TRUE(VmPortGetStats(input, 1, bytes1, pkts1));

    /* Send ICMP packets to update statistics */
	send_icmp(fd_table[0], 8, 7, 0x01010102, 0x01010103);
	send_icmp(fd_table[1], 8, 7, 0x02010102, 0x02010103);
    client->WaitForIdle();

    /* Wait for stats-collector task to be run */
    usleep((AgentStatsCollector::AgentStatsInterval + 1)*1000);

    /* Verify interface stats */
    EXPECT_TRUE(VmPortStats(input, 0, (bytes0 + 42), (pkts0 + 1)));
    EXPECT_TRUE(VmPortStats(input, 1, (bytes1 + 42), (pkts1 + 1)));

    /* Send ICMP packets again to update statistics */
	send_icmp(fd_table[0], 8, 7, 0x01010102, 0x01010103);
	send_icmp(fd_table[1], 8, 7, 0x02010102, 0x02010103);
    client->WaitForIdle();

    /* Wait for stats-collector task to be run */
    usleep((AgentStatsCollector::AgentStatsInterval + 1)*1000);
    client->WaitForIdle();

    /* Verify updated interface stats */
    EXPECT_TRUE(VmPortStats(input, 0, (bytes0 + (42 * 2)), (pkts0 + 2)));
    EXPECT_TRUE(VmPortStats(input, 1, (bytes1 + (42 * 2)), (pkts1 + 2)));
}

TEST_F(StatsTest, FlowStatsTest) {
    unsigned int vn_count = 1, if_count = 4, cfg_if_count = 2;

    struct FlowIp flow_input[] = {
        {0x05010102, 0x05010103, "vrf4"},
        {0x04010102, 0x04010103, "vrf4"},
    };

    struct PortInfo input[] = {
        {"test2", 3, "5.1.1.1", "00:00:00:01:01:03", 4, 3},
        {"test3", 4, "4.1.1.1", "00:00:00:02:02:04", 4, 4},
        {"test4", 5, "5.1.1.3", "00:00:00:01:11:03", 4, 5},
        {"test5", 6, "4.1.1.3", "00:00:00:02:12:04", 4, 6},
    };

    client->Reset();
    CreateVmportEnv(input, 4, 2);
    client->WaitForIdle();
    vn_count++;

    usleep(1*1000*1000);
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_TRUE(VmPortActive(input, 2));
    EXPECT_TRUE(VmPortActive(input, 3));
    EXPECT_TRUE(VmPortPolicyEnable(input, 0));
    EXPECT_TRUE(VmPortPolicyEnable(input, 1));
    EXPECT_TRUE(VmPortPolicyEnable(input, 2));
    EXPECT_TRUE(VmPortPolicyEnable(input, 3));
    if_count += 4;
    EXPECT_EQ(if_count, Agent::GetInstance()->GetInterfaceTable()->Size());
    EXPECT_EQ(vn_count, Agent::GetInstance()->GetVnTable()->Size());
    cfg_if_count += 4;
    EXPECT_EQ(cfg_if_count, Agent::GetInstance()->GetIntfCfgTable()->Size());

    /* Flush any existing Flows */
    Agent::GetInstance()->pkt()->flow_table()->DeleteAll();
    client->WaitForIdle();
    usleep(1*1000*1000);
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());

    /* Send ICMP packets to vmport interfaces to create flows */
    send_icmp(fd_table[2], 8, 7, flow_input[0].sip, flow_input[0].dip);
    client->WaitForIdle();
    send_icmp(fd_table[3], 5, 7, flow_input[1].sip, flow_input[1].dip);

    client->WaitForIdle();
    client->WaitForIdle();
    EXPECT_EQ(2U, Agent::GetInstance()->pkt()->flow_table()->Size());

    /* Wait for stats-collector task to be run */
    usleep((FlowStatsCollector::FlowStatsInterval + 1)*1000);

    /* Match with expected flow stats */
    EXPECT_TRUE(FlowStats(flow_input, 0, 28, 1));
    EXPECT_TRUE(FlowStats(flow_input, 1, 28, 1));

    /* Send ICMP packets again to update flow statistics */
    send_icmp(fd_table[2], 8, 7, flow_input[0].sip, flow_input[0].dip);
    client->WaitForIdle();
    send_icmp(fd_table[3], 5, 7, flow_input[1].sip, flow_input[1].dip);
    
    /* Wait for stats-collector task to be run */
    usleep((FlowStatsCollector::FlowStatsInterval + 1)*1000);
    client->WaitForIdle();

    /* Verify updated interface stats */
    EXPECT_TRUE(FlowStats(flow_input, 0, (28 * 2), 2));
    EXPECT_TRUE(FlowStats(flow_input, 1, (28 * 2), 2));
    client->WaitForIdle();

    /* Flush all the Flows */
    Agent::GetInstance()->pkt()->flow_table()->DeleteAll();
    EXPECT_EQ(0U, Agent::GetInstance()->pkt()->flow_table()->Size());
    usleep(2*1000*1000);
    client->WaitForIdle();
}

int main(int argc, char *argv[]) {
    client = StatsTestInit();
    CreateTapInterfaces("test", MAX_VNET, fd_table);

    ::testing::InitGoogleTest(&argc, argv);
    usleep(1000);
    return RUN_ALL_TESTS();
}

