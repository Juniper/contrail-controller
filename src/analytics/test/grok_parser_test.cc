/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <testing/gunit.h>
#include "grok_parser.h"
#include <base/logging.h>
#include <iostream>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>

std::string msg2("Jan  1 06:25:43 mailserver14 postfix/cleanup[21403]: BEF25A72965: message-id=<20130101142543.5828399CCAF@mailserver14.example.com>");
std::string msg3("55.3.244.1 GET /index.html 15824 0.043");
std::string pat2("OTHER_LOG %{SYSLOGBASE} %{POSTFIX_QUEUEID:queue_id}: %{GREEDYDATA:syslog_message}");
std::string pat3("HTTP_LOG %{IP:client} %{WORD:method} %{URIPATHPARAM:request} %{NUMBER:bytes} %{NUMBER:duration}");

class GrokParserTest : public ::testing::Test {
public:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(GrokParserTest, DISABLED_Add_Match_n_Delete) {
    GrokParser gp;
    gp.init();
    gp.add_base_pattern(pat2);
    gp.add_base_pattern("POSTFIX_QUEUEID [0-9A-F]{10,11}");
    EXPECT_TRUE(gp.msg_type_add("OTHER_LOG"));
    EXPECT_FALSE(gp.msg_type_add("SHOULD_NOT_EXIST"));
    EXPECT_TRUE(gp.match(msg2, NULL));
    EXPECT_TRUE(gp.msg_type_del("OTHER_LOG"));
    EXPECT_FALSE(gp.msg_type_del("SHOULD_NOT_EXIST"));
    EXPECT_FALSE(gp.match(msg2, NULL));
}

TEST_F(GrokParserTest, DISABLED_MultipleMsgType) {
    GrokParser gp;
    gp.init();
    gp.add_base_pattern(pat2);
    gp.add_base_pattern(pat3);
    gp.add_base_pattern("POSTFIX_QUEUEID [0-9A-F]{10,11}");
    EXPECT_TRUE(gp.msg_type_add("OTHER_LOG"));
    EXPECT_TRUE(gp.msg_type_add("HTTP_LOG"));
    std::map<std::string, std::string> matched_data2;
    EXPECT_TRUE(gp.match(msg2, &matched_data2));
    EXPECT_TRUE("postfix/cleanup" == matched_data2["program"]);
    std::map<std::string, std::string> matched_data3;
    EXPECT_TRUE(gp.match(msg3, &matched_data3));
    EXPECT_TRUE("OTHER_LOG" == matched_data2["Message Type"]);
    EXPECT_TRUE("HTTP_LOG" == matched_data3["Message Type"]);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

