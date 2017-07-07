/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <testing/gunit.h>
#include <base/logging.h>
#include <string>
#include "grok_parser.h"

class GrokParserTest : public ::testing::Test {
public:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

   GrokParser gp; 
};

TEST_F(GrokParserTest, MatchPattern) {
    std::string pattern = "<%{POSINT}>%{PROG:prog}: %{CONTRAIL_SYSLOG_TIMESTAMP:timestamp} %{HOSTNAME:hostname} \\[%{CONTRAIL_MODULE:module}\\]\\[%{CONTRAIL_LOG_LEVEL:log_level}\\] : %{GREEDYDATA:message}";
    std::string strin = "<12>contrail: 2017 Jul 05 16:01:01.886453 yw-dev-ci-target-14045-32g [Analytics:contrail-collector:0:None][SYS_NOTICE] : SandeshModuleServerTrace:39 [ModuleServerState: name = yw-dev-ci-target-14045-32g:Control:contrail-control:0, [msg_stats: [hostname = yw-dev-ci-target-14045-32g, [msgtype_stats: [message_type = BGPRouterInfo, messages = 1, bytes = 3820, last_msg_timestamp = 1499270412840567] [message_type = NodeStatusUVE, messages = 3, bytes = 7831, last_msg_timestamp = 1499270412841044] [message_type = PeerStatsUve, messages = 2, bytes = 5148, last_msg_timestamp = 1499270426923272] [message_type = RoutingInstanceStats, messages = 6, bytes = 14570, last_msg_timestamp = 1499270426923165] [message_type = SandeshModuleClientTrace, messages = 6, bytes = 126111, last_msg_timestamp = 1499270443972817] [message_type = XMPPPeerInfo, messages = 2, bytes = 6862, last_msg_timestamp = 1499270426923189]], [log_level_stats:]]], sm_queue_count = 0, [SandeshStateMachineStats: [ev_stats: [event = ssm::EvSandeshCtrlMessageRecv, enqueues = 1, dequeues = 1, enqueue_fails = 0, dequeue_fails = 0] [event = ssm::EvSandeshMessageRecv, enqueues = 20, dequeues = 20, enqueue_fails = 0, dequeue_fails = 0] [event = ssm::EvStart, enqueues = 1, dequeues = 1, enqueue_fails = 0, dequeue_fails = 0] [event = ssm::EvTcpPassiveOpen, enqueues = 1, dequeues = 1, enqueue_fails = 0, dequeue_fails = 0] [event = , enqueues = 23, dequeues = 23, enqueue_fails = 0, dequeue_fails = 0]], state = Established, state_since = 1499270412829426, last_state = Established, last_event = ssm::EvSandeshMessageRecv, last_event_at = 1499270444003594], [SandeshSessionStats: num_recv_msg = 0, num_recv_msg_fail = 0, num_recv_fail = 0, num_send_msg = 1, num_send_msg_fail = 0, num_send_buffer_fail = 0, num_wait_msgq_enqueue = 0, num_wait_msgq_dequeue = 0, num_write_ready_cb_error = 0], [SocketIOStats: bytes = 166788, calls = 20, average_bytes = 8339, blocked_duration = 00:00:00, blocked_count = 0, average_blocked_duration = , errors = 0], [SocketIOStats: bytes = 998, calls = 1, average_bytes = 998, blocked_duration = 00:00:00, blocked_count = 0, average_blocked_duration = , errors = 0], [SandeshGeneratorBasicStats: [type_stats: [message_type = BGPRouterInfo, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 1, bytes_received = 3820, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = NodeStatusUVE, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 3, bytes_received = 7831, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = PeerStatsUve, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 2, bytes_received = 5148, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = RoutingInstanceStats, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 6, bytes_received = 14570, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = SandeshCtrlClientToServer, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 1, bytes_received = 1627, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = SandeshModuleClientTrace, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 6, bytes_received = 126111, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = XMPPPeerInfo, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 2, bytes_received = 6862, messages_sent_dropped = 0, messages_received_dropped = 0]]], [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 21, bytes_received = 165969, messages_sent_dropped = 0, messages_received_dropped = 0], [SandeshQueueStats: enqueues = 0, count = 0, max_count = 0]], sm_drop_level = INVALID]]";

    EXPECT_TRUE(gp.grok_parser_match(pattern, strin));
}

TEST_F(GrokParserTest, Add_n_Delete) {
    gp.grok_parser_add_pattern_from_string("MYWORD \\w+");
    gp.grok_parser_add_pattern_from_string("TEST TEST");
    gp.grok_parser_add_pattern_from_string("MACROTEST %{MYWORD}%{TEST}");
    std::string pattern = "This is %{MYWORD:myword} %{TEST} and %{MACROTEST}";
    std::string strin = "This is AddPattern TEST and AddMacroPatternTEST";
    EXPECT_TRUE(gp.grok_parser_match(pattern, strin));
    /*
    EXPECT_TRUE(gp.grok_parser_delete_pattern("TEST"));
    pattern = "Delete %{TEST}";
    strin = "Delete TEST";
    EXPECT_FALSE(gp.grok_parser_match(pattern, strin));
    */
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


