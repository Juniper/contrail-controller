/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <testing/gunit.h>

extern "C" {
  #include <grok.h>
}

#include <base/logging.h>
#include <iostream>
#include <queue>
#include <string>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <map>

class GrokTest : public ::testing::Test {
public:
    virtual void SetUp() {
        grok_init(&grok_);
        grok_patterns_import_from_file(&grok_, "/etc/contrail/grok-pattern-base.conf");
    }

    virtual void TearDown() {
        grok_free(&grok_);
    }

    grok_t grok_;
};

TEST_F(GrokTest, ParseLog) {
    char pattern[256] = "<%{POSINT}>%{PROG:prog}: %{CONTRAIL_SYSLOG_TIMESTAMP:timestamp} %{HOSTNAME:hostname} \\[%{CONTRAIL_MODULE:module}\\]\\[%{CONTRAIL_LOG_LEVEL:log_level}\\] : %{GREEDYDATA:message}";
    std::string strin = "<12>contrail: 2017 Jul 05 16:01:01.886453 yw-dev-ci-target-14045-32g [Analytics:contrail-collector:0:None][SYS_NOTICE] : SandeshModuleServerTrace:39 [ModuleServerState: name = yw-dev-ci-target-14045-32g:Control:contrail-control:0, [msg_stats: [hostname = yw-dev-ci-target-14045-32g, [msgtype_stats: [message_type = BGPRouterInfo, messages = 1, bytes = 3820, last_msg_timestamp = 1499270412840567] [message_type = NodeStatusUVE, messages = 3, bytes = 7831, last_msg_timestamp = 1499270412841044] [message_type = PeerStatsUve, messages = 2, bytes = 5148, last_msg_timestamp = 1499270426923272] [message_type = RoutingInstanceStats, messages = 6, bytes = 14570, last_msg_timestamp = 1499270426923165] [message_type = SandeshModuleClientTrace, messages = 6, bytes = 126111, last_msg_timestamp = 1499270443972817] [message_type = XMPPPeerInfo, messages = 2, bytes = 6862, last_msg_timestamp = 1499270426923189]], [log_level_stats:]]], sm_queue_count = 0, [SandeshStateMachineStats: [ev_stats: [event = ssm::EvSandeshCtrlMessageRecv, enqueues = 1, dequeues = 1, enqueue_fails = 0, dequeue_fails = 0] [event = ssm::EvSandeshMessageRecv, enqueues = 20, dequeues = 20, enqueue_fails = 0, dequeue_fails = 0] [event = ssm::EvStart, enqueues = 1, dequeues = 1, enqueue_fails = 0, dequeue_fails = 0] [event = ssm::EvTcpPassiveOpen, enqueues = 1, dequeues = 1, enqueue_fails = 0, dequeue_fails = 0] [event = , enqueues = 23, dequeues = 23, enqueue_fails = 0, dequeue_fails = 0]], state = Established, state_since = 1499270412829426, last_state = Established, last_event = ssm::EvSandeshMessageRecv, last_event_at = 1499270444003594], [SandeshSessionStats: num_recv_msg = 0, num_recv_msg_fail = 0, num_recv_fail = 0, num_send_msg = 1, num_send_msg_fail = 0, num_send_buffer_fail = 0, num_wait_msgq_enqueue = 0, num_wait_msgq_dequeue = 0, num_write_ready_cb_error = 0], [SocketIOStats: bytes = 166788, calls = 20, average_bytes = 8339, blocked_duration = 00:00:00, blocked_count = 0, average_blocked_duration = , errors = 0], [SocketIOStats: bytes = 998, calls = 1, average_bytes = 998, blocked_duration = 00:00:00, blocked_count = 0, average_blocked_duration = , errors = 0], [SandeshGeneratorBasicStats: [type_stats: [message_type = BGPRouterInfo, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 1, bytes_received = 3820, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = NodeStatusUVE, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 3, bytes_received = 7831, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = PeerStatsUve, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 2, bytes_received = 5148, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = RoutingInstanceStats, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 6, bytes_received = 14570, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = SandeshCtrlClientToServer, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 1, bytes_received = 1627, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = SandeshModuleClientTrace, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 6, bytes_received = 126111, messages_sent_dropped = 0, messages_received_dropped = 0]] [message_type = XMPPPeerInfo, [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 2, bytes_received = 6862, messages_sent_dropped = 0, messages_received_dropped = 0]]], [SandeshMessageBasicStats: messages_sent = 0, bytes_sent = 0, messages_received = 21, bytes_received = 165969, messages_sent_dropped = 0, messages_received_dropped = 0], [SandeshQueueStats: enqueues = 0, count = 0, max_count = 0]], sm_drop_level = INVALID]]";

    grok_match_t gm;

    EXPECT_EQ(GROK_OK, grok_compile(&grok_, pattern));
    EXPECT_EQ(GROK_OK, grok_exec(&grok_, strin.c_str(), &gm));

    std::queue<std::string> q;
    std::string pattern_str = pattern;

    boost::regex ex("%\\{(\\w+):(\\w+)\\}");
    boost::smatch match;
    boost::sregex_token_iterator iter(pattern_str.begin(), pattern_str.end(), ex, 0);
    boost::sregex_token_iterator end;
    for (; iter != end; ++iter) {
        std::string r = *iter;
        std::vector<std::string> results;
        boost::split(results, r, boost::is_any_of(":"));
        EXPECT_EQ(2, results.size());
        boost::algorithm::trim_left_if(results[0], boost::is_any_of("%{"));
        boost::algorithm::trim_right_if(results[1], boost::is_any_of("}"));
        q.push(results[1]);
    }
    std::map<std::string, std::string> match_map;
    const char *m;
    int len(0);
    while (!q.empty()) {
        grok_match_get_named_substring(&gm, q.front().c_str(), &m, &len);
        match_map[q.front()] = std::string(m).substr(0,len);
        q.pop();
    }
    EXPECT_EQ("2017 Jul 05 16:01:01.886453", match_map.find("timestamp")->second);
    EXPECT_EQ("contrail", match_map.find("prog")->second);
    EXPECT_EQ("Analytics:contrail-collector:0:None", match_map.find("module")->second);
    EXPECT_EQ("yw-dev-ci-target-14045-32g", match_map.find("hostname")->second);
    EXPECT_EQ("SYS_NOTICE", match_map.find("log_level")->second);
}


TEST_F(GrokTest, AddPattern) {
    grok_pattern_add(&grok_, "MYWORD", 6,  "\\w+", 3);
    grok_pattern_add(&grok_, "TEST", 4, "TEST", 4);
    grok_pattern_add(&grok_, "MACROTEST", 9, "%{MYWORD}%{TEST}", 16);
    const char *regexp = NULL;
    size_t len = 0;

    grok_pattern_find(&grok_, "MYWORD", 6, &regexp, &len);
    EXPECT_TRUE(regexp != NULL);
    EXPECT_EQ(3, len);
    EXPECT_EQ(0, strncmp("\\w+", regexp, len));

    grok_pattern_find(&grok_, "TEST", 4, &regexp, &len);
    EXPECT_TRUE(regexp != NULL);
    EXPECT_EQ(4, len);
    EXPECT_EQ(0, strncmp("TEST", regexp, len));

    grok_pattern_find(&grok_, "MACROTEST", 9, &regexp, &len);
    EXPECT_TRUE(regexp != NULL);
    EXPECT_EQ(16, len);
    EXPECT_EQ(0, strncmp("%{MYWORD}%{TEST}", regexp, len));

    grok_match_t gm;
    EXPECT_EQ(GROK_OK, grok_compile(&grok_, "This is %{MYWORD:myword} %{TEST} and %{MACROTEST}")); 
    EXPECT_EQ(GROK_OK, grok_exec(&grok_, "This is AddPattern TEST and AddMacroPatternTEST", &gm));

    const char *m;
    grok_match_get_named_substring(&gm, "myword", &m, (int*)&len);
    EXPECT_EQ("AddPattern", std::string(m).substr(0,len));
    grok_match_get_named_substring(&gm, "TEST", &m, (int*)&len);
    EXPECT_EQ("TEST", std::string(m).substr(0,len));
    grok_match_get_named_substring(&gm, "MACROTEST", &m, (int*)&len);
    EXPECT_EQ("AddMacroPatternTEST", std::string(m).substr(0,len));
}

/*
TEST_F(GrokTest, DeletePattern) {
    grok_pattern_add(&grok_, "TESTDELETE", 10, "\\w+", 3);
    const char *regexp = NULL;
    size_t len = 0;

    grok_pattern_find(&grok_, "TESTDELETE", 10, &regexp, &len);
    EXPECT_TRUE(regexp != NULL);
    EXPECT_EQ(3, len);
    EXPECT_EQ(0, strncmp("\\w+", regexp, len));

    grok_pattern_delete(&grok_, "TESTDELETE", 10);

    grok_pattern_find(&grok_, "TESTDELETE", 10, &regexp, &len);
    EXPECT_TRUE(regexp == NULL);
}
*/

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

