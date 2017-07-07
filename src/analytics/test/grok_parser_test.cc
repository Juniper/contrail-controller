/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <testing/gunit.h>
#include "grok_parser.h"
#include <base/logging.h>
#include <iostream>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>

    std::string msg1("<12>contrail: 2017 Jul 05 16:01:01.886453 yw-dev-ci-target-14045-32g [Analytics:contrail-collector:0:None][SYS_NOTICE] : mockdata");
    std::string msg2("Jan  1 06:25:43 mailserver14 postfix/cleanup[21403]: BEF25A72965: message-id=<20130101142543.5828399CCAF@mailserver14.example.com>");
    std::string msg3("55.3.244.1 GET /index.html 15824 0.043");
    std::string pat1("CONTRAIL_LOG <%{POSINT}>%{PROG:prog}: %{CONTRAIL_SYSLOG_TIMESTAMP:timestamp} %{HOSTNAME:hostname} \\[%{CONTRAIL_MODULE:module}\\]\\[%{CONTRAIL_LOG_LEVEL:log_level}\\] : %{GREEDYDATA:message}");
    std::string pat2("OTHER_LOG %{SYSLOGBASE} %{POSTFIX_QUEUEID:queue_id}: %{GREEDYDATA:syslog_message}");
    std::string pat3("HTTP_LOG %{IP:client} %{WORD:method} %{URIPATHPARAM:request} %{NUMBER:bytes} %{NUMBER:duration}");

class GrokParserTest : public ::testing::Test {
public:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
    GrokParser gp;
};

TEST_F(GrokParserTest, Add_Match_n_Delete) {
    gp.add_base_pattern(pat1);
    EXPECT_TRUE(gp.msg_type_add("CONTRAIL_LOG"));
    EXPECT_TRUE(gp.msg_type_add("MAC"));
    EXPECT_FALSE(gp.msg_type_add("SHOULD_NOT_EXIST"));
    EXPECT_TRUE("CONTRAIL_LOG" == gp.match(msg1));
    EXPECT_TRUE(gp.msg_type_del("CONTRAIL_LOG"));
    EXPECT_FALSE(gp.msg_type_del("SHOULD_NOT_EXIST"));
    EXPECT_TRUE("" == gp.match(msg1));
}

TEST_F(GrokParserTest, MultipleMsgType) {
    gp.add_base_pattern(pat1);
    gp.add_base_pattern(pat2);
    gp.add_base_pattern(pat3);
    gp.add_base_pattern("POSTFIX_QUEUEID [0-9A-F]{10,11}");
    EXPECT_TRUE(gp.msg_type_add("CONTRAIL_LOG"));
    EXPECT_TRUE(gp.msg_type_add("OTHER_LOG"));
    EXPECT_TRUE(gp.msg_type_add("HTTP_LOG"));
    EXPECT_TRUE("CONTRAIL_LOG" == gp.match(msg1));
    EXPECT_TRUE("OTHER_LOG" == gp.match(msg2));
    EXPECT_TRUE("HTTP_LOG" == gp.match(msg3));
    std::map<std::string, std::string> matched_data;
    gp.get_matched_data(msg1, &matched_data);
    std::map<std::string, std::string> matched_data2;
    gp.get_matched_data(msg2, &matched_data2);
    EXPECT_TRUE("postfix/cleanup" == matched_data2["program"]);
    std::map<std::string, std::string> matched_data3;
    gp.get_matched_data(msg3, &matched_data3);
    std::cout<<"destroy" << std::endl;    
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

